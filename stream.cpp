/* smushplay - A simple LucasArts SMUSH video player
 *
 * smushplay is the legal property of its developers, whose names can be
 * found in the AUTHORS file distributed with this source
 * distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

// Based on ScummVM Stream classes (GPLv2+)

#include <assert.h>
#include <stdio.h>
#include <string>
#include "stream.h"

uint32 MemoryReadStream::read(void *dataPtr, uint32 dataSize) {
	// Read at most as many bytes as are still available...
	if (dataSize > _size - _pos) {
		dataSize = _size - _pos;
		_eos = true;
	}
	memcpy(dataPtr, _ptr, dataSize);

	_ptr += dataSize;
	_pos += dataSize;

	return dataSize;
}

bool MemoryReadStream::seek(int32 offs, int whence) {
	// Pre-Condition
	assert(_pos <= _size);
	switch (whence) {
	case SEEK_END:
		// SEEK_END works just like SEEK_SET, only 'reversed',
		// i.e. from the end.
		offs = _size + offs;
		// Fall through
	case SEEK_SET:
		_ptr = _ptrOrig + offs;
		_pos = offs;
		break;

	case SEEK_CUR:
		_ptr += offs;
		_pos += offs;
		break;
	}
	// Post-Condition
	assert(_pos <= _size);

	// Reset end-of-stream flag on a successful seek
	_eos = false;
	return true;	// FIXME: STREAM REWRITE
}

class StdioStream : public SeekableReadStream {
public:
	StdioStream() {}
	StdioStream(FILE *handle);
	~StdioStream();

	bool err() const;
	void clearErr();
	bool eos() const;

	uint32 write(const void *dataPtr, uint32 dataSize);
	bool flush();

	int32 pos() const;
	int32 size() const;
	bool seek(int32 offs, int whence = SEEK_SET);
	uint32 read(void *dataPtr, uint32 dataSize);

private:
	// Prevent copying instances by accident
	StdioStream(const StdioStream &);
	StdioStream &operator=(const StdioStream &);

	/** File handle to the actual file. */
	FILE *_handle;
};

StdioStream::StdioStream(FILE *handle) : _handle(handle) {
	assert(handle);
}

StdioStream::~StdioStream() {
	fclose(_handle);
}

bool StdioStream::err() const {
	return ferror(_handle) != 0;
}

void StdioStream::clearErr() {
	clearerr(_handle);
}

bool StdioStream::eos() const {
	return feof(_handle) != 0;
}

int32 StdioStream::pos() const {
	return ftell(_handle);
}

int32 StdioStream::size() const {
	int32 oldPos = ftell(_handle);
	fseek(_handle, 0, SEEK_END);
	int32 length = ftell(_handle);
	fseek(_handle, oldPos, SEEK_SET);

	return length;
}

bool StdioStream::seek(int32 offs, int whence) {
	return fseek(_handle, offs, whence) == 0;
}

uint32 StdioStream::read(void *ptr, uint32 len) {
	return fread(ptr, 1, len, _handle);
}

uint32 StdioStream::write(const void *ptr, uint32 len) {
	return fwrite(ptr, 1, len, _handle);
}

bool StdioStream::flush() {
	return fflush(_handle) == 0;
}

SeekableReadStream *createReadStream(const char *pathName) {
	FILE *file = fopen(pathName, "rb");

	if (!file)
		return 0;

	return new StdioStream(file);
}

SeekableReadStream *createReadStream(void* buf, int len) {
	void* cpy = new char[len];
	memcpy(cpy, buf, len);

	return new MemoryReadStream((byte*)cpy, (uint32)len, true);
}

SeekableReadStream *wrapCompressedReadStream(SeekableReadStream *toBeWrapped) {
	if (toBeWrapped) {
		uint16 header = toBeWrapped->readUint16BE();
		bool isCompressed = (header == 0x1F8B ||
				     ((header & 0x0F00) == 0x0800 &&
				      header % 31 == 0));
		toBeWrapped->seek(-2, SEEK_CUR);
		if (isCompressed)
			return nullptr;//new GZipReadStream(toBeWrapped);
	}

	return toBeWrapped;
}
