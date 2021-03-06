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

// Inspired by the ScummVM code of the same name (GPLv2+)

#include <assert.h>
#include "audioman.h"
#include "audiostream.h"
#include "rate.h"
#include "util.h"

AudioManager::AudioManager() {
	InitializeCriticalSection(&critsec);
	_channelSeed = 0;
}

AudioManager::~AudioManager() {
	stopAll();
	//SDL_CloseAudio();
	DeleteCriticalSection(&critsec);
}

bool AudioManager::init() {
	/*SDL_AudioSpec spec;
	spec.freq = 44100;
	spec.format = AUDIO_S16SYS;
	spec.channels = 2;
	spec.samples = 4096;
	spec.callback = sdlCallback;
	spec.userdata = this;

	if (SDL_OpenAudio(&spec, &_spec) != 0)
		return false;

	if (_spec.channels != 2 || _spec.format != AUDIO_S16SYS)
		return false;

	SDL_PauseAudio(0);*/
	return true;
}

void AudioManager::play(AudioStream *stream) {
	AudioHandle handle;
	play(stream, handle);
}

void AudioManager::play(AudioStream *stream, AudioHandle &handle, byte volume, int8 balance) {
	if (!stream)
		return;

	Channel *chan = new Channel(stream, /*_spec.freq*/44100, volume, balance);

	EnterCriticalSection(&critsec);
	handle._id = _channelSeed++;

	if (handle._id == 0xFFFFFFFF) {
		// Probably could have better error handling, but I really hope
		// this never happens :P
		fprintf(stderr, "Rolling over AudioManager id's\n");
		handle._id = 0;
	}

	_channels[handle._id] = chan;
	LeaveCriticalSection(&critsec);
}

void AudioManager::stop(const AudioHandle &handle) {
	if (handle._id == 0xFFFFFFFF)
		return;

	EnterCriticalSection(&critsec);

	ChannelMap::iterator it = _channels.find(handle._id);

	if (it != _channels.end()) {
		delete it->second;
		_channels.erase(it);
	}

	LeaveCriticalSection(&critsec);
}

void AudioManager::stopAll() {
	EnterCriticalSection(&critsec);

	for (ChannelMap::iterator it = _channels.begin(); it != _channels.end(); it++)
		delete it->second;

	_channels.clear();

	LeaveCriticalSection(&critsec);
}

void AudioManager::sdlCallback(void *manager, byte *samples, int len) {
	((AudioManager *)manager)->callbackHandler(samples, len);
}

void AudioManager::callbackHandler(byte *samples, int len) {
	assert((len % 4) == 0);
	memset(samples, 0, len);

	EnterCriticalSection(&critsec);

	for (ChannelMap::iterator it = _channels.begin(); it != _channels.end(); it++) {
		Channel *channel = it->second;

		if (channel->endOfStream()) {
			// TODO: Remove the channel
		} else if (!channel->endOfData()) {
			channel->mix((int16 *)samples, len >> 2);
		}
	}

	LeaveCriticalSection(&critsec);
}

void AudioManager::setVolume(const AudioHandle &handle, byte volume) {
	if (handle._id == 0xFFFFFFFF)
		return;

	EnterCriticalSection(&critsec);

	ChannelMap::iterator it = _channels.find(handle._id);

	if (it != _channels.end())
		it->second->setVolume(volume);

	LeaveCriticalSection(&critsec);
}

byte AudioManager::getVolume(const AudioHandle &handle) {
	if (handle._id == 0xFFFFFFFF)
		return 0;

	byte volume = 0;

	EnterCriticalSection(&critsec);

	ChannelMap::iterator it = _channels.find(handle._id);

	if (it != _channels.end())
		volume = it->second->getVolume();

	LeaveCriticalSection(&critsec);

	return volume;
}

AudioManager::Channel::Channel(AudioStream *stream, uint destFreq, byte volume, int8 balance) {
	_stream = stream;
	_converter = makeRateConverter(stream->getRate(), destFreq, stream->getChannels() == 2);
	_balance = CLIP<int8>(balance, -127, 127);
	_volume = volume;
	updateChannelVolumes();
}

AudioManager::Channel::~Channel() {
	delete _stream;
	delete _converter;
}

void AudioManager::Channel::updateChannelVolumes() {
	// TODO: Global volume setting instead of kMaxAudioManVolume
	int vol = kMaxAudioManVolume * _volume;

	if (_balance == 0) {
		_leftVolume = _rightVolume = vol / kMaxChannelVolume;
	} else if (_balance < 0) {
		_leftVolume = vol / kMaxChannelVolume;
		_rightVolume = ((127 + _balance) * vol) / (kMaxChannelVolume * 127);
	} else {
		_leftVolume = ((127 - _balance) * vol) / (kMaxChannelVolume * 127);
		_rightVolume = vol / kMaxChannelVolume;
	}
}

void AudioManager::Channel::mix(int16 *samples, uint length) {
	_converter->flow(*_stream, samples, length, _leftVolume, _rightVolume);
}

bool AudioManager::Channel::endOfStream() const {
	return _stream->endOfStream();
}

bool AudioManager::Channel::endOfData() const {
	return _stream->endOfData();
}

void AudioManager::Channel::setVolume(byte volume) {
	_volume = volume;
	updateChannelVolumes();
}
