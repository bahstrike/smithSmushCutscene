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

#ifndef SMUSHVIDEO_H
#define SMUSHVIDEO_H

#include <map>
#include "graphicsman.h"
#include "types.h"

class AudioManager;
class Blocky16;
class Codec48Decoder;
class SeekableReadStream;
class SMUSHChannel;
class QueuingAudioStream;

struct SMUSHTrackHandle {
	uint32 type;
	uint32 id;
	uint32 maxFrames;
};

bool operator<(const SMUSHTrackHandle &handle1, const SMUSHTrackHandle &handle2);

class SMUSHVideo {
public:
	SMUSHVideo(AudioManager &audio);
	~SMUSHVideo();

	bool load(void* buf, int len);
	void close();
	bool isLoaded() const { return _file != 0; }
	int frame(GraphicsManager &gfx);
	void play(GraphicsManager &gfx);

	bool isHighColor() const;
	uint getWidth() const;
	uint getHeight() const;
	uint getNumFrames() const;
	double getFPS() const;

	int getCutsceneStringId() const { return cutscene_string_id; }

private:
	uint32 lastFrameTick;
	uint curFrame;

	int cutscene_string_id;

	SeekableReadStream *_file;
	uint _frameRate;

	// Header
	uint32 _mainTag;
	uint _version, _frameCount;

	// Palette
	byte _palette[256 * 3];
	uint16 _deltaPalette[256 * 3];

	// Main Buffer
	byte *_buffer;
	uint _width, _height, _pitch;
	bool detectFrameSize();

	// Stored Frame
	bool _storeFrame;
	byte *_storedFrame;

	// Main Functions
	bool readHeader();
	bool handleFrame(GraphicsManager &gfx);
	bool readFrameHeader();
	uint32 getNextFrameTime(uint32 curFrame) const;

	// Frame Types
	bool handleFrameObject(GraphicsManager &gfx, uint32 size);
	bool handleFetch(uint32 size);
	bool handleIACT(uint32 size);
	bool handleNewPalette(GraphicsManager &gfx, uint32 size);
	bool handleStore(uint32 size);
	bool handleText(uint32 type, uint32 size);
	bool handleDeltaPalette(GraphicsManager &gfx, uint32 size);
	bool handleSoundFrame(uint32 type, uint32 size);

	// Codecs
	bool handleFrameObject(GraphicsManager &gfx, SeekableReadStream *stream, uint32 size);
	void decodeCodec1(SeekableReadStream *stream, int left, int top, uint width, uint height);
	Codec48Decoder *_codec48;

	// Sound
	bool _oldSoundHeader, _runSoundHeaderCheck;
	bool _hasIACTSound, _ranIACTSoundCheck;
	uint _audioRate, _audioChannels;
	void detectSoundHeaderType();
	void detectIACTType(uint32 flags);
	bool bufferIACTAudio(uint32 size);
	AudioManager *_audio;
	QueuingAudioStream *_iactStream;
	byte *_iactBuffer;
	uint32 _iactPos;
	SMUSHChannel *findAudioTrack(const SMUSHTrackHandle &track);
	typedef std::map<SMUSHTrackHandle, SMUSHChannel *> ChannelMap;
	ChannelMap _audioTracks;
};

#endif
