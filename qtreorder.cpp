/* qtreorder.cpp -- Ensure the moov atom comes before the mdat atom in QuickTime files
 * Copyright (c) 2009-2010 Matthew Hoops (clone2727)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <cstdio>
#include <cstring>

// Standard types
typedef unsigned char byte;
typedef unsigned short uint16;
typedef unsigned int uint32;

// Constants
enum {
	kBufSize = 16384,
	kMoovTag = 'moov',
	kMdatTag = 'mdat'
};

// Helper functions for reading integers from the stream (maintaining endianness)
byte readByte(FILE *file) {
	byte b = 0;
	fread(&b, 1, 1, file);
	return b;
}

uint16 readUint16BE(FILE *file) {
	uint16 x = readByte(file) << 8;
	return x | readByte(file);
}

uint32 readUint32BE(FILE *file) {
	uint32 x = readUint16BE(file) << 16;
	return x | readUint16BE(file); 
}

void writeByte(FILE *file, byte b) {
	fwrite(&b, 1, 1, file);
}

void writeUint16BE(FILE *file, uint16 x) {
	writeByte(file, x >> 8);
	writeByte(file, x & 0xff);
}

void writeUint32BE(FILE *file, uint32 x) {
	writeUint16BE(file, x >> 16);
	writeUint16BE(file, x & 0xffff);
}

void copyData(FILE *in, FILE *out, uint32 length) {
	byte *buf = new byte[kBufSize];
	
	while (length > 0) {
		uint32 chunkSize = (length < kBufSize) ? length : kBufSize;
		fread(buf, chunkSize, 1, in);
		fwrite(buf, chunkSize, 1, out); 
		length -= chunkSize;
	}

	delete[] buf;
}

void copyAtomToFile(FILE *in, FILE *out, uint32 moovSize) {
	uint32 atomSize = readUint32BE(in);
	uint32 atomTag = readUint32BE(in);
	writeUint32BE(out, atomSize);
	writeUint32BE(out, atomTag);

	if (atomTag == 'trak' || atomTag == 'mdia' || atomTag == 'minf' || atomTag == 'stbl') {
		// These atoms contain leaves that may contain stco (or more of these)
		copyAtomToFile(in, out, moovSize);
	} else if (atomTag == 'stco') {
		// Adjust all the chunk offset sizes
		writeUint32BE(out, readUint32BE(in)); // Version, flags
		uint32 chunkCount = readUint32BE(in);
		writeUint32BE(out, chunkCount);
		for (uint32 i = 0; i < chunkCount; i++)
			writeUint32BE(out, readUint32BE(in) + moovSize);
	} else {
		// All other atoms should just be copied verbatim
		copyData(in, out, atomSize - 8);
	}
}

// TODO: Support for having other top-level atoms besides moov and mdat
// (i.e. wide, junk). This would require rewriting a chunk of the below code
// as well as rewriting the stco offset modifying code from above.

int main(int argc, const char **argv) {
	printf("\nQuickTime File Reorderer\n");
	printf("Ensures the moov atom comes befor the mdat atom for easier streaming\n");
	printf("Written by Matthew Hoops (clone2727)\n");
	printf("See license.txt for the license\n\n");

	if (argc < 3) {
		printf("Usage: %s <input> <output>\n", argv[0]);
		return 0;
	}
	
	FILE *videoFile = fopen(argv[1], "rb");
	
	if (!videoFile) {
		printf("Couldn't open file %s\n", argv[1]);
		return 0;
	}
	
	FILE *output = fopen(argv[2], "wb");
	
	if (!output) {
		printf("Could not open file %s for output\n", argv[2]);
		return 0;
	}
	
	// Verify we've got a mdat starting video
	uint32 mdatSize = readUint32BE(videoFile);
	uint32 mdatTag = readUint32BE(videoFile);
	
	if (mdatTag != kMdatTag) {
		if (mdatTag == kMoovTag)
			printf("Video is already in the optimal order!\n");
		else
			printf("Could not detect mdat tag in the data fork!\n");
		return 0;
	}
	
	printf("Seeking to the moov atom... ");
	fseek(videoFile, mdatSize - 8, SEEK_CUR);
	
	uint32 startPos = ftell(videoFile);
	uint32 moovSize = readUint32BE(videoFile);
	uint32 moovTag = readUint32BE(videoFile);
	
	if (moovTag != kMoovTag) {
		printf("No moov atom present!\n");
		return 0;
	}

	writeUint32BE(output, moovSize);
	writeUint32BE(output, moovTag);
	
	printf("Done\nCopying atoms in the moov atom... ");
	while ((uint32)ftell(videoFile) < startPos + moovSize)
		copyAtomToFile(videoFile, output, moovSize);
	printf("Done\n");
	
	printf("Moving back to mdat atom... ");
	fseek(videoFile, 0, SEEK_SET);
	printf("Done\nCopying mdat data... ");
	copyData(videoFile, output, mdatSize);
	printf("Done\n");

	// Shut down!
	fclose(videoFile);
	fflush(output);
	fclose(output);
	
	printf("All Done!\n");
	
	return 0;
}