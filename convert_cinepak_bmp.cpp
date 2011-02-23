/* convert_cinepak_bmp.cpp -- Convert Cinepak encoded BMP images to raw BMP images
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

// The CLIP function is taken from ScummVM (www.scummvm.org)
// The Cinepak code is based on the ScummVM decoder, which in turn is based on the FFmpeg (ffmpeg.org) decoder

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

struct CinepakCodebook {
	byte y[4];
	byte u, v;
};

struct CinepakStrip {
	uint16 id;
	uint16 length;
	uint16 left, top, right, bottom;
	CinepakCodebook v1_codebook[256], v4_codebook[256];
};

struct CinepakFrame {
	byte flags;
	uint32 length;
	uint16 width;
	uint16 height;
	uint16 stripCount;
	CinepakStrip *strips;
	byte *surface;
};

class CinepakDecoder {
public:
	CinepakDecoder();
	~CinepakDecoder();

	uint16 getWidth() { return _curFrame.width; }
	uint16 getHeight() { return _curFrame.height; }
	byte *decodeImage(FILE *input);

private:
	CinepakFrame _curFrame;
	uint32 _y;

	void loadCodebook(FILE *input, uint16 strip, byte codebookType, byte chunkID, uint32 chunkSize);
	void decodeVectors(FILE *input, uint16 strip, byte chunkID, uint32 chunkSize);
};

template<typename T> inline T CLIP (T v, T amin, T amax)
		{ if (v < amin) return amin; else if (v > amax) return amax; else return v; }

// Convert a color from YUV to RGB colorspace, Cinepak style.
inline static void CPYUV2RGB(byte y, byte u, byte v, byte &r, byte &g, byte &b) {
	r = CLIP<int>(y + 2 * (v - 128), 0, 255);
	g = CLIP<int>(y - (u - 128) / 2 - (v - 128), 0, 255);
	b = CLIP<int>(y + 2 * (u - 128), 0, 255);
}

#define PUT_PIXEL(offset, lum, u, v) \
	CPYUV2RGB(lum, u, v, r, g, b); \
	*(_curFrame.surface + offset) = b; \
	*(_curFrame.surface + offset + 1) = g; \
	*(_curFrame.surface + offset + 2) = r

CinepakDecoder::CinepakDecoder() {
	_curFrame.surface = 0;
	_curFrame.strips = 0;
	_y = 0;
}

CinepakDecoder::~CinepakDecoder() {
	delete[] _curFrame.surface;
	delete[] _curFrame.strips;
}

byte *CinepakDecoder::decodeImage(FILE *input) {
	_curFrame.flags = readByte(input);
	_curFrame.length = (readByte(input) << 16) + readUint16BE(input);
	_curFrame.width = readUint16BE(input);
	_curFrame.height = readUint16BE(input);
	_curFrame.stripCount = readUint16BE(input);

	if (!_curFrame.strips)
		_curFrame.strips = new CinepakStrip[_curFrame.stripCount];

	if (!_curFrame.surface)
		_curFrame.surface = new byte[_curFrame.width * _curFrame.height * 3];

	// Reset the y variable.
	_y = 0;

	for (uint16 i = 0; i < _curFrame.stripCount; i++) {
		if (i > 0 && !(_curFrame.flags & 1)) { // Use codebooks from last strip
			for (uint16 j = 0; j < 256; j++) {
				_curFrame.strips[i].v1_codebook[j] = _curFrame.strips[i - 1].v1_codebook[j];
				_curFrame.strips[i].v4_codebook[j] = _curFrame.strips[i - 1].v4_codebook[j];
			}
		}

		_curFrame.strips[i].id = readUint16BE(input);
		_curFrame.strips[i].length = readUint16BE(input) - 12; // Subtract the 12 byte header
		_curFrame.strips[i].top = _y; readUint16BE(input); // Ignore, substitute with our own.
		_curFrame.strips[i].left = 0; readUint16BE(input); // Ignore, substitute with our own
		_curFrame.strips[i].bottom = _y + readUint16BE(input);
		_curFrame.strips[i].right = _curFrame.width; readUint16BE(input); // Ignore, substitute with our own

		uint32 pos = ftell(input);

		while (ftell(input) < (pos + _curFrame.strips[i].length) && !feof(input)) {
			byte chunkID = readByte(input);

			if (feof(input))
				break;

			// Chunk Size is 24-bit, ignore the first 4 bytes
			uint32 chunkSize = readByte(input) << 16;
			chunkSize += readUint16BE(input) - 4;

			uint32 startPos = ftell(input);

			switch (chunkID) {
			case 0x20:
			case 0x21:
			case 0x24:
			case 0x25:
				loadCodebook(input, i, 4, chunkID, chunkSize);
				break;
			case 0x22:
			case 0x23:
			case 0x26:
			case 0x27:
				loadCodebook(input, i, 1, chunkID, chunkSize);
				break;
			case 0x30:
			case 0x31:
			case 0x32:
				decodeVectors(input, i, chunkID, chunkSize);
				break;
			default:
				printf("Unknown Cinepak chunk ID %02x\n", chunkID);
				return _curFrame.surface;
			}

			if (ftell(input) != startPos + chunkSize)
				fseek(input, startPos + chunkSize, SEEK_SET);
		}

		_y = _curFrame.strips[i].bottom;
	}

	return _curFrame.surface;
}

void CinepakDecoder::loadCodebook(FILE *input, uint16 strip, byte codebookType, byte chunkID, uint32 chunkSize) {
	CinepakCodebook *codebook = (codebookType == 1) ? _curFrame.strips[strip].v1_codebook : _curFrame.strips[strip].v4_codebook;

	uint32 startPos = ftell(input);
	uint32 flag = 0, mask = 0;

	for (uint16 i = 0; i < 256; i++) {
		if ((chunkID & 0x01) && !(mask >>= 1)) {
			if ((ftell(input) - startPos + 4) > chunkSize)
				break;

			flag  = readUint32BE(input);
			mask  = 0x80000000;
		}

		if (!(chunkID & 0x01) || (flag & mask)) {
			byte n = (chunkID & 0x04) ? 4 : 6;
			if ((ftell(input) - startPos + n) > chunkSize)
				break;

			for (byte j = 0; j < 4; j++)
				codebook[i].y[j] = readByte(input);

			if (n == 6) {
				codebook[i].u  = readByte(input) + 128;
				codebook[i].v  = readByte(input) + 128;
			} else {
				// This codebook type indicates either greyscale or
				// palettized video. We don't handle palettized video
				// currently.
				codebook[i].u  = 128;
				codebook[i].v  = 128;
			}
		}
	}
}

void CinepakDecoder::decodeVectors(FILE *input, uint16 strip, byte chunkID, uint32 chunkSize) {
	uint32 flag = 0, mask = 0;
	uint32 iy[4];
	uint32 startPos = ftell(input);
	byte r = 0, g = 0, b = 0;

	for (uint16 y = _curFrame.strips[strip].top; y < _curFrame.strips[strip].bottom; y += 4) {
		iy[0] = (_curFrame.strips[strip].left + y * _curFrame.width) * 3;
		iy[1] = iy[0] + _curFrame.width * 3;
		iy[2] = iy[1] + _curFrame.width * 3;
		iy[3] = iy[2] + _curFrame.width * 3;

		for (uint16 x = _curFrame.strips[strip].left; x < _curFrame.strips[strip].right; x += 4) {
			if ((chunkID & 0x01) && !(mask >>= 1)) {
				if ((ftell(input) - startPos + 4) > chunkSize)
					return;

				flag  = readUint32BE(input);
				mask  = 0x80000000;
			}

			if (!(chunkID & 0x01) || (flag & mask)) {
				if (!(chunkID & 0x02) && !(mask >>= 1)) {
					if ((ftell(input) - startPos + 4) > chunkSize)
						return;

					flag  = readUint32BE(input);
					mask  = 0x80000000;
				}

				if ((chunkID & 0x02) || (~flag & mask)) {
					if ((ftell(input) - startPos + 1) > chunkSize)
						return;

					// Get the codebook
					CinepakCodebook codebook = _curFrame.strips[strip].v1_codebook[readByte(input)];

					PUT_PIXEL(iy[0] + 0 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[0] + 1 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 0 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 1 * 3, codebook.y[0], codebook.u, codebook.v);

					PUT_PIXEL(iy[0] + 2 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[0] + 3 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 2 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 3 * 3, codebook.y[1], codebook.u, codebook.v);

					PUT_PIXEL(iy[2] + 0 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[2] + 1 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 0 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 1 * 3, codebook.y[2], codebook.u, codebook.v);

					PUT_PIXEL(iy[2] + 2 * 3, codebook.y[3], codebook.u, codebook.v);
					PUT_PIXEL(iy[2] + 3 * 3, codebook.y[3], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 2 * 3, codebook.y[3], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 3 * 3, codebook.y[3], codebook.u, codebook.v);
				} else if (flag & mask) {
					if ((ftell(input) - startPos + 4) > chunkSize)
						return;

					CinepakCodebook codebook = _curFrame.strips[strip].v4_codebook[readByte(input)];
					PUT_PIXEL(iy[0] + 0 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[0] + 1 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 0 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 1 * 3, codebook.y[3], codebook.u, codebook.v);

					codebook = _curFrame.strips[strip].v4_codebook[readByte(input)];
					PUT_PIXEL(iy[0] + 2 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[0] + 3 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 2 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[1] + 3 * 3, codebook.y[3], codebook.u, codebook.v);

					codebook = _curFrame.strips[strip].v4_codebook[readByte(input)];
					PUT_PIXEL(iy[2] + 0 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[2] + 1 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 0 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 1 * 3, codebook.y[3], codebook.u, codebook.v);

					codebook = _curFrame.strips[strip].v4_codebook[readByte(input)];
					PUT_PIXEL(iy[2] + 2 * 3, codebook.y[0], codebook.u, codebook.v);
					PUT_PIXEL(iy[2] + 3 * 3, codebook.y[1], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 2 * 3, codebook.y[2], codebook.u, codebook.v);
					PUT_PIXEL(iy[3] + 3 * 3, codebook.y[3], codebook.u, codebook.v);
				}
			}

			for (byte i = 0; i < 4; i++)
				iy[i] += 4 * 3;
		}
	}
}


bool extractImageToBMP(FILE *input, FILE *output) {
	uint16 tag = readUint16BE(input);

	if (tag != 'BM') {
		printf("Not a valid bitmap image\n");
		return false;
	}

	readUint32LE(input);
	readUint16LE(input);
	readUint16LE(input);
	uint32 imageOffset = readUint32LE(input);

	// Now onto the info header

	if (readUint32LE(input) != 40) {
		printf("Not a Windows v3 bitmap\n");
		return false;
	}

	readUint32LE(input);
	readUint32LE(input);
	readUint16LE(input);
	readUint16LE(input);

	if (readUint32BE(input) != 'cvid') {
		printf("Not a Cinepak bitmap\n");
		return false;
	}

	fseek(input, imageOffset, SEEK_SET);

	CinepakDecoder *cinepak = new CinepakDecoder();
	byte *pixels = cinepak->decodeImage(input);
	uint16 width = cinepak->getWidth();
	uint16 height = cinepak->getHeight();

	writeBMPHeader(output, width, height, 24);

	const uint32 pitch = width * 3;
	const int extraDataLength = (pitch % 4) ? 4 - (pitch % 4) : 0;

	for (int y = height - 1; y >= 0; y--) {
		byte *row = pixels + width * y * 3;
		for (int x = 0; x < width; x++) {
			writeByte(output, *row++);
			writeByte(output, *row++);
			writeByte(output, *row++);
		}

		for (int i = 0; i < extraDataLength; i++)
			writeByte(output, 0);
	}

	fillBMPHeaderValues(output, 54, (pitch + extraDataLength) * height);

	delete cinepak;
	return true;
}

int main(int argc, const char **argv) {
	printf("\nCinepak BMP to Raw BMP Converter\n");
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
		printf("Could not open '%s' for writing\n", argv[2]);
		fclose(input);
		return 1;
	}

	if (!extractImageToBMP(input, output))
		return 1;

	fclose(input);
	fflush(output);
	fclose(output);

	printf("\nAll Done!\n");
	return 0;
}
