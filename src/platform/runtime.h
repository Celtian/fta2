// SPDX-License-Identifier: GPL-3.0-or-later
// Small native services used by the preserved SAGA2 simulation.
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
using byte = uint8_t;
using uint = unsigned int;
using int8 = int8_t;
using uint8 = uint8_t;
using int16 = int16_t;
using uint16 = uint16_t;
using int32 = int32_t;
using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;
#define ARRAYSIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MKTAG(a, b, c, d)                                                                          \
    ((uint32(uint8(a)) << 24) | (uint32(uint8(b)) << 16) | (uint32(uint8(c)) << 8) | uint8(d))
#if defined(_MSC_VER)
#define PACKED_STRUCT
#else
#define PACKED_STRUCT __attribute__((packed))
#endif
#define ARRAYCLEAR(x)                                                                              \
    std::fill(std::begin(x), std::end(x), std::remove_reference_t<decltype((x)[0])>{})
#define GCC_PRINTF(a, b)
#define GCC_NORETURN [[noreturn]]
template <class A, class B> constexpr auto MIN(A a, B b) { return a < b ? a : b; }
template <class A, class B> constexpr auto MAX(A a, B b) { return a > b ? a : b; }
template <class A, class B, class C> constexpr auto CLIP(A a, B b, C c) {
    return MIN(MAX(a, b), c);
}
template <class T> constexpr auto ABS(T a) { return a < 0 ? -a : a; }
template <class T> void SWAP(T& a, T& b) { std::swap(a, b); }
inline uint16 READ_LE_UINT16(const void* p) {
    auto b = static_cast<const byte*>(p);
    return b[0] | uint16(b[1]) << 8;
}
inline int16 READ_LE_INT16(const void* p) { return int16(READ_LE_UINT16(p)); }
inline uint32 READ_LE_UINT32(const void* p) {
    auto b = static_cast<const byte*>(p);
    return READ_LE_UINT16(b) | uint32(READ_LE_UINT16(b + 2)) << 16;
}
inline int32 READ_LE_INT32(const void* p) { return int32(READ_LE_UINT32(p)); }
inline int32 READ_BE_INT32(const void* p);
inline uint16 READ_BE_UINT16(const void* p) {
    auto b = static_cast<const byte*>(p);
    return uint16(b[0]) << 8 | b[1];
}
inline uint32 READ_BE_UINT32(const void* p) {
    auto b = static_cast<const byte*>(p);
    return uint32(READ_BE_UINT16(b)) << 16 | READ_BE_UINT16(b + 2);
}
inline int32 READ_BE_INT32(const void* p) { return int32(READ_BE_UINT32(p)); }
inline void WRITE_LE_UINT16(void* p, uint16 x) {
    auto b = static_cast<byte*>(p);
    b[0] = x;
    b[1] = x >> 8;
}
inline void WRITE_LE_UINT32(void* p, uint32 x) {
    auto b = static_cast<byte*>(p);
    WRITE_LE_UINT16(b, x);
    WRITE_LE_UINT16(b + 2, x >> 16);
}
inline void WRITE_BE_UINT16(void* p, uint16 x) {
    auto b = static_cast<byte*>(p);
    b[0] = x >> 8;
    b[1] = x;
}
inline void WRITE_BE_UINT32(void* p, uint32 x) {
    auto b = static_cast<byte*>(p);
    WRITE_BE_UINT16(b, x >> 16);
    WRITE_BE_UINT16(b + 2, x);
}
enum class DisposeAfterUse { NO, YES };
const char* tag2str(uint32);
const char* tag2strP(uint32);
void warning(const char* fmt, ...);
[[noreturn]] void error(const char* fmt, ...);
void debug(const char* fmt, ...);
void debug(int level, const char* fmt, ...);
inline bool debugChannelSet(int, int = 0) { return false; }
void debugC(int level, int channel, const char* fmt, ...);
void debugCN(int level, int channel, const char* fmt, ...);
namespace Native {
using std::fill;
class String : public std::string {
  public:
    using std::string::string;
    void toLowercase() {
        for (auto& c : *this)
            c = std::tolower(static_cast<unsigned char>(c));
    }
    String(const std::string& s) : std::string(s) {}
    size_t findFirstOf(char c) const { return find_first_of(c); }
    bool contains(char c) const { return find(c) != npos; }
    bool contains(const String& s) const { return find(s) != npos; }
    bool equalsIgnoreCase(const String& s) const;
    static String format(const char* fmt, ...);
};
using U32String = std::u32string;
class Path : public std::filesystem::path {
  public:
    using std::filesystem::path::path;
    Path(const String& s) : std::filesystem::path(std::string(s)) {}
    Path(const std::filesystem::path& s) : std::filesystem::path(s) {}
    String toString(char = '/') const { return generic_string(); }
};
template <class T> using Array = std::vector<T>;
template <class T> using List = std::list<T>;
template <class K, class V> using HashMap = std::map<K, V>;
template <class T> class Queue : public std::deque<T> {
  public:
    void push(const T& x) { this->push_back(x); }
    T pop() {
        auto x = this->front();
        this->pop_front();
        return x;
    }
};
class NonCopyable {
  protected:
    NonCopyable() = default;

  private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};
size_t strlcpy(char* dest, const char* src, size_t cap);
size_t strlcat(char* dest, const char* src, size_t cap);
inline int vsprintf_s(char* dest, size_t cap, const char* fmt, va_list ap) {
    return vsnprintf(dest, cap, fmt, ap);
}
template <size_t N> int vsprintf_s(char (&dest)[N], const char* fmt, va_list ap) {
    return vsnprintf(dest, N, fmt, ap);
}
inline int sprintf_s(char* dest, size_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dest, cap, fmt, ap);
    va_end(ap);
    return n;
}
template <size_t N, class... A> int sprintf_s(char (&dest)[N], const char* fmt, A... args) {
    return snprintf(dest, N, fmt, args...);
}
inline void strcpy_s(char* d, size_t n, const char* s) { strlcpy(d, s, n); }
template <size_t N> void strcpy_s(char (&d)[N], const char* s) { strlcpy(d, s, N); }
inline void strcat_s(char* d, size_t n, const char* s) { strlcat(d, s, n); }
template <size_t N> void strcat_s(char (&d)[N], const char* s) { strlcat(d, s, N); }
inline bool isDigit(int c) { return c >= '0' && c <= '9'; }
void hexdump(const byte* p, int n, int width = 16, int start = 0);
enum Error { kNoError, kCreatingFileFailed };
class ReadStream {
  public:
    virtual ~ReadStream() = default;
    virtual uint32 read(void*, uint32) = 0;
    virtual bool eos() const = 0;
    int32 readSint32BE() { return int32(readUint32BE()); }
    byte readByte() {
        byte b = 0;
        read(&b, 1);
        return b;
    }
    int8 readSByte() { return int8(readByte()); }
    uint16 readUint16LE() {
        byte b[2] = {};
        read(b, 2);
        return READ_LE_UINT16(b);
    }
    int16 readSint16LE() { return int16(readUint16LE()); }
    uint32 readUint32LE() {
        byte b[4] = {};
        read(b, 4);
        return READ_LE_UINT32(b);
    }
    int32 readSint32LE() { return int32(readUint32LE()); }
    uint32 readUint32BE() {
        byte b[4] = {};
        read(b, 4);
        return READ_BE_UINT32(b);
    }
    uint16 readUint16BE() {
        byte b[2] = {};
        read(b, 2);
        return READ_BE_UINT16(b);
    }
    String readString() {
        String s;
        while (!eos()) {
            auto b = readByte();
            if (!b)
                break;
            s += char(b);
        }
        return s;
    }
};
class SeekableReadStream : public ReadStream {
  public:
    virtual int64 size() const = 0;
    virtual int64 pos() const = 0;
    virtual bool seek(int64, int whence = SEEK_SET) = 0;
    bool skip(int64 n) { return seek(n, SEEK_CUR); }
    SeekableReadStream* readStream(uint32 n);
};
class MemoryReadStream : public SeekableReadStream {
  protected:
    const byte* _data = nullptr;
    size_t _size = 0, _pos = 0;
    bool _eos = false;
    DisposeAfterUse _dispose = DisposeAfterUse::NO;

  public:
    MemoryReadStream(const byte* data, uint32 size, DisposeAfterUse dispose = DisposeAfterUse::NO)
        : _data(data), _size(size), _dispose(dispose) {}
    ~MemoryReadStream() override {
        if (_dispose == DisposeAfterUse::YES)
            std::free(const_cast<byte*>(_data));
    }
    uint32 read(void*, uint32) override;
    bool eos() const override { return _eos; }
    int64 size() const override { return _size; }
    int64 pos() const override { return _pos; }
    bool seek(int64, int = SEEK_SET) override;
};
class WriteStream {
  public:
    virtual ~WriteStream() = default;
    virtual uint32 write(const void*, uint32) = 0;
    virtual int64 pos() const = 0;
    virtual bool err() const { return false; }
    void writeSint32BE(int32 x) { writeUint32BE(x); }
    void writeSByte(int8 x) { writeByte(x); }
    void writeByte(byte x) { write(&x, 1); }
    void writeUint16LE(uint16 x) {
        byte b[2];
        WRITE_LE_UINT16(b, x);
        write(b, 2);
    }
    void writeSint16LE(int16 x) { writeUint16LE(x); }
    void writeUint32LE(uint32 x) {
        byte b[4];
        WRITE_LE_UINT32(b, x);
        write(b, 4);
    }
    void writeSint32LE(int32 x) { writeUint32LE(x); }
    void writeUint32BE(uint32 x) {
        byte b[4];
        WRITE_BE_UINT32(b, x);
        write(b, 4);
    }
};
class MemoryWriteStreamDynamic : public WriteStream {
  protected:
    std::vector<byte> _bytes;

  public:
    explicit MemoryWriteStreamDynamic(DisposeAfterUse = DisposeAfterUse::YES) {}
    uint32 write(const void* p, uint32 n) override {
        if (n) {
            auto b = static_cast<const byte*>(p);
            _bytes.insert(_bytes.end(), b, b + n);
        }
        return n;
    }
    int64 pos() const override { return _bytes.size(); }
    int64 size() const { return _bytes.size(); }
    byte* getData() { return _bytes.data(); }
};
class File : public MemoryReadStream {
    std::vector<byte> _storage;
    bool _opened = false;

  public:
    File() : MemoryReadStream(nullptr, 0) {}
    bool open(const Path& name);
    bool isOpen() const { return _opened; }
    void close() {
        _storage.clear();
        _data = nullptr;
        _size = _pos = 0;
        _opened = false;
    }
    static bool exists(const Path& name);
};
using InSaveFile = SeekableReadStream;
class OutSaveFile : public MemoryWriteStreamDynamic {
  protected:
    Path _path;

  public:
    explicit OutSaveFile(const Path& path) : _path(path) {}
    void finalize();
};
class DumpFile : public OutSaveFile {
  public:
    DumpFile() : OutSaveFile(Path()) {}
    bool open(const Path& p, bool = false);
    void flush() { finalize(); }
    void close() {}
};
class SaveFileManager {
  public:
    Path directory;
    InSaveFile* openForLoading(const String& name);
    OutSaveFile* openForSaving(const String& name, bool = false);
};
class Config {
    std::map<std::string, std::string> _values;

  public:
    Path file;
    bool hasKey(const std::string& key) const { return _values.contains(key); }
    bool getBool(const std::string& key) const {
        auto i = _values.find(key);
        return i != _values.end() && (i->second == "true" || i->second == "1");
    }
    int getInt(const std::string& key) const {
        auto i = _values.find(key);
        return i == _values.end() ? 0 : std::stoi(i->second);
    }
    Path getPath(const std::string& key) const {
        auto i = _values.find(key);
        return i == _values.end() ? Path() : Path(i->second);
    }
    void setBool(const std::string& key, bool b) { _values[key] = b ? "true" : "false"; }
    void setInt(const std::string& key, int n) { _values[key] = std::to_string(n); }
    void setPath(const std::string& key, const Path& p) { _values[key] = p.string(); }
    void load();
    void flushToDisk();
};
class RandomSource {
    uint32 _seed = 1;
    void scramble() {
        _seed ^= _seed >> 13;
        _seed ^= _seed << 21;
        _seed ^= _seed >> 11;
    }

  public:
    explicit RandomSource(const char*);
    void setSeed(uint32 x) { _seed = x ? x : 1; }
    uint32 getSeed() const { return _seed; }
    uint getRandomNumber(uint max) {
        scramble();
        auto n = _seed * 0xDEADBF03u;
        return max == UINT_MAX ? n : n % (max + 1);
    }
    uint getRandomBit() {
        scramble();
        return _seed & 1;
    }
    uint getRandomNumberRng(uint min, uint max) { return min + getRandomNumber(max - min); }
};
struct Point {
    int16 x = 0, y = 0;
};
struct Rect {
    int16 left, top, right, bottom;
    Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
};
struct PixelFormat {
    static PixelFormat createFormatCLUT8() { return {}; }
};
struct Surface {
    int w = 0, h = 0, pitch = 0;
    PixelFormat format;
    std::vector<byte> storage;
    void* pixels = nullptr;
    void create(int width, int height, PixelFormat f) {
        w = width;
        h = height;
        pitch = w;
        format = f;
        storage.assign(w * h, 0);
        pixels = storage.data();
    }
    void free() {
        storage.clear();
        pixels = nullptr;
    }
    void setPixels(void* p) { pixels = p; }
    void* getPixels() const { return pixels; }
    void* getBasePtr(int x, int y) const { return static_cast<byte*>(pixels) + y * pitch + x; }
    void copyRectToSurface(const void* src, int srcPitch, int x, int y, int width, int height);
    void debugPrint() const {}
};
enum EventType {
    EVENT_INVALID,
    EVENT_MOUSEMOVE,
    EVENT_LBUTTONDOWN,
    EVENT_RBUTTONDOWN,
    EVENT_LBUTTONUP,
    EVENT_RBUTTONUP,
    EVENT_KEYDOWN,
    EVENT_QUIT
};
enum {
    KBD_SHIFT = 1,
    KBD_CTRL = 2,
    KBD_ALT = 4,
    ASCII_ESCAPE = 27,
    ASCII_BACKSPACE = 8,
    ASCII_TAB = 9,
    ASCII_RETURN = 13
};
enum KeyCode {
    KEYCODE_SPACE = ' ',
    KEYCODE_z = 'z',
    KEYCODE_UP = 256,
    KEYCODE_DOWN,
    KEYCODE_LEFT,
    KEYCODE_RIGHT,
    KEYCODE_HOME,
    KEYCODE_END,
    KEYCODE_PAGEUP,
    KEYCODE_PAGEDOWN,
    KEYCODE_DELETE,
    KEYCODE_KP_EQUALS
};
struct Event {
    EventType type = EVENT_INVALID;
    Point mouse;
    struct {
        int ascii = 0, keycode = 0, flags = 0;
    } kbd;
};
struct GameFileDescription {
    const char* fileName;
    int fileType;
};
class TimerManager {
    struct Timer {
        void (*fn)(void*);
        void* arg;
        uint64 interval, next;
    };
    std::vector<Timer> _timers;

  public:
    void installTimerProc(void (*fn)(void*), int interval, void* arg, const char*);
    void removeTimerProc(void (*fn)(void*));
    void checkTimers();
};
class Palette {
  public:
    std::array<byte, 768> colors{};
    void setPalette(const byte* p, int start, int n) {
        std::memcpy(colors.data() + start * 3, p, n * 3);
    }
    void grabPalette(byte* p, int start, int n) {
        std::memcpy(p, colors.data() + start * 3, n * 3);
    }
};
class Cursor {
  public:
    bool visible = false;
    int w = 0, h = 0, hotX = 0, hotY = 0, key = 0;
    std::vector<byte> pixels;
    void showMouse(bool v) { visible = v; }
    void replaceCursor(const void* p, int width, int height, int x, int y, int transparent) {
        w = width;
        h = height;
        hotX = x;
        hotY = y;
        key = transparent;
        auto b = static_cast<const byte*>(p);
        pixels.assign(b, b + w * h);
    }
};
class Platform;
extern Path dataDirectory, resourceDirectory;
Path resolveDataFile(const Path& name);
} // namespace Native
extern Native::Config ConfMan;
extern Native::Cursor CursorMan;
namespace Audio {
enum { FLAG_UNSIGNED = 1, FLAG_16BITS = 2, FLAG_LITTLE_ENDIAN = 4 };
struct AudioStream {
    std::vector<float> samples;
    int rate = 22050, loops = 1;
    bool loop = false;
};
using SeekableAudioStream = AudioStream;
struct SoundHandle {
    uint64 id = 0;
};
SeekableAudioStream* makeRawStream(const byte*, uint32, int, int,
                                   DisposeAfterUse = DisposeAfterUse::YES);
SeekableAudioStream* makeRawStream(Native::SeekableReadStream*, int, int);
AudioStream* makeLoopingAudioStream(SeekableAudioStream*, int);
class Mixer {
    struct Channel {
        SoundHandle handle;
        std::unique_ptr<AudioStream> data;
        double position = 0;
        int volume = 255;
        int type = 0;
    };
    SDL_AudioStream* _output = nullptr;
    std::vector<Channel> _channels;
    std::mutex _mutex;
    uint64 _id = 0;
    bool _paused = false;
    std::function<void(float*, int)> _music;
    uint64 _musicNonzero = 0, _effectsNonzero = 0;
    bool _deterministic = false;
    static void SDLCALL callback(void*, SDL_AudioStream*, int, int);
    void render(float*, int);

  public:
    enum { kSFXSoundType, kSpeechSoundType, kMusicSoundType, kMaxMixerVolume = 255 };
    Mixer();
    ~Mixer();
    void playStream(int, SoundHandle*, AudioStream*, int = -1, int = 255);
    void stopHandle(SoundHandle);
    bool isSoundHandleActive(SoundHandle);
    void setChannelVolume(SoundHandle, int);
    void pauseAll(bool);
    void syncSoundSettings() {}
    void setMusic(std::function<void(float*, int)>);
    void enableDeterministic();
    void advance(int frames);
    std::pair<uint64, uint64> sampleCounts();
};
} // namespace Audio
namespace Native {
class Platform {
    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    SDL_Texture* _texture = nullptr;
    Surface _screen;
    Palette _palette;
    std::vector<uint32> _rgba;
    std::unique_ptr<Audio::Mixer> _mixer;
    int _mouseX = 0, _mouseY = 0;

  public:
    bool quitting = false, deterministic = false;
    uint64 virtualMicros = 0;
    TimerManager timers;
    SaveFileManager saves;
    Platform();
    ~Platform();
    void initGraphics(int, int);
    int getWidth() const { return _screen.w; }
    int getHeight() const { return _screen.h; }
    Surface* lockScreen() { return &_screen; }
    void unlockScreen() {}
    Palette* getPaletteManager() { return &_palette; }
    Audio::Mixer* getMixer() { return _mixer.get(); }
    SaveFileManager* getSavefileManager() { return &saves; }
    uint32 getMillis() {
        return deterministic ? uint32(virtualMicros / 1000) : uint32(SDL_GetTicks());
    }
    void delayMillis(uint32 n);
    bool pollEvent(Event&);
    int getModifierState() const;
    void copyRectToScreen(const void*, int, int, int, int, int);
    void fillScreen(const Rect&, uint32);
    void updateScreen();
    Point windowToGame(float, float) const;
    void capture(const Path&);
};
} // namespace Native
extern Native::Platform* g_platform;
