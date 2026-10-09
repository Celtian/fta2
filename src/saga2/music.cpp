/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// MIDI music class

#include "saga2/fta.h"
#include "saga2/saga2.h"

#include "saga2/hresmgr.h"
#include "saga2/music.h"

#include "platform/runtime.h"

namespace Saga2 {

Music::Music(hResContext* res) : _musicContext(res) {
    _driver.reset(Audio::MidiDriver_Miles_AdLib_create("SAMPLE.AD", "SAMPLE.OPL"));
    _driver->property(MidiDriver_Multisource::PROP_MILES_VERSION, Audio::MILES_VERSION_3);
    if (_driver->open())
        error("Cannot initialize OPL music");
    _driver->setSourceNeutralVolume(255);
    _parser.reset(MidiParser::createParser_XMIDI(nullptr, nullptr, 0));
    _parser->setMidiDriver(_driver.get());
    _parser->setTimerRate(_driver->getBaseTempo());
    _parser->property(MidiParser::mpCenterPitchWheelOnUnload, 1);
    _parser->property(MidiParser::mpSendSustainOffOnNotesOff, 1);
    syncSoundSettings();
    g_platform->getMixer()->setMusic([this](float* out, int frames) { render(out, frames); });
}
Music::~Music() {
    g_platform->getMixer()->setMusic({});
    _parser->stopPlaying();
    _parser.reset();
    _driver->close();
}
bool Music::isPlaying() {
    std::lock_guard lock(_mutex);
    return _parser->isPlaying();
}
void Music::play(uint32 id, MusicFlags flags) {
    std::lock_guard lock(_mutex);
    if (_parser->isPlaying() && id == _trackNumber)
        return;
    _parser->unloadMusic();
    _trackNumber = id;
    std::unique_ptr<byte, decltype(&free)> data(
        static_cast<byte*>(LoadResource(_musicContext, id, "music data")), free);
    auto size = _musicContext->size(id);
    if (!data || !size)
        error("Missing music resource %u", id);
    _buffer.assign(data.get(), data.get() + size);
    _parser->property(MidiParser::mpAutoLoop, flags & MUSIC_LOOP);
    if (!_parser->loadMusic(_buffer.data(), _buffer.size()))
        error("Invalid XMIDI resource %u", id);
    _samplesUntilTick = 0;
}
void Music::pause() {
    std::lock_guard lock(_mutex);
    _parser->pausePlaying();
}
void Music::resume() {
    std::lock_guard lock(_mutex);
    _parser->resumePlaying();
}
void Music::stop() {
    std::lock_guard lock(_mutex);
    _parser->stopPlaying();
}
void Music::setVolume(int v) {
    std::lock_guard lock(_mutex);
    _currentVolume = v;
    _driver->setSourceVolume(0, v);
}
void Music::syncSoundSettings() {
    std::lock_guard lock(_mutex);
    _muted = ConfMan.getBool("music_mute");
    _driver->syncSoundSettings();
}
void Music::render(float* out, int frames) {
    std::lock_guard lock(_mutex);
    std::vector<int16> samples(frames * 2);
    int offset = 0;
    while (offset < frames) {
        if (!_samplesUntilTick) {
            _parser->onTimer();
            _samplesUntilTick = 48000 / OPL::OPL::kDefaultCallbackFrequency;
        }
        int count = MIN(uint64(frames - offset), _samplesUntilTick);
        _driver->render(samples.data() + offset * 2, count);
        _samplesUntilTick -= count;
        offset += count;
    }
    if (!_muted) {
        for (int i = 0; i < frames * 2; ++i)
            out[i] += float(samples[i]) / 32768;
    }
}
} // namespace Saga2
