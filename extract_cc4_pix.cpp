/* extract_cc4_pix.cpp -- Extract CC4/CC5 pictures from PIX files
 * Copyright (c) 2010 Matthew Hoops (clone2727)
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

// Thanks to http://wiki.xentax.com/index.php/Close_Combat_4_PIX for the format information

#include <cstdio>
#include <cstring>

// Standard types
typedef unsigned char byte;
typedef unsigned short uint16;
typedef unsigned int uint32;

// Helper functions for reading integers from the stream (maintaining endianness)
byte readByte(FILE *file) {
	byte b = 0;
	fread(&b, 1, 1, file);
	return b;
}

uint16 readUint16LE(FILE *file) {
	uint16 x = readByte(file);
	return x | readByte(file) << 8;
}

uint32 readUint32LE(FILE *file) {
	uint16 x = readUint16LE(file);
	return x | readUint16LE(file) << 16;
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

void writeUint16LE(FILE *file, uint16 x) {
	writeByte(file, x & 0xff);
	writeByte(file, x >> 8);
}

void writeUint32LE(FILE *file, uint32 x) {
	writeUint16LE(file, x & 0xffff);
	writeUint16LE(file, x >> 16);
}

void writeUint16BE(FILE *file, uint16 x) {
	writeByte(file, x >> 8);
	writeByte(file, x & 0xff);
}

uint32 getFileSize(FILE *file) {
	uint32 pos = ftell(file);
	fseek(file, 0, SEEK_END);
	uint32 size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

void writeBMPHeader(FILE *output, uint16 width, uint16 height, uint16 bitsPerPixel) {
	// Main Header
	writeUint16BE(output, 'BM');
	writeUint32LE(output, 0); // Size, will fill in later
	writeUint16LE(output, 0); // Reserved
	writeUint16LE(output, 0); // Reserved
	writeUint32LE(output, 0); // Image offset, will fill in later

	// Info Header
	writeUint32LE(output, 40);
	writeUint32LE(output, width);
	writeUint32LE(output, height);
	writeUint16LE(output, 1);
	writeUint16LE(output, bitsPerPixel);
	writeUint32LE(output, 0);
	writeUint32LE(output, 0); // Image size, will fill in later
	writeUint32LE(output, 72); // 72 dpi sounds fine to me
	writeUint32LE(output, 72); // as above

	// Only write the empty palette if we're not in paletted mode. The
	// palette header will be written in writeBMPPalette() otherwise.
	if (bitsPerPixel > 8) {
		writeUint32LE(output, 0);
		writeUint32LE(output, 0);
	}
}

void fillBMPHeaderValues(FILE *output, uint32 imageOffset, uint32 imageSize) {
	fflush(output);
	uint32 fileSize = getFileSize(output);

	fseek(output, 2, SEEK_SET);
	fflush(output);
	writeUint32LE(output, fileSize);
	fflush(output);

	fseek(output, 10, SEEK_SET);
	fflush(output);
	writeUint32LE(output, imageOffset);
	fflush(output);

	fseek(output, 34, SEEK_SET);
	fflush(output);
	writeUint32LE(output, imageSize);
}

struct PicEntry {
	char filename[32];
	uint32 width;
	uint32 height;
	uint32 length;
	uint32 offset;
};

// Functions to isolate the color channels and blow them up to 8-bit values

inline byte isolateRedChannel(uint16 color) {
	return (color & 0x7c00) >> 7;
}

inline byte isolateGreenChannel(uint16 color) {
	return (color & 0x3e0) >> 2;
}

inline byte isolateBlueChannel(uint16 color) {
	return (color & 0x1f) << 3;
}


// NOTE: Original format is rgb555
bool extractImageToBMP(FILE *input, FILE *output, PicEntry &entry) {
	fseek(input, entry.offset, SEEK_SET);

	printf("Width = %d\n", entry.width);
	printf("Height = %d\n", entry.height);

	if (entry.width * entry.height * 2 != entry.length) {
		printf("Image entry has bad length %08x, %08x\n", entry.length, entry.offset);
		return false;
	}

	uint16 *pixels = new uint16[entry.width * entry.height];
	for (uint32 i = 0; i < entry.width * entry.height; i++)
		pixels[i] = readUint16LE(input);

	writeBMPHeader(output, entry.width, entry.height, 24);

	const uint32 pitch = entry.width * 3;
	const int extraDataLength = (pitch % 4) ? 4 - (pitch % 4) : 0;

	for (int y = entry.height - 1; y >= 0; y--) {
		for (int x = 0; x < entry.width; x++) {
			uint16 color = pixels[x + entry.width * y];
			writeByte(output, isolateBlueChannel(color));
			writeByte(output, isolateGreenChannel(color));
			writeByte(output, isolateRedChannel(color));
		}

		for (int i = 0; i < extraDataLength; i++)
			writeByte(output, 0);
	}

	fillBMPHeaderValues(output, 54, (pitch + extraDataLength) * entry.height);

	delete[] pixels;
	return true;
}

bool extractAllFiles(FILE *input) {
	uint32 tag = readUint32BE(input);
	uint32 version = readUint32LE(input);

	if (tag != 'PICS') {
		printf("PICS tag not found\n");
		return false;
	}

	if (version != 1) {
		printf("Unknown version %d", version);
		return false;
	}


	uint32 fileCount = readUint32LE(input);
	PicEntry *entries = new PicEntry[fileCount];

	for (uint32 i = 0; i < fileCount; i++) {
		fread(entries[i].filename, 1, 32, input);
		entries[i].width = readUint32LE(input);
		entries[i].height = readUint32LE(input);
		entries[i].length = readUint32LE(input);
		entries[i].offset = readUint32LE(input);
	}

	bool allDone = true;

	for (uint32 i = 0; i < fileCount; i++) {
		char *filename = new char[strlen(entries[i].filename) + 5];
		memset(filename, 0, strlen(entries[i].filename) + 5);
		strcpy(filename, entries[i].filename);
		strcat(filename, ".bmp");

		FILE *output = fopen(filename, "wb+");
		if (!output) {
			printf("Could not open '%s' for writing\n", filename);
			allDone = false;
			delete[] filename;
			break;
		}

		printf("Extracting %s\n", filename);

		if (!extractImageToBMP(input, output, entries[i])) {
			allDone = false;
			delete[] filename;
			break;
		}

		printf("\n");

		fflush(output);
		fclose(output);
		delete[] filename;
	}

	delete[] entries;
	return allDone;
}

int main(int argc, const char **argv) {
	printf("\nCC4/CC5 PIX Image Extractor\n");
	printf("Converts files from CC4/CC5 PIX files to BMP\n");
	printf("Written by Matthew Hoops (clone2727)\n");
	printf("See license.txt for the license\n\n");

	if (argc < 2) {
		printf("Usage: %s <input>\n", argv[0]);
		return 0;
	}

	FILE *input = fopen(argv[1], "rb");
	if (!input) {
		printf("Could not open '%s' for reading\n", argv[1]);
		return 1;
	}

	if (!extractAllFiles(input))
		return 1;

	fclose(input);

	printf("All Done!\n");
	return 0;
}
