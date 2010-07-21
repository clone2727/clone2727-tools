/* extract_cc3_sfx.cpp -- Extract CC3/CC4/CC5 sounds from SFX files
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

// Thanks to http://wiki.xentax.com/index.php/Close_Combat_SFX for the format information
// Highly modified from what the specs say...

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

struct SoundEntry {
	uint32 length;
	uint32 offset;
	uint16 unk1; // signedness?
	uint16 unk2;
	uint32 unkRate;
	uint32 byteRate;
	uint16 channels;
	uint16 bitsPerSample;
	uint32 unk3;
};

bool extractSoundToWave(FILE *input, FILE *output, SoundEntry &entry) {
	if (entry.unk1 != 1) {
		// Possibly a signed flag?
		// Compression flag (ie. 1 = PCM from the WAVE format)?
		printf("unk1 = %d\n", entry.unk1);
		return false;
	}

	if (entry.unk1 != 1 && entry.unk2 != 2) {
		// This seems to not have an effect...
		// channels/2?
		printf("unk2 = %d\n", entry.unk2);
		return false;
	}

	if (entry.unkRate != 22050) {
		// Sound.sfx of CC4 has a bunch of 8000 and 44100 ones.
		// The sounds still extract properly, this probably just
		// isn't used.
		//printf("unkRate = %d\n", entry.unkRate);
		//return false;
	}

	if (entry.bitsPerSample != 16)
		printf("Untested bitsPerSample %d\n", entry.bitsPerSample);

	fseek(input, entry.offset, SEEK_SET);

	byte *data = new byte[entry.length];
	fread(data, 1, entry.length, input);

	writeUint32BE(output, 'RIFF');
	writeUint32LE(output, entry.length + 44);
	writeUint32BE(output, 'WAVE');
	writeUint32BE(output, 'fmt ');
	writeUint32LE(output, 16);
	writeUint16LE(output, 1);
	writeUint16LE(output, entry.channels);
	writeUint32LE(output, entry.byteRate / entry.channels / (entry.bitsPerSample >> 3));
	writeUint32LE(output, entry.byteRate);
	writeUint16LE(output, entry.channels * (entry.bitsPerSample >> 3));
	writeUint16LE(output, entry.bitsPerSample);
	writeUint32BE(output, 'data');
	writeUint32LE(output, entry.length);

	fwrite(data, 1, entry.length, output);

	delete[] data;
	return true;
}

bool extractAllFiles(FILE *input) {
	uint32 fileCount = readUint32LE(input);
	uint32 unk0 = readUint32LE(input);
	readUint32LE(input); // Always 0
	readUint32LE(input); // Always 0
	

	if (unk0 != 99) {
		printf("Second SFX field is not 99\n");
		return false;
	}

	SoundEntry *entries = new SoundEntry[fileCount];

	for (uint32 i = 0; i < fileCount; i++) {
		entries[i].length = readUint32LE(input);
		entries[i].offset = readUint32LE(input);
		entries[i].unk1 = readUint16LE(input);
		entries[i].unk2 = readUint16LE(input);
		entries[i].unkRate = readUint32LE(input);
		entries[i].byteRate = readUint32LE(input);
		entries[i].channels = readUint16LE(input);
		entries[i].bitsPerSample = readUint16LE(input);
		entries[i].unk3 = readUint32LE(input);
	}

	bool allDone = true;

	for (uint32 i = 0; i < fileCount; i++) {
		static char filename[32];
		memset(filename, 0, sizeof(filename));
		sprintf(filename, "%d.wav", i);

		FILE *output = fopen(filename, "wb");
		if (!output) {
			printf("Could not open '%s' for writing\n", filename);
			allDone = false;
			break;
		}

		printf("Extracting %s...\n", filename);

		if (!extractSoundToWave(input, output, entries[i])) {
			allDone = false;
			break;
		}

		fflush(output);
		fclose(output);
	}

	delete[] entries;
	return allDone;
}

int main(int argc, const char **argv) {
	printf("\nCC3/CC4/CC5 SFX Sound Extractor\n");
	printf("Converts files from CC3/CC4/CC5 SFX files to WAVE\n");
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
