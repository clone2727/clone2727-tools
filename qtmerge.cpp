/* qtmerge.cpp -- Merge QuickTime videos that are split between the resource and data forks
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
	kMoovOffset = 0x104,
	kBufSize = 1024 * 1024, // 1MB sounds good to me
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

uint32 fileSize(FILE *file) {
	uint32 pos = ftell(file);
	fseek(file, 0, SEEK_END);
	uint32 size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

// Helper macros/functions for opening the data/resource forks
#define OPEN_DATA_FORK(x) \
	fopen(x, "rb")

#ifdef __APPLE__
FILE *OPEN_RESOURCE_FORK(const char *filename) {
	// Mac OS X allows access of the resource fork using this
	// crazy extension. Probably leftover from the pre-Mac OS X
	// days like rest of HFS...
	static const char *resExtension = "/..namedfork/rsrc";
	
	// Go and create the filename
	int length = strlen(filename) + strlen(resExtension) + 2;
	char *resFilename = new char[length];
	memset(resFilename, 0, length);
	sprintf(resFilename, "%s%s", filename, resExtension);
	
	FILE *resFile = fopen(resFilename, "rb");
	delete[] resFilename;
	return resFile;
}
#else
#error "Non-Mac OS X systems not supported yet!"
#endif

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

int main(int argc, const char **argv) {
	printf("\nQuickTime File Merger\n");
	printf("Merges QuickTime files that store moov chunks in Mac resource forks\n");
	printf("Written by Matthew Hoops (clone2727)\n");
	printf("See license.txt for the license\n\n");

	if (argc < 3) {
		printf("Usage: %s <input> <output>\n", argv[0]);
		return 0;
	}

	if (sizeof(byte) != 1 || sizeof(uint16) != 2 || sizeof(uint32) != 4) {
		printf("Your system has different integer sizes than the standard! Cannot continue!\n");
		return 0;
	}
	
	FILE *dataFork = OPEN_DATA_FORK(argv[1]);
	if (!dataFork) {
		printf("Couldn't open file %s\n", argv[1]);
		return 0;
	} else
		printf("Have the data fork\n");

	
	FILE *resFork = OPEN_RESOURCE_FORK(argv[1]);
	if (!resFork) {
		printf("Couldn't open resource fork of %s\n", argv[1]);
		return 0;
	}
	
	FILE *output = fopen(argv[2], "wb");
	if (!output) {
		printf("Could not open file %s for output\n", argv[2]);
		return 0;
	}
	
	// Verify we've got a mdat starting video
	uint32 mdatSize = readUint32BE(dataFork);
	uint32 mdatTag = readUint32BE(dataFork);
	
	if (mdatTag != kMdatTag) {
		printf("Could not detect mdat tag in the data fork!\n");
		return 0;
	}

	// WORKAROUND: Some QuickTime movies have a 0 mdat size...
	if (mdatSize == 0)
		mdatSize = fileSize(dataFork);
	
	// Copy the mdat section to the output
	printf("Copying mdat section from the data fork... ");
	writeUint32BE(output, mdatSize);
	writeUint32BE(output, mdatTag);
	copyData(dataFork, output, mdatSize - 8);
	printf("Done\n");
	
	// Seek to the moov data in the resource fork and get the size
	// NOTE: This is a hack. I should be using the offsets in the resource fork
	// itself than just using this present offset.
	fseek(resFork, kMoovOffset, SEEK_SET);
	uint32 moovSize = readUint32BE(resFork);
	uint32 moovTag = readUint32BE(resFork);
	
	// Verify we're in the moov section
	if (moovTag != kMoovTag) {
		printf("Could not detect moov tag in the resource fork!\n");
		return 0;
	}
	
	// Copy the moov section to the output
	printf("Copying moov section from the resource fork... ");
	writeUint32BE(output, moovSize);
	writeUint32BE(output, moovTag);
	copyData(resFork, output, moovSize - 8);
	printf("Done\n");

	// Shut down!
	fclose(dataFork);
	fclose(resFork);
	fflush(output);
	fclose(output);
	
	printf("All Done!\n");
	return 0;
}
