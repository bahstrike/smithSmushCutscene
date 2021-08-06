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

// Based on the ScummVM and ResidualVM SMUSH code (GPLv2+ and LGPL v2.1,
// respectively).

#include "audioman.h"
#include "audiostream.h"
#include "codec48.h"
#include "pcm.h"
#include "smushchannel.h"
#include "smushvideo.h"
#include "stream.h"
#include "util.h"
#include <Windows.h>

// OVERALL STATUS:
// Basic parsing achieved
// Basic video playback achieved
// Basic audio playback achieved
// Need ANIM v1 frame rate detection
// A/V Sync could be improved

// ANIM:
// Rebel Assault: Decodes a few videos, missing several codecs, missing ghost support, missing negative coordinate handling
// Rebel Assault II: Decodes most videos, missing at least one codec
// The Dig/Full Throttle/CMI/Shadows of the Empire/Grim Demo/Outlaws/Mysteries of the Sith: Decodes all videos
// Mortimer: Some videos work, but looks like it scales up low-res frames; missing codec 23
// IACT audio (CMI/SotE/Grim Demo/Outlaws/MotS) works
// iMuse audio (The Dig) works
// PSAD audio (Rebel Assault/Rebel Assault II/Full Throttle/Mortimer) mostly works

// SANM:
// X-Wing Alliance/Grim Fandango/Racer: Should playback video fine
// Infernal Machine: Untested
// VIMA audio works


SMUSHVideo::SMUSHVideo(AudioManager &audio) : _audio(&audio) {
	cutscene_string_id = 0;
	curFrame = 0;
	lastFrameTick = 0;
	_file = 0;
	_buffer = _storedFrame = 0;
	_storeFrame = false;
	_codec48 = 0;
	_runSoundHeaderCheck = false;
	_ranIACTSoundCheck = false;
	_audioChannels = 0;
	_width = _height = 0;
	_iactStream = 0;
	_iactBuffer = 0;
	_frameRate = 0;
	_audioRate = 0;
}

SMUSHVideo::~SMUSHVideo() {
	close();
}

bool SMUSHVideo::load(void* buf, int len) {
	_file = wrapCompressedReadStream(createReadStream(buf, len));

	if (!_file)
		return false;

	_mainTag = _file->readUint32BE();
	if (_mainTag == MKTAG('S', 'A', 'U', 'D')) {
		fprintf(stderr, "Standalone SMUSH audio files not supported atm\n");
		close();
		return false;
	} else if (_mainTag != MKTAG('A', 'N', 'I', 'M') && _mainTag != MKTAG('S', 'A', 'N', 'M')) {
		fprintf(stderr, "Not a valid SMUSH FourCC\n");
		close();
		return false;
	}

	_file->readUint32BE(); // file size

	if (!readHeader()) {
		fprintf(stderr, "Problem while reading SMUSH header\n");
		close();
		return false;
	}

	/*printf("'%s' Details:\n", fileName);
	printf("\tSMUSH Tag: '%c%c%c%c'\n", LISTTAG(_mainTag));
	printf("\tFrame Count: %d\n", _frameCount);
	printf("\tWidth: %d\n", _width);
	printf("\tHeight: %d\n", _height);
	if (_mainTag == MKTAG('A', 'N', 'I', 'M')) {
		printf("\tVersion: %d\n", _version);
		if (_version == 2) {
			printf("\tFrame Rate: %d\n", _frameRate);
			printf("\tAudio Rate: %dHz\n", _audioRate);
		}
	} else {
		printf("\tFrame Rate: %d\n", (int)(1000000.0f / _frameRate + 0.5f)); // approximate
		if (_audioRate != 0) {
			printf("\tAudio Rate: %dHz\n", _audioRate);
			printf("\tAudio Channels: %d\n", _audioChannels);
		}
	}*/

	return true;
}

void SMUSHVideo::close() {
	_audio->stopAll();

	if (_file) {
		delete _file;
		_file = 0;

		delete[] _buffer;
		_buffer = 0;

		delete[] _storedFrame;
		_storedFrame = 0;

		delete _codec48;
		_codec48 = 0;

		_iactStream = 0;

		delete[] _iactBuffer;
		_iactBuffer = 0;

		_runSoundHeaderCheck = false;
		_ranIACTSoundCheck = false;
		_storeFrame = false;
		_audioChannels = 0;
		_width = _height = 0;
		_frameRate = 0;
		_audioRate = 0;

		for (ChannelMap::iterator it = _audioTracks.begin(); it != _audioTracks.end(); it++)
			delete it->second;

		_audioTracks.clear();
	}
}

bool SMUSHVideo::isHighColor() const {
	return _mainTag == MKTAG('S', 'A', 'N', 'M');
}

uint SMUSHVideo::getWidth() const {
	return _width;
}

uint SMUSHVideo::getHeight() const {
	return _height;
}

uint SMUSHVideo::getNumFrames() const {
	return _frameCount;
}

double SMUSHVideo::getFPS() const {
	return (double)_frameRate;
}

uint32 SMUSHVideo::getNextFrameTime(uint32 curFrame) const {
	// SANM stores the frame rate as time between frames
	if (_mainTag == MKTAG('S', 'A', 'N', 'M'))
		return curFrame * _frameRate / 1000;

	// Otherwise, in terms of frames per second
	return curFrame * 1000 / _frameRate;
}

uint32 GetTicks()
{
	return timeGetTime();
}

int SMUSHVideo::frame(GraphicsManager &gfx)
{
	if(lastFrameTick == 0)
	{
		// first frame?
		if (!isHighColor())
			gfx.setPalette(_palette, 0, 256);

		lastFrameTick = GetTicks();
	}

	if(curFrame >= _frameCount)
		return 2/*done*/;

	uint32 tick = GetTicks();
#if 1
	if(tick-lastFrameTick <= getNextFrameTime(curFrame))
		return 0/*no new frame*/;
#endif

	//lastFrameTick = tick;
	handleFrame(gfx);
	gfx.update();
	curFrame++;

	return 1/*new frame*/;
}

void SMUSHVideo::play(GraphicsManager &gfx) {
	if (!isLoaded())
		return;

	// Set the palette from the header for 8bpp videos
	if (!isHighColor())
		gfx.setPalette(_palette, 0, 256);

	uint32 startTime = GetTicks();
	uint curFrame = 0;

	while (curFrame < _frameCount) {
		if (GetTicks() > startTime + getNextFrameTime(curFrame)) {
			if (!handleFrame(gfx)) {
				fprintf(stderr, "Problem during frame decode\n");
				return;
			}

			gfx.update();
			curFrame++;
		}

		/*SDL_Event event;
		while (SDL_PollEvent(&event))
			if (event.type == SDL_QUIT)
				return;*/

		//SDL_Delay(10);
		Sleep(10);
	}

	printf("Done!\n");
}

bool SMUSHVideo::readHeader() {
	uint32 tag = _file->readUint32BE();
	uint32 size = _file->readUint32BE();
	uint32 pos = _file->pos();

	if (tag == MKTAG('A', 'H', 'D', 'R')) {
		if (size < 0x306)
			return false;

		_version = _file->readUint16LE();
		_frameCount = _file->readUint16LE();
		_file->readUint16LE(); // unknown

		_file->read(_palette, 256 * 3);

		if (_version == 2) {
			// This seems to be the only difference between v1 and v2
			if (size < 0x312) {
				fprintf(stderr, "ANIM v2 without extended header\n");
				return false;
			}

			_frameRate = _file->readUint32LE();
			_file->readUint32LE();
			_audioRate = _file->readUint32LE(); // This isn't right for CMI? O_o -- Also doesn't guarantee audio
			_audioChannels = 1; // FIXME: Is this right?
		} else {
			// TODO: Figure out proper values
			_frameRate = 15;
			_audioRate = 11025;
			_audioChannels = 1;
		}

		_file->seek(pos + size + (size & 1), SEEK_SET);
		return detectFrameSize();
	} else if (tag == MKTAG('S', 'H', 'D', 'R')) {
		_file->readUint16LE();
		_frameCount = _file->readUint32LE();
		_file->readUint16LE();
		_width = _file->readUint16LE();
		_pitch = _width * 2;
		_height = _file->readUint16LE();
		_file->readUint16LE();
		_frameRate = _file->readUint32LE();
		/* _flags = */ _file->readUint16LE();
		_file->seek(pos + size + (size & 1), SEEK_SET);
		return readFrameHeader();
	}

	fprintf(stderr, "Unknown SMUSH header type '%c%c%c%c'\n", LISTTAG(tag));
	return false;
}

bool SMUSHVideo::handleFrame(GraphicsManager &gfx) {
	uint32 tag = _file->readUint32BE();
	uint32 size = _file->readUint32BE();
	uint32 pos = _file->pos();

	if (tag == MKTAG('A', 'N', 'N', 'O')) {
		// Skip over any ANNO tag
		// (SANM only)
		_file->seek(pos + size + (size & 1), SEEK_SET);
		tag = _file->readUint32BE();
		size = _file->readUint32BE();
		pos = _file->pos();
	}

	// Now we have to be at FRME
	if (tag != MKTAG('F', 'R', 'M', 'E'))
		return false;

	uint32 bytesLeft = size;
	while (bytesLeft > 0) {
		uint32 subType = _file->readUint32BE();
		uint32 subSize = _file->readUint32BE();
		uint32 subPos = _file->pos();

		if (_file->eos()) {
			// HACK: L2PLAY.ANM from Rebel Assault seems to have an unaligned FOBJ :/
			fprintf(stderr, "Unexpected end of file!\n");
			return false;
		}

		bool result = true;

		switch (subType) {
		case MKTAG('F', 'O', 'B', 'J'):
			result = handleFrameObject(gfx, subSize);
			break;
		case MKTAG('F', 'T', 'C', 'H'):
			result = handleFetch(subSize);
			break;
		case MKTAG('I', 'A', 'C', 'T'):
			result = handleIACT(subSize);
			break;
		case MKTAG('N', 'P', 'A', 'L'):
			result = handleNewPalette(gfx, subSize);
			break;
		case MKTAG('S', 'T', 'O', 'R'):
			result = handleStore(subSize);
			break;
		case MKTAG('T', 'E', 'X', 'T'):
		case MKTAG('T', 'R', 'E', 'S'):
			result = handleText(subType, subSize);
			break;
		case MKTAG('X', 'P', 'A', 'L'):
			result = handleDeltaPalette(gfx, subSize);
			break;
		default:
			// TODO: Other types
			printf("\tSub Type: '%c%c%c%c'\n", LISTTAG(subType));
		}

		if (!result)
			return false;

		bytesLeft -= subSize + 8 + (subSize & 1);
		_file->seek(subPos + subSize + (subSize & 1), SEEK_SET);
	}

	_file->seek(pos + size + (size & 1), SEEK_SET);
	return true;
}

bool SMUSHVideo::handleNewPalette(GraphicsManager &gfx, uint32 size) {
	// Load a new palette

	if (size < 256 * 3) {
		fprintf(stderr, "Bad NPAL chunk\n");
		return false;
	}

	_file->read(_palette, 256 * 3);
	gfx.setPalette(_palette, 0, 256);
	return true;
}

static byte deltaColor(byte pal, int16 delta) {
	int t = (pal * 129 + delta) / 128;
	if (t < 0)
		t = 0;
	else if (t > 255)
		t = 255;
	return t;
}

bool SMUSHVideo::handleDeltaPalette(GraphicsManager &gfx, uint32 size) {
	// Decode a delta palette

	if (size == 256 * 3 * 3 + 4) {
		_file->seek(4, SEEK_CUR);

		for (uint16 i = 0; i < 256 * 3; i++)
			_deltaPalette[i] = _file->readUint16LE();

		_file->read(_palette, 256 * 3);
		gfx.setPalette(_palette, 0, 256);
		return true;
	} else if (size == 6 || size == 4) {
		for (uint16 i = 0; i < 256 * 3; i++)
			_palette[i] = deltaColor(_palette[i], _deltaPalette[i]);

		gfx.setPalette(_palette, 0, 256);
		return true;
	} else if (size == 256 * 3 * 2 + 4) {
		// SMUSH v1 only
		_file->seek(4, SEEK_CUR);

		for (uint16 i = 0; i < 256 * 3; i++)
			_deltaPalette[i] = _file->readUint16LE();
		return true;
	}

	fprintf(stderr, "Bad XPAL chunk (%d)\n", size);
	return false;
}

bool SMUSHVideo::handleFrameObject(GraphicsManager &gfx, uint32 size) {
	return handleFrameObject(gfx, _file, size);
}

bool SMUSHVideo::handleFrameObject(GraphicsManager &gfx, SeekableReadStream *stream, uint32 size) {
	// Decode a frame object

	if (isHighColor()) {
		fprintf(stderr, "Frame object chunk in 16bpp video\n");
		return false;
	}

	if (size < 14)
		return false;

	byte codec = stream->readByte();
	/* byte codecParam = */ stream->readByte();
	int16 left = stream->readSint16LE();
	int16 top = stream->readSint16LE();
	uint16 width = stream->readUint16LE();
	uint16 height = stream->readUint16LE();
	stream->readUint16LE();
	stream->readUint16LE();

	size -= 14;
	
	if (codec == 37 || codec == 47 || codec == 48) {
		// We ignore left/top for these codecs
		if (width != _width || height != _height) {
			// FIXME: The Dig's SQ1.SAN also has extra large frames (seem broken)
			fprintf(stderr, "Modified codec %d coordinates %d, %d\n", codec, width, height);
			return true;
		}
	} else if (left < 0 || top < 0 || left + width > (int)_width || top + height > (int)_height) {
		// TODO: We should be drawing partial frames
		fprintf(stderr, "Bad codec %d coordinates %d, %d, %d, %d\n", codec, left, top, width, height);
		return true;
	}

	switch (codec) {
	case 1:
	case 3:
		decodeCodec1(stream, left, top, width, height);
		break;
	case 48: {
		// Used by Mysteries of the Sith
		// Seems similar to codec 47
		byte *ptr = new byte[size];
		stream->read(ptr, size);

		if (!_codec48)
			_codec48 = new Codec48Decoder(width, height);

		_codec48->decode(_buffer, ptr);
		delete[] ptr;
		} break;
	default:
		// TODO: Lots of other Rebel Assault ones
		// They look like a terrible compression
		fprintf(stderr, "Unknown codec %d\n", codec);
		//return false;
		break;
	}

	if (_storeFrame) {
		if (!_storedFrame)
			_storedFrame = new byte[_pitch * _height];

		memcpy(_storedFrame, _buffer, _pitch * _height);
		_storeFrame = false;
	}

	// Ideally, this call should be at the end of the FRME block, but it
	// seems that breaks things like the video in Rebel Assault of Cmdr.
	// Farrell coming in to save you.
	gfx.blit(_buffer, 0, 0, _width, _height, _pitch);
	return true;
}

bool SMUSHVideo::handleStore(uint32 size) {
	// Store the next frame object
	// TODO: There's definitely a mechanism to grab more than just what's on
	// the screen. RA's L3INTRO.ANM draws overlarge frames, then expects to
	// later restore them, while moving them.
	_storeFrame = true;
	return size >= 4;
}

bool SMUSHVideo::handleText(uint32 type, uint32 size) {
	int pos_x = _file->readSint16LE();
	int pos_y = _file->readSint16LE();
	int flags = _file->readSint16LE();
	int left = _file->readSint16LE();
	int top = _file->readSint16LE();
	int right = _file->readSint16LE();
	int32 height = _file->readSint16LE();
	int32 unk2 = _file->readUint16LE();

	if(type == MKTAG('T', 'E', 'X', 'T'))
	{
		char* sz = new char[size-16];
		_file->read(sz, size-16);

	} else {
		int string_id = _file->readUint16LE();

		if(string_id != cutscene_string_id)
		{
			cutscene_string_id = string_id;
		}
	}
	
	return true;
}

bool SMUSHVideo::handleFetch(uint32 size) {
	// Restore an previous frame object
	int32 xOffset = 0, yOffset = 0;

	// Skip the first uint32. It's some sort of index.
	// After a STOR, the value is always -1. Then it increases
	// by 1 each call after that.
	if (size >= 4)
		/* int32 u0 = */ _file->readSint32BE();

	// Offset for drawing in the x direction
	if (size >= 8)
		xOffset = _file->readSint32BE();

	// Offset for drawing in the y direction
	if (size >= 12)
		yOffset = _file->readSint32BE();

	if (_storedFrame && _buffer) {
		for (uint y = 0; y < _height; y++) {
			int realY = yOffset + y;
			if (realY < 0 || realY >= (int)_height)
				continue;

			for (uint x = 0; x < _width; x++) {
				int realX = xOffset + x;
				if (realX < 0 || realX >= (int)_width)	
					continue;

				_buffer[realY * _pitch + realX] = _storedFrame[y * _pitch + x];
			}
		}
	}

	return true;
}

void SMUSHVideo::decodeCodec1(SeekableReadStream *stream, int left, int top, uint width, uint height) {
	// This is very similar to the bomp compression
	for (uint y = 0; y < height; y++) {
		uint16 lineSize = stream->readUint16LE();
		byte *dst = _buffer + (top + y) * _pitch + left;

		while (lineSize > 0) {
			byte code = stream->readByte();
			lineSize--;
			byte length = (code >> 1) + 1;

			if (code & 1) {
				byte val = stream->readByte();
				lineSize--;

				if (val != 0)
					memset(dst, val, length);

				dst += length;
			} else {
				lineSize -= length;

				while (length--) {
					byte val = stream->readByte();

					if (val)
						*dst = val;

					dst++;
				}
			}
		}
	}
}

void SMUSHVideo::detectSoundHeaderType() {
	// We're just assuming that maxFrames and flags are not going to be zero
	// for the newer header and that the first chunk in the old header
	// will have index = 0 (which seems to be pretty safe).

	_file->readUint32BE();
	_oldSoundHeader = (_file->readUint32BE() == 0);

	_file->seek(-8, SEEK_CUR);
	_runSoundHeaderCheck = true;
}

bool SMUSHVideo::readFrameHeader() {
	// SANM frame header

	if (_file->readUint32BE() != MKTAG('F', 'L', 'H', 'D'))
		return false;

	uint32 size = _file->readUint32BE();
	uint32 pos = _file->pos();
	uint32 bytesLeft = size;

	while (bytesLeft > 0) {
		uint32 subType = _file->readUint32BE();
		uint32 subSize = _file->readUint32BE();
		uint32 subPos = _file->pos();

		bool result = true;

		switch (subType) {
		case MKTAG('B', 'l', '1', '6'):
			// Nothing to do
			break;
		case MKTAG('W', 'a', 'v', 'e'):
			_audioRate = _file->readUint32LE();
			_audioChannels = _file->readUint32LE();

			// HACK: Based on what Residual does
			// Seems the size is always 12 even when it's not :P
			subSize = 12;
			break;
		default:
			fprintf(stderr, "Invalid SANM frame header type '%c%c%c%c'\n", LISTTAG(subType));
			result = false;
		}

		if (!result)
			return false;

		bytesLeft -= subSize + 8 + (subSize & 1);
		_file->seek(subPos + subSize + (subSize & 1), SEEK_SET);
	}

	_file->seek(pos + size + (size & 1), SEEK_SET);
	return true;
}

bool SMUSHVideo::handleIACT(uint32 size) {
	// Handle interactive sequences

	if (size < 8)
		return false;

	uint16 code = _file->readUint16LE();
	uint16 flags = _file->readUint16LE();
	/* int16 unknown = */ _file->readSint16LE();
	uint16 trackFlags = _file->readUint16LE();

	if (code == 8 && flags == 46) {
		if (!_ranIACTSoundCheck)
			detectIACTType(trackFlags);

		if (_hasIACTSound) {
			// Audio track
			if (trackFlags == 0)
				return bufferIACTAudio(size);
		}
	} if (code == 6 && flags == 38) {
		// Clear frame? Seems to fix some RA2 videos
		//if (_buffer)
		//	memset(_buffer, 0, _pitch * _height);
	} else {
		// Otherwise, the data is meant for INSANE
	}

	return true;
}

bool SMUSHVideo::bufferIACTAudio(uint32 size) {
	// Queue IACT audio (22050Hz)

	if (!_iactStream) {
		// Ignore _audioRate since it's always 22050Hz
		// and CMI often lies and says 11025Hz
		_iactStream = makeQueuingAudioStream(22050, 2);
		_audio->play(_iactStream);
		_iactPos = 0;
		_iactBuffer = new byte[4096];
	}

	/* uint16 trackID = */ _file->readUint16LE();
	/* uint16 index = */ _file->readUint16LE();
	/* uint16 frameCount = */ _file->readUint16LE();
	/* uint32 bytesLeft = */ _file->readUint32LE();
	size -= 18;

	while (size > 0) {
		if (_iactPos >= 2) {
			uint32 length = READ_BE_UINT16(_iactBuffer) + 2;
			length -= _iactPos;

			if (length > size) {
				_file->read(_iactBuffer + _iactPos, size);
				_iactPos += size;
				size = 0;
			} else {
				byte *output = new byte[4096];

				_file->read(_iactBuffer + _iactPos, length);

				byte *dst = output;
				byte *src = _iactBuffer + 2;

				int count = 1024;
				byte var1 = *src++;
				byte var2 = var1 >> 4;
				var1 &= 0xF;

				while (count--) {
					byte value = *src++;
					if (value == 0x80) {
						*dst++ = *src++;
						*dst++ = *src++;
					} else {
						int16 val = (int8)value << var2;
						*dst++ = val >> 8;
						*dst++ = (byte)val;
					}

					value = *src++;
					if (value == 0x80) {
						*dst++ = *src++;
						*dst++ = *src++;
					} else {
						int16 val = (int8)value << var1;
						*dst++ = val >> 8;
						*dst++ = (byte)val;
					}
				}

				_iactStream->queueAudioStream(makePCMStream(output, 4096, _iactStream->getRate(), _iactStream->getChannels(), FLAG_16BITS));
				size -= length;
				_iactPos = 0;
			}
		} else {
			if (size > 1 && _iactPos == 0) {
				_iactBuffer[0] = _file->readByte();
				_iactPos = 1;
				size--;
			}

			_iactBuffer[_iactPos] = _file->readByte();
			_iactPos++;
			size--;
		}
	}

	return true;
}

bool SMUSHVideo::detectFrameSize() {
	// There is no frame size, so we'll be using a heuristic to detect it.

	// Basically, codecs 37, 47, and 48 work directly off of the whole frame
	// so they generally will always show the correct size. (Except for
	// Mortimer which does some funky frame scaling/resizing). There we'll
	// have to resize based on the dimensions of 37 and scale appropriately
	// to 640x480.

	// Most of this is for detecting the total frame size of a Rebel Assault
	// video which is a lot harder.

	uint32 startPos = _file->pos();
	bool done = false;

	// Only go through a certain amount of frames
	uint32 maxFrames = 20;
	if (maxFrames > _frameCount)
		maxFrames = _frameCount;

	for (uint i = 0; i < maxFrames && !done; i++) {
		if (_file->readUint32BE() != MKTAG('F', 'R', 'M', 'E'))
			return false;

		uint32 frameSize = _file->readUint32BE();
		uint32 bytesLeft = frameSize;

		while (bytesLeft > 0) {
			uint32 subType = _file->readUint32BE();
			uint32 subSize = _file->readUint32BE();
			uint32 subPos = _file->pos();

			if (_file->eos()) {
				// HACK: L2PLAY.ANM from Rebel Assault seems to have an unaligned FOBJ :/
				fprintf(stderr, "Unexpected end of file!\n");
				return false;
			}

			if (subType == MKTAG('F', 'O', 'B', 'J')) {
				SeekableReadStream *stream = _file;

				byte codec = stream->readByte();
				/* byte codecParam = */ stream->readByte();
				int16 left = stream->readSint16LE();
				int16 top = stream->readSint16LE();
				uint16 width = stream->readUint16LE();
				uint16 height = stream->readUint16LE();
				stream->readUint16LE();
				stream->readUint16LE();

				if (width != 1 && height != 1) {
					// HACK: Some Full Throttle videos start off with this. Don't
					// want our algorithm to be thrown off.

					// Codecs 37, 47, and 48 should be telling the truth
					if (codec == 37 || codec == 47 || codec == 48) {
						_width = width;
						_height = height;
						done = true;
					} else {
						// FIXME: Just take other codecs at face value for now too
						// (This basically only affects Rebel Assault and NUT files)
						_width = width;
						if (left > 0)
							_width += left;

						_height = height;
						if (top > 0)
							_height += top;

						// Try to figure how close we are to 320x200 and see if maybe
						// this object is a partial frame object.
						// TODO: Not ready for primetime yet
						/*if (_width < 320 && _width > 310)
							_width = 320;
						if (height < 200 && _height > 190)
							_height = 200;*/

						done = true;
					}
				}

				if (done)
					break;
			}

			bytesLeft -= subSize + 8 + (subSize & 1);
			_file->seek(subPos + subSize + (subSize & 1), SEEK_SET);
		}
	}

	if (_width == 0 || _height == 0)
		return false;

	_file->seek(startPos, SEEK_SET);
	_pitch = _width;
	_buffer = new byte[_pitch * _height];
	memset(_buffer, 0, _pitch * _height); // FIXME: Is this right?
	return true;
}

SMUSHChannel *SMUSHVideo::findAudioTrack(const SMUSHTrackHandle &track) {
	ChannelMap::iterator it = _audioTracks.find(track);

	if (it != _audioTracks.end())
		return it->second;

	return 0;
}

void SMUSHVideo::detectIACTType(uint flags) {
	// Detect the IACT sound type

	if (flags == 0) {
		// CMI-era sound, OK
		_hasIACTSound = true;
	}

	_ranIACTSoundCheck = true;
}

// Just a simple < operator for our three values
bool operator<(const SMUSHTrackHandle &handle1, const SMUSHTrackHandle &handle2) {
	if (handle1.type < handle2.type)
		return true;

	if (handle1.type > handle2.type)
		return false;

	if (handle1.id < handle2.id)
		return true;

	if (handle1.id > handle2.id)
		return false;

	if (handle1.maxFrames < handle2.maxFrames)
		return true;

	return false;
}