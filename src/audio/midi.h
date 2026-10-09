// SPDX-License-Identifier: GPL-3.0-or-later
// Native one-source MIDI interface for the portable Miles/XMIDI algorithms.
#pragma once
#include "audio/nuked.h"
#include "platform/runtime.h"
class MidiDriver_BASE {
  public:
    static const uint8 MIDI_CHANNEL_COUNT = 16;
    static const uint8 MIDI_RHYTHM_CHANNEL = 9;

    static const byte MIDI_COMMAND_NOTE_OFF = 0x80;
    static const byte MIDI_COMMAND_NOTE_ON = 0x90;
    static const byte MIDI_COMMAND_POLYPHONIC_AFTERTOUCH = 0xA0;
    static const byte MIDI_COMMAND_CONTROL_CHANGE = 0xB0;
    static const byte MIDI_COMMAND_PROGRAM_CHANGE = 0xC0;
    static const byte MIDI_COMMAND_CHANNEL_AFTERTOUCH = 0xD0;
    static const byte MIDI_COMMAND_PITCH_BEND = 0xE0;
    static const byte MIDI_COMMAND_SYSTEM = 0xF0;

    static const byte MIDI_CONTROLLER_BANK_SELECT_MSB = 0x00;
    static const byte MIDI_CONTROLLER_MODULATION = 0x01;
    static const byte MIDI_CONTROLLER_DATA_ENTRY_MSB = 0x06;
    static const byte MIDI_CONTROLLER_VOLUME = 0x07;
    static const byte MIDI_CONTROLLER_BALANCE = 0x08;
    static const byte MIDI_CONTROLLER_PANNING = 0x0A;
    static const byte MIDI_CONTROLLER_EXPRESSION = 0x0B;
    static const byte MIDI_CONTROLLER_BANK_SELECT_LSB = 0x20;
    static const byte MIDI_CONTROLLER_DATA_ENTRY_LSB = 0x26;
    static const byte MIDI_CONTROLLER_SUSTAIN = 0x40;
    static const byte MIDI_CONTROLLER_PORTAMENTO = 0x41;
    static const byte MIDI_CONTROLLER_SOSTENUTO = 0x42;
    static const byte MIDI_CONTROLLER_SOFT = 0x43;
    static const byte MIDI_CONTROLLER_REVERB = 0x5B;
    static const byte MIDI_CONTROLLER_CHORUS = 0x5D;
    static const byte MIDI_CONTROLLER_RPN_LSB = 0x64;
    static const byte MIDI_CONTROLLER_RPN_MSB = 0x65;
    static const byte MIDI_CONTROLLER_ALL_SOUND_OFF = 0x78;
    static const byte MIDI_CONTROLLER_RESET_ALL_CONTROLLERS = 0x79;
    static const byte MIDI_CONTROLLER_ALL_NOTES_OFF = 0x7B;
    static const byte MIDI_CONTROLLER_OMNI_ON = 0x7C;
    static const byte MIDI_CONTROLLER_OMNI_OFF = 0x7D;
    static const byte MIDI_CONTROLLER_MONO_ON = 0x7E;
    static const byte MIDI_CONTROLLER_POLY_ON = 0x7F;

    static const uint16 MIDI_RPN_PITCH_BEND_SENSITIVITY = 0x0000;
    static const uint16 MIDI_RPN_MASTER_TUNING_FINE = 0x0001;
    static const uint16 MIDI_RPN_MASTER_TUNING_COARSE = 0x0002;
    static const uint16 MIDI_RPN_NULL = 0x7F7F;

    static const uint8 MIDI_META_END_OF_TRACK = 0x2F;
    static const uint8 MIDI_META_SEQUENCER = 0x7F;

    static const uint16 MIDI_PITCH_BEND_DEFAULT = 0x2000;
    static const uint8 MIDI_PANNING_DEFAULT = 0x40;
    static const uint8 MIDI_EXPRESSION_DEFAULT = 0x7F;
    static const uint16 MIDI_MASTER_TUNING_FINE_DEFAULT = 0x2000;
    static const uint8 MIDI_MASTER_TUNING_COARSE_DEFAULT = 0x40;
    virtual ~MidiDriver_BASE() = default;
    virtual void send(uint32) = 0;
    virtual void send(int8, uint32 b) { send(b); }
    virtual void metaEvent(byte, const byte*, uint16) {}
    virtual void metaEvent(int8, byte type, const byte* p, uint16 n) { metaEvent(type, p, n); }
    virtual bool isReady(int8) { return true; }
    virtual uint16 sysExNoDelay(const byte*, uint16) { return 0; }
    virtual void stopAllNotes(bool = false) {
        for (int i = 0; i < 16; ++i)
            send(0xB0 | i | (123 << 8));
    }
};
class MidiDriver_Multisource : public MidiDriver_BASE {
  protected:
    struct Source {
        int volume = 255, neutralVolume = 255, type = 0;
    } _sources[1];
    struct Defaults {
        int program[16];
        int pitchBend = -1, modulation = -1, volume = -1, panning = -1, expression = -1,
            pitchBendSensitivity = -1;
        Defaults() { std::fill(program, program + 16, -1); }
    } _controllerDefaults;
    bool _userVolumeScaling = true, _userMute = false;
    int _userSfxVolume = 255, _userMusicVolume = 255;
    const byte* _instrumentRemapping = nullptr;
    uint32 _timerRate = 4000;

  public:
    enum { PROP_MILES_VERSION = 9, PROP_USER_VOLUME_SCALING = 5, SOURCE_TYPE_SFX = 1 };
    virtual int open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual uint32 getBaseTempo() { return 4000; }
    virtual void stopAllNotes(uint8, uint8) = 0;
    void stopAllNotes(bool) override { stopAllNotes(0, 0xFF); }
    virtual void applySourceVolume(uint8) = 0;
    virtual void deinitSource(uint8 s) { stopAllNotes(s, 0xFF); }
    virtual uint32 property(int prop, uint32 value) {
        if (prop == PROP_USER_VOLUME_SCALING)
            _userVolumeScaling = value;
        return 0;
    }
    virtual void render(int16*, int) = 0;
    void setSourceVolume(uint8 s, int v) {
        if (!s) {
            _sources[0].volume = v;
            applySourceVolume(s);
        }
    }
    void setSourceNeutralVolume(int v) { _sources[0].neutralVolume = v; }
    void syncSoundSettings() {
        _userMusicVolume = ConfMan.getInt("music_volume");
        _userSfxVolume = ConfMan.getInt("sfx_volume");
        applySourceVolume(0);
    }
};
namespace OPL {
class OPL {
    NUKED::opl3_chip _chip{};
    uint16 _address = 0;

  public:
    static constexpr int kDefaultCallbackFrequency = 250;
    bool init() {
        NUKED::OPL3_Reset(&_chip, 48000);
        return true;
    }
    void write(int port, int value) {
        if (port & 1)
            NUKED::OPL3_WriteRegBuffered(&_chip, _address, value);
        else
            _address = (value & 0xFF) | ((port << 7) & 0x100);
    }
    void render(int16* out, int frames) { NUKED::OPL3_GenerateStream(&_chip, out, frames); }
};
namespace Config {
enum OplType { kOpl3, kDualOpl2, kOpl2 };
inline int detect(OplType) { return 0; }
inline OPL* create(OplType) { return new OPL; }
} // namespace Config
} // namespace OPL
namespace Audio {
#define MILES_CONTROLLER_SELECT_PATCH_BANK 114
#define MILES_CONTROLLER_PROTECT_VOICE 112
#define MILES_CONTROLLER_PROTECT_TIMBRE 113
#define MILES_CONTROLLER_LOCK_CHANNEL 110
#define MILES_CONTROLLER_PROTECT_CHANNEL 111
#define MILES_CONTROLLER_PITCH_RANGE 6
#define MILES_CONTROLLER_PATCH_REVERB 59
#define MILES_CONTROLLER_PATCH_BENDER 60
#define MILES_CONTROLLER_REVERB_MODE 61
#define MILES_CONTROLLER_REVERB_TIME 62
#define MILES_CONTROLLER_REVERB_LEVEL 63
#define MILES_CONTROLLER_RHYTHM_KEY_TIMBRE 58
#define MILES_CONTROLLER_SYSEX_RANGE_BEGIN 32
#define MILES_CONTROLLER_SYSEX_RANGE_END 46
#define MILES_CONTROLLER_SYSEX_QUEUE_COUNT 3
#define MILES_CONTROLLER_SYSEX_QUEUE_SIZE 32
#define MILES_CONTROLLER_SYSEX_COMMAND_ADDRESS1 0
#define MILES_CONTROLLER_SYSEX_COMMAND_ADDRESS2 1
#define MILES_CONTROLLER_SYSEX_COMMAND_ADDRESS3 2
#define MILES_CONTROLLER_SYSEX_COMMAND_DATA 3
#define MILES_CONTROLLER_SYSEX_COMMAND_FINAL_DATA 4
#define MILES_CONTROLLER_XMIDI_RANGE_BEGIN 110
#define MILES_CONTROLLER_XMIDI_RANGE_END 120
#define MILES_MT32_PATCHES_COUNT 128
#define MILES_MT32_CUSTOMTIMBRE_COUNT 64
#define MILES_MT32_PATCHDATA_COMMONPARAMETER_SIZE 14
#define MILES_MT32_PATCHDATA_PARTIALPARAMETER_SIZE 58
#define MILES_MT32_PATCHDATA_PARTIALPARAMETERS_COUNT 4
#define MILES_MT32_PATCHDATA_TOTAL_SIZE                                                            \
    (MILES_MT32_PATCHDATA_COMMONPARAMETER_SIZE +                                                   \
     (MILES_MT32_PATCHDATA_PARTIALPARAMETER_SIZE * MILES_MT32_PATCHDATA_PARTIALPARAMETERS_COUNT))
#define MILES_DEFAULT_SOURCE_NEUTRAL_VOLUME 256
enum MilesVersion { MILES_VERSION_2 = 2, MILES_VERSION_3 = 3 };
class MidiDriver_Miles_Xmidi_Timbres {
  public:
    virtual ~MidiDriver_Miles_Xmidi_Timbres() = default;
    virtual void processXMIDITimbreChunk(const byte*, uint32) = 0;
};
MidiDriver_Multisource* MidiDriver_Miles_AdLib_create(const Native::Path&, const Native::Path&,
                                                      Native::SeekableReadStream* = nullptr,
                                                      Native::SeekableReadStream* = nullptr);
} // namespace Audio
