/* tim2bmp.cpp -- Convert PlayStation TIM images to BMP's
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

// Thanks to http://www.romhacking.net/docs/timgfx.txt for the format information

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

void writeUint32BE(FILE *file, uint32 x) {
	writeUint16BE(file, x >> 16);
	writeUint16BE(file, x & 0xffff);
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

void writeBMPPalette(FILE *output, byte *palette) {
	writeUint32LE(output, 256);
	writeUint32LE(output, 256);
	fwrite(palette, 1, 256 * 4, output);
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

// 15-bit BGR
byte *readTIMPalette(FILE *input, uint16 maxPaletteSize) {
	byte *palette = new byte[256 * 4];
	memset(palette, 0, 256 * 4);

	/* uint32 clutSize = */ readUint32LE(input);
	/* uint16 palOrigX = */ readUint16LE(input);
	/* uint16 palOrigY = */ readUint16LE(input);
	uint16 colorCount = readUint16LE(input);
	uint16 clutCount = readUint16LE(input);

	if (clutCount != 1) {
		printf("Unsupported CLUT count %d\n", clutCount);
		return 0;
	}

	if (colorCount > maxPaletteSize) {
		printf("CLUT color count greater than possible %d > %d\n", colorCount, maxPaletteSize);
		return 0;
	}

	for (uint16 i = 0; i < colorCount; i++) {
		uint16 color = readUint16LE(input);
		palette[i * 4] = isolateBlueChannel(color);
		palette[i * 4 + 1] = isolateGreenChannel(color);
		palette[i * 4 + 2] = isolateRedChannel(color);
	}

	return palette;
}

// 4bpp, paletted
bool convertTIM4ToBMP(FILE *input, FILE *output) {
	byte *palette = readTIMPalette(input, 16);

	if (!palette)
		return false;

	/* uint32 fileSize = */ readUint32LE(input);
	/* uint16 origX = */ readUint16LE(input);
	/* uint16 origY = */ readUint16LE(input);
	uint16 width = readUint16LE(input) * 4;
	uint16 height = readUint16LE(input);

	printf("Width = %d\n", width);
	printf("Height = %d\n", height);

	byte *pixels = new byte[width * height];

	for (uint32 i = 0; i < width * height / 2; i++) {
		byte val = readByte(input);
		pixels[i * 2] = val >> 4;
		pixels[i * 2 + 1] = val & 0xf;
	}

	writeBMPHeader(output, width, height, 8);
	writeBMPPalette(output, palette);

	const uint32 pitch = width;
	const int extraDataLength = (pitch % 4) ? 4 - (pitch % 4) : 0;

	for (int y = height - 1; y >= 0; y--) {
		for (int x = 0; x < width; x++)
			writeByte(output, pixels[x + width * y]);

		for (int i = 0; i < extraDataLength; i++)
			writeByte(output, 0);
	}

	fillBMPHeaderValues(output, 54 + 256 * 4, (pitch + extraDataLength) * height);

	delete[] pixels;
	delete[] palette;
	return true;
}

// 15-bit BGR
bool convertTIM16ToBMP(FILE *input, FILE *output) {
	/* uint32 fileSize = */ readUint32LE(input);
	/* uint16 origX = */ readUint16LE(input);
	/* uint16 origY = */ readUint16LE(input);
	uint16 width = readUint16LE(input);
	uint16 height = readUint16LE(input);

	printf("Width = %d\n", width);
	printf("Height = %d\n", height);

	uint16 *pixels = new uint16[width * height];
	for (uint32 i = 0; i < width * height; i++)
		pixels[i] = readUint16LE(input);

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

// 24-bit BGR
bool convertTIM24ToBMP(FILE *input, FILE *output) {
	/* uint32 fileSize = */ readUint32LE(input);
	/* uint16 origX = */ readUint16LE(input);
	/* uint16 origY = */ readUint16LE(input);
	uint16 width = readUint16LE(input) * 2 / 3;
	uint16 height = readUint16LE(input);

	printf("Width = %d\n", width);
	printf("Height = %d\n", height);

	byte *pixels = new byte[width * height * 3];
	for (uint32 i = 0; i < width * height * 3; i++)
		pixels[i] = readByte(input);

	writeBMPHeader(output, width, height, 24);

	const uint32 pitch = width * 3;
	const int extraDataLength = (pitch % 4) ? 4 - (pitch % 4) : 0;

	for (int y = height - 1; y >= 0; y--) {
		for (int x = 0; x < width; x++) {
			writeByte(output, pixels[y * pitch + x * 3 + 2]);
			writeByte(output, pixels[y * pitch + x * 3 + 1]);
			writeByte(output, pixels[y * pitch + x * 3]);
		}

		for (int i = 0; i < extraDataLength; i++)
			writeByte(output, 0);
	}

	fillBMPHeaderValues(output, 54, (pitch + extraDataLength) * height);

	delete[] pixels;
	return true;
}

bool convertTIMToBMP(FILE *input, FILE *output) {
	uint32 tag = readUint32LE(input);
	uint32 version = readUint32LE(input);

	if (tag != 0x10) {
		printf("TIM tag not found\n");
		return false;
	}

	switch (version) {
		case 8: // 4bpp (with CLUT)
			printf("Found 4bpp (with CLUT) image\n");
			return convertTIM4ToBMP(input, output);
		case 0: // 4bpp (without CLUT)
			printf("Unhandled 4bpp (without CLUT) image\n");
			return false;
		case 9: // 8bpp (with CLUT)
			printf("Unhandled 8bpp (with CLUT) image\n");
			return false;
		case 1: // 8bpp (without CLUT)
			printf("Unhandled 8bpp (without CLUT) image\n");
			return false;
		case 2: // 16bpp
			printf("Found 16bpp TIM image\n");
			return convertTIM16ToBMP(input, output);
		case 3: // 24bpp
			printf("Found 24bpp TIM image\n");
			return convertTIM24ToBMP(input, output);
	}

	printf("Unknown TIM type %d\n", version);
	return false;
}

int main(int argc, const char **argv) {
	printf("\nTIM to BMP Converter\n");
	printf("Converts from PlayStation TIM files to BMP\n");
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

	if (!convertTIMToBMP(input, output))
		return 1;

	fclose(input);
	fflush(output);
	fclose(output);

	printf("\nAll Done!\n");
	return 0;
}
