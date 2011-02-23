/* dg22bmp.cpp -- Convert D (A Sega Saturn game) raw images to BMP's
 * Copyright (c) 2010-2011 Matthew Hoops (clone2727)
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

// Thanks to http://multimedia.cx/eggs/brute-force-dimensional-analysis for basic information/samples

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

uint16 readUint16BE(FILE *file) {
	uint16 x = readByte(file) << 8;
	return x | readByte(file);
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

// Functions to isolate the color channels and blow them up to 8-bit values

inline byte isolateRedChannel(uint16 color) {
	return (color & 0x1f) << 3;
}

inline byte isolateGreenChannel(uint16 color) {
	return (color & 0x3e0) >> 2;
}

inline byte isolateBlueChannel(uint16 color) {
	return (color & 0x7c00) >> 7;
}

// A function for just listing all the factors of a number
// Could be optimized...
void listAllFactors(uint32 x) {
	printf("Factors:\n");
	printf("(1, %d)\n", x);
	for (uint32 i = 2; i <= x; i++)
		if (x % i == 0)
			printf("(%d, %d)\n", i, x / i);
}

bool convertDG2ToBMP(FILE *input, FILE *output) {
	uint32 fileSize = getFileSize(input);
	uint16 width = 0, height = 0;

	// Just remap the file size to the width/height
	// Should be easy enough to add other sizes
	switch (fileSize) {
		case 3840:
			width = 120;
			height = 16;
			break;
		case 4320:
			width = 24;
			height = 90;
			break;
		case 20736:
			width = 216;
			height = 48;
			break;
		case 21632:
			width = 104;
			height = 104;
			break;
		case 25600:
			width = 160;
			height = 80;
			break;
		case 32768:
			width = 128;
			height = 128;
			break;
		case 82944:
			width = 288;
			height = 144;
			break;
	}

	if (width == 0 || height == 0) {
		printf("Not a valid DG2 image!\n");
		return false;
	}

	printf("Width = %d\n", width);
	printf("Height = %d\n", height);

	uint16 *pixels = new uint16[width * height];
	for (uint32 i = 0; i < width * height; i++)
		pixels[i] = readUint16BE(input);

	writeBMPHeader(output, width, height, 24);

	const uint32 pitch = width * 3;
	const int extraDataLength = (pitch % 4) ? 4 - (pitch % 4) : 0;

	for (int y = height - 1; y >= 0; y--) {
		for (int x = 0; x < width; x++) {
			uint16 color = pixels[x + width * y];
			writeByte(output, isolateBlueChannel(color));
			writeByte(output, isolateGreenChannel(color));
			writeByte(output, isolateRedChannel(color));
		}

		for (int i = 0; i < extraDataLength; i++)
			writeByte(output, 0);
	}

	fillBMPHeaderValues(output, 54, (pitch + extraDataLength) * height);

	delete[] pixels;
	return true;
}

int main(int argc, const char **argv) {
	printf("\nDG2 to BMP Converter\n");
	printf("Converts from D (A Sega Saturn game) DG2 images to BMP\n");
	printf("Written by Matthew Hoops (clone2727)\n");
	printf("See license.txt for the license\n\n");

	if (argc < 3) {
		printf("Usage: %s <input> <output>\n", argv[0]);
		return 0;
	}

	FILE *input = fopen(argv[1], "rb");
	if (!input) {
		printf("Could not open '%s' for reading\n", argv[1]);
		return 1;
	}

	FILE *output = fopen(argv[2], "wb+");
	if (!output) {
		fclose(input);
		printf("Could not open '%s' for writing\n", argv[2]);
		return 1;
	}

	if (!convertDG2ToBMP(input, output))
		return 1;

	fclose(input);
	fflush(output);
	fclose(output);

	printf("\nAll Done!\n");
	return 0;
}
