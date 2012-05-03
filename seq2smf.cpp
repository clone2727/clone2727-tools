/* seq2smf.cpp -- Convert PlayStation SEQ MIDI files to SMF's
 * Copyright (c) 2012 Matthew Hoops (clone2727)
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

#include <stdio.h>

// Standard types
typedef unsigned char byte;
typedef unsigned short uint16;
typedef unsigned int uint32;

// Helper functions for reading integers from the stream (maintaining endianness)
byte readByte(FILE *input) {
	byte b;
	fread(&b, 1, 1, input);
	return b;
}

uint16 readUint16LE(FILE *input) {
	uint16 x = readByte(input);
	return x | (readByte(input) << 8);
}

uint32 readUint32LE(FILE *input) {
	uint32 x = readUint16LE(input);
	return x | (readUint16LE(input) << 16);
}

uint16 readUint16BE(FILE *input) {
	uint16 x = readByte(input) << 8;
	return x | readByte(input);
}

uint32 readUint24BE(FILE *input) {
	uint32 x = readUint16BE(input) << 8;
	return x | readByte(input);
}

uint32 readUint32BE(FILE *input) {
	uint32 x = readUint16BE(input) << 16;
	return x | readUint16BE(input);
}

void writeByte(FILE *output, byte b) {
	fwrite(&b, 1, 1, output);
}

void writeUint16BE(FILE *output, uint16 x) {
	writeByte(output, x >> 8);
	writeByte(output, x & 0xff);
}

void writeUint24BE(FILE *output, uint32 x) {
	writeUint16BE(output, x >> 8);
	writeByte(output, x & 0xff);
}

void writeUint32BE(FILE *output, uint32 x) {
	writeUint16BE(output, x >> 16);
	writeUint16BE(output, x & 0xffff);
}

uint32 getFileSize(FILE *input) {
	uint32 startPos = ftell(input);
	fseek(input, 0, SEEK_END);
	uint32 size = ftell(input);
	fseek(input, startPos, SEEK_SET);
	return size;
}

#define MKTAG(a0, a1, a2, a3) ((uint32)((a3) | ((a2) << 8) | ((a1) << 16) | ((a0) << 24)))

int convertToSMF(FILE *input, FILE *output) {
	if (readUint32LE(input) != MKTAG('S', 'E', 'Q', 'p')) {
		fprintf(stderr, "Not a valid PSX SEQ\n");
		return 1;
	}

	if (readUint32BE(input) != 1) {
		fprintf(stderr, "SEP files not handled yet!\n");
		return 2;
	}

	uint16 ppqn = readUint16BE(input);
	uint32 tempo = readUint24BE(input);
	/* uint16 beat = */ readUint16BE(input); // Not sure what to do with this yet!

	uint32 seqDataSize = getFileSize(input) - 15;
	byte *seqData = new byte[seqDataSize];
	fread(seqData, 1, seqDataSize, input);

	// We parsed the data and now it's time to generate the SMF header
	writeUint32BE(output, MKTAG('M', 'T', 'h', 'd'));
	writeUint32BE(output, 6);
	writeUint32BE(output, 1);
	writeUint16BE(output, ppqn);
	writeUint32BE(output, MKTAG('M', 'T', 'r', 'k'));
	writeUint32BE(output, seqDataSize + 7);

	// Fake a tempo change event
	writeByte(output, 0x00);
	writeByte(output, 0xFF);
	writeByte(output, 0x51);
	writeByte(output, 0x03);
	writeUint24BE(output, tempo);

	// Now, finally, add all the SEQ data
	fwrite(seqData, 1, seqDataSize, output);

	delete[] seqData;
	return 0;
}

int main(int argc, const char **argv) {
	if (argc < 3) {
		printf("Usage: %s <seq file input> <mid file output>\n", argv[0]);
		return 0;
	}

	FILE *input = fopen(argv[1], "rb");
	if (!input) {
		fprintf(stderr, "Could not open '%s' for reading\n", argv[1]);
		return 1;
	}

	FILE *output = fopen(argv[2], "wb");
	if (!output) {
		fprintf(stderr, "Could not open '%s' for writing\n", argv[2]);
		return 1;
	}

	int result = convertToSMF(input, output);

	if (result != 0) {
		fprintf(stderr, "Failed to extract!\n");
		return result;
	}

	fclose(input);
	fflush(output);
	fclose(output);

	printf("All complete!\n");

	return 0;
}
