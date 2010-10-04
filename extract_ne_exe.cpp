/* extract_ne_exe.cpp -- Extract resources from NE executables
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

// This is based on DrMcCoy's Dark Seed II NE parser (github.com/DrMcCoy/scummvm-darkseed2)
// But several portions were rewritten by me (and will be put into his tree eventually)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

uint16 READ_LE_UINT16(byte *data) {
	return (*(data + 1) << 8) | *data;
}

uint32 getFileSize(FILE *file) {
	uint32 pos = ftell(file);
	fseek(file, 0, SEEK_END);
	uint32 size = ftell(file);
	fseek(file, pos, SEEK_SET);
	return size;
}

class NEResourceID {
public:
	NEResourceID() { _idType = kIDTypeNull; }
	NEResourceID(std::string x) { _idType = kIDTypeString; _name = x; }
	NEResourceID(uint16 x) { _idType = kIDTypeNumerical; _id = x; }

	NEResourceID &operator=(std::string string);
	NEResourceID &operator=(uint16 x);

	bool operator==(const std::string &x) const;
	bool operator==(const uint16 &x) const;
	bool operator==(const NEResourceID &x) const;

	std::string getString() const;
	uint16 getID() const;
	std::string toString(std::string extension = "") const;

private:
	/** An ID Type. */
	enum IDType {
		kIDTypeNull, // No type set
		kIDTypeNumerical, ///< A numerical ID.
		kIDTypeString     ///< A string ID.
	} _idType;

	std::string _name; ///< The resource's string ID.
	uint16 _id;           ///< The resource's numerical ID.
};

NEResourceID &NEResourceID::operator=(std::string string) {
	_name = string;
	_idType = kIDTypeString;
	return *this;
}

NEResourceID &NEResourceID::operator=(uint16 x) {
	_id = x;
	_idType = kIDTypeNumerical;
	return *this;
}

bool NEResourceID::operator==(const std::string &x) const {
	return _idType == kIDTypeString && _name.compare(x) == 0;
}

bool NEResourceID::operator==(const uint16 &x) const {
	return _idType == kIDTypeNumerical && _id == x;
}

bool NEResourceID::operator==(const NEResourceID &x) const {
	if (_idType != x._idType)
		return false;
	if (_idType == kIDTypeString)
		return _name.compare(x._name) == 0;
	if (_idType == kIDTypeNumerical)
		return _id == x._id;
	return true;
}

std::string NEResourceID::getString() const {
	if (_idType != kIDTypeString)
		return "";

	return _name;
}

uint16 NEResourceID::getID() const {
	if (_idType != kIDTypeNumerical)
		return 0xffff;

	return _idType;
}

std::string NEResourceID::toString(std::string extension) const {
	if (_idType == kIDTypeString) {
		std::string name = _name;
		name += extension;
		return name;
	} else if (_idType == kIDTypeNumerical) {
		std::string str;
		str.resize(9);
		sprintf(&str[0], "%04x%s", _id, extension.c_str()); 
		return str;
	}

	return "";
}

enum NEResourceType {
	kNECursor = 0x8001,
	kNEBitmap = 0x8002,
	kNEIcon = 0x8003,
	kNEMenu = 0x8004,
	kNEDialog = 0x8005,
	kNEString = 0x8006,
	kNEFontDir = 0x8007,
	kNEFont = 0x8008,
	kNEAccelerator = 0x8009,
	kNERCData = 0x800A,
	kNEMessageTable = 0x800B,
	kNEGroupCursor = 0x800C,
	kNEGroupIcon = 0x800D,
	kNEVersion = 0x8010,
	kNEDlgInclude = 0x8011,
	kNEPlugPlay = 0x8013,
	kNEVXD = 0x8014,
	kNEAniCursor = 0x8015,
	kNEAniIcon = 0x8016,
	kNEHTML = 0x8017,
	kNEManifest = 0x8018
};

struct DataSet {
	DataSet() { data = 0; size = 0; }
	~DataSet() { delete[] data; }
	byte *data;
	uint32 size;
};

/** A class able to load resources from a New Executable. */
class NEResources {
public:
	NEResources();
	~NEResources();

	/** Clear all information. */
	void clear();

	/** Load from an EXE file. */
	bool loadFromEXE(FILE *exe);

	std::vector<NEResourceID> getTypeList(uint16 type);

	DataSet *getResource(uint16 type, NEResourceID id);

private:
	/** A resource. */
	struct Resource {
		NEResourceID id;

		uint16 type; ///< Type of the resource.

		uint32 offset; ///< Offset within the EXE.
		uint32 size;   ///< Size of the data.

		uint16 flags;
		uint16 handle;
		uint16 usage;
	};

	FILE *_exe;        ///< Current file.

	/** All resources. */
	std::vector<Resource> _resources;

	/** Read the offset to the resource table. */
	uint32 getResourceTableOffset();
	/** Read the resource table. */
	bool readResourceTable(uint32 offset);

	/** Find a specific resource. */
	const Resource *findResource(uint16 type, NEResourceID id) const;

	/** Read a resource string. */
	std::string getResourceString(uint32 offset);
};

NEResources::NEResources() {
	_exe = 0;
}

NEResources::~NEResources() {
	clear();
}

void NEResources::clear() {
	_resources.clear();
}

bool NEResources::loadFromEXE(FILE *exe) {
	clear();

	_exe = exe;

	uint32 offsetResourceTable = getResourceTableOffset();
	if (offsetResourceTable == 0xFFFFFFFF)
		return false;
	if (offsetResourceTable == 0)
		return true;

	if (!readResourceTable(offsetResourceTable))
		return false;

	return true;
}

uint32 NEResources::getResourceTableOffset() {
	if (!_exe)
		return 0xFFFFFFFF;

	fseek(_exe, 0, SEEK_SET);

	//                          'MZ'
	if (readUint16BE(_exe) != 'MZ')
		return 0xFFFFFFFF;

	fseek(_exe, 60, SEEK_SET);

	uint32 offsetSegmentEXE = readUint16LE(_exe);

	fseek(_exe, offsetSegmentEXE, SEEK_SET);

	//                          'NE'
	if (readUint16BE(_exe) != 'NE')
		return 0xFFFFFFFF;

	fseek(_exe, offsetSegmentEXE + 36, SEEK_SET);

	uint32 offsetResourceTable = readUint16LE(_exe);
	if (offsetResourceTable == 0)
		// No resource table
		return 0;

	// Offset relative to the segment _exe header
	offsetResourceTable += offsetSegmentEXE;

	fseek(_exe, offsetResourceTable, SEEK_SET);

	return offsetResourceTable;
}

static const char *s_resTypeNames[] = {
	"", "cursor", "bitmap", "icon", "menu", "dialog", "string",
	"font_dir", "font", "accelerator", "rc_data", "msg_table",
	"group_cursor", "group_icon", "version", "dlg_include",
	"plug_play", "vxd", "ani_cursor", "ani_icon", "html",
	"manifest"
};

bool NEResources::readResourceTable(uint32 offset) {
	if (!_exe)
		return false;

	fseek(_exe, offset, SEEK_SET);

	uint32 align = 1 << readUint16LE(_exe);

	uint16 typeID = readUint16LE(_exe);
	while (typeID != 0) {
		uint16 resCount = readUint16LE(_exe);

		readUint32LE(_exe); // reserved

		for (int i = 0; i < resCount; i++) {
			Resource res;

			// Resource properties
			res.offset = readUint16LE(_exe) * align;
			res.size = readUint16LE(_exe) * align;
			res.flags = readUint16LE(_exe);
			uint16 id = readUint16LE(_exe);
			res.handle = readUint16LE(_exe);
			res.usage = readUint16LE(_exe);

			res.type = typeID;

			if ((id & 0x8000) == 0)
				res.id = getResourceString(offset + id);
			else
				res.id = id & 0x7FFF;

			_resources.push_back(res);
		}

		typeID = readUint16LE(_exe);
	}

	return true;
}

std::string NEResources::getResourceString(uint32 offset) {
	uint32 curPos = ftell(_exe);

	fseek(_exe, offset, SEEK_SET);

	byte length = readByte(_exe);

	std::string string;
	for (uint16 i = 0; i < length; i++)
		string += (char)readByte(_exe);

	fseek(_exe, curPos, SEEK_SET);
	return string;
}

const NEResources::Resource *NEResources::findResource(uint16 type, NEResourceID id) const {
	for (uint32 i = 0; i < _resources.size(); i++)
		if (_resources[i].type == type && _resources[i].id == id)
			return &_resources[i];

	return 0;
}

DataSet *NEResources::getResource(uint16 type, NEResourceID id) {
	const Resource *res = findResource(type, id);

	if (!res)
		return 0;

	fseek(_exe, res->offset, SEEK_SET);

	DataSet *set = new DataSet();
	set->size = res->size;
	set->data = new byte[set->size];
	fread(set->data, 1, set->size, _exe);
	return set;
}

std::vector<NEResourceID> NEResources::getTypeList(uint16 type) {
	std::vector<NEResourceID> idList;

	for (uint32 i = 0; i < _resources.size(); i++)
		if (_resources[i].type == type)
			idList.push_back(_resources[i].id);

	return idList;
}

bool outputNEBitmap(std::string name, DataSet *data) {
	if (!data) {
		printf("No data");
		return false;
	}

	FILE *output = fopen(name.c_str(), "wb");

	if (!output) {
		printf("Could not open output");
		return false;
	}

	writeUint16BE(output, 'BM');
	writeUint32LE(output, data->size + 14);
	writeUint16LE(output, 0);
	writeUint16LE(output, 0);

	if (READ_LE_UINT16(data->data) != 40) {
		printf("Bitmap format not handled");
		return false;
	}

	uint16 bitsPerPixel = READ_LE_UINT16(data->data + 14);
	uint16 palSize = 0;

	if (bitsPerPixel <= 8) {
		palSize = READ_LE_UINT16(data->data + 32);
		if (!palSize)
			palSize = 1 << bitsPerPixel;
		palSize *= 4;
	}

	writeUint32LE(output, palSize + 40 + 14);

	fwrite(data->data, 1, data->size, output);
	fflush(output);
	fclose(output);
	return true;
}

bool extractAllResources(FILE *input) {
	NEResources res;

	if (!res.loadFromEXE(input))
		return false;

	printf("Extracting bitmaps...\n");
	std::vector<NEResourceID> idList = res.getTypeList(kNEBitmap);

	for (uint32 i = 0; i < idList.size(); i++) {
		DataSet *data = res.getResource(kNEBitmap, idList[i]);
		std::string outputName = idList[i].toString(".bmp");
		printf("\tExtracting %s... ", outputName.c_str());

		if (outputNEBitmap(outputName, data)) {
			printf("Done\n");
			delete data;
		} else {
			printf("\nStopping extraction\n");
			delete data;
			return false;
		}
	}

	return true;
}

int main(int argc, const char **argv) {
	printf("\nNE Executable Resource Extractor\n");
	printf("Extracts Resources from NE Executables\n");
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

	if (!extractAllResources(input))
		return 1;

	fclose(input);

	printf("All Done!\n");
	return 0;
}
