// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/runtime.h"
#include "platform/diagnostics.h"
#include "app-icon.h"
#include <chrono>
Native::Config ConfMan;
Native::Cursor CursorMan;
Native::Platform* g_platform = nullptr;
namespace {
std::string message(const char* fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(nullptr, 0, fmt, cp);
    va_end(cp);
    if (n < 0)
        return "Formatting error";
    std::vector<char> b(n + 1);
    vsnprintf(b.data(), b.size(), fmt, ap);
    return b.data();
}
} // namespace
void warning(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto s = message(fmt, ap);
    va_end(ap);
    Native::diagnosticMessage("WARNING", "%s", s.c_str());
    fprintf(stderr, "Warning: %s\n", s.c_str());
}
[[noreturn]] void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto s = message(fmt, ap);
    va_end(ap);
    Native::diagnosticMessage("ERROR", "%s", s.c_str());
    throw std::runtime_error(s);
}
void debug(const char*, ...) {}
void debug(int, const char*, ...) {}
void debugC(int, int, const char*, ...) {}
void debugCN(int, int, const char*, ...) {}
namespace Native {
static uint64 timerMicros() {
    return g_platform && g_platform->deterministic ? g_platform->virtualMicros
                                                   : SDL_GetTicksNS() / 1000;
}
Path dataDirectory, resourceDirectory;
String String::format(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    auto s = message(fmt, ap);
    va_end(ap);
    return s;
}
bool String::equalsIgnoreCase(const String& s) const {
    return size() == s.size() &&
           std::equal(begin(), end(), s.begin(), [](unsigned char a, unsigned char b) {
               return std::tolower(a) == std::tolower(b);
           });
}
size_t strlcpy(char* dest, const char* src, size_t cap) {
    size_t n = strlen(src);
    if (cap) {
        size_t k = std::min(n, cap - 1);
        memcpy(dest, src, k);
        dest[k] = 0;
    }
    return n;
}
size_t strlcat(char* dest, const char* src, size_t cap) {
    size_t n = 0;
    while (n < cap && dest[n])
        ++n;
    if (n == cap)
        return cap + strlen(src);
    return n + strlcpy(dest + n, src, cap - n);
}
void hexdump(const byte*, int, int, int) {}
uint32 MemoryReadStream::read(void* p, uint32 n) {
    size_t count = std::min<size_t>(n, _size - _pos);
    if (count)
        memcpy(p, _data + _pos, count);
    _pos += count;
    if (count < n)
        _eos = true;
    return count;
}
bool MemoryReadStream::seek(int64 offset, int whence) {
    int64 p = offset + (whence == SEEK_CUR ? int64(_pos) : whence == SEEK_END ? int64(_size) : 0);
    if (p < 0 || uint64(p) > _size)
        return false;
    _pos = size_t(p);
    _eos = false;
    return true;
}
SeekableReadStream* SeekableReadStream::readStream(uint32 n) {
    auto p = static_cast<byte*>(malloc(n));
    if (!p && n)
        throw std::bad_alloc();
    auto actual = read(p, n);
    return new MemoryReadStream(p, actual, DisposeAfterUse::YES);
}
Path resolveDataFile(const Path& name) {
    // Only explicitly rooted game and cache files are searched. No global cwd lookup.
    if (name.is_absolute())
        return name;
    for (const auto& base : {resourceDirectory, dataDirectory, Path(dataDirectory / "DRIVERS"),
                             Path(dataDirectory / "drivers"), Path(dataDirectory / "res")}) {
        auto direct = base / name;
        if (std::filesystem::is_regular_file(direct))
            return direct;
        Path current = base;
        bool found = true;
        for (const auto& component : name) {
            if (component == "..") {
                found = false;
                break;
            }
            bool matched = false;
            if (std::filesystem::is_directory(current))
                for (const auto& entry : std::filesystem::directory_iterator(current)) {
                    if (Path(entry.path().filename())
                            .toString()
                            .equalsIgnoreCase(Path(component).toString())) {
                        current = entry.path();
                        matched = true;
                        break;
                    }
                }
            if (!matched) {
                found = false;
                break;
            }
        }
        if (found && std::filesystem::is_regular_file(current))
            return current;
    }
    return {};
}
bool File::open(const Path& name) {
    close();
    auto path = resolveDataFile(name);
    std::ifstream in(path.c_str(), std::ios::binary | std::ios::ate);
    if (!in)
        return false;
    auto n = in.tellg();
    if (n < 0 || n > INT_MAX)
        throw std::runtime_error("Invalid file size: " + path.toString());
    _storage.resize(size_t(n));
    in.seekg(0);
    if (n && !in.read(reinterpret_cast<char*>(_storage.data()), n))
        return false;
    _data = _storage.data();
    _size = _storage.size();
    _opened = true;
    return true;
}
bool File::exists(const Path& name) { return !resolveDataFile(name).empty(); }
void OutSaveFile::finalize() {
    if (_path.empty())
        throw std::runtime_error("Missing output path");
    auto temporary = _path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary.c_str(), std::ios::binary | std::ios::trunc);
        if (!out.write(reinterpret_cast<const char*>(_bytes.data()), _bytes.size()) || !out.flush())
            throw std::runtime_error("Cannot write save: " + _path.toString());
    }
    // SDL implements replacement atomically on supported desktop systems.
    if (!SDL_RenamePath(temporary.toString().c_str(), _path.toString().c_str()))
        throw std::runtime_error("Cannot finalize save: " + std::string(SDL_GetError()));
}
InSaveFile* SaveFileManager::openForLoading(const String& name) {
    if (std::filesystem::path(std::string(name)).filename() !=
        std::filesystem::path(std::string(name)))
        return nullptr;
    std::ifstream in(directory / std::string(name), std::ios::binary | std::ios::ate);
    if (!in)
        return nullptr;
    auto n = in.tellg();
    if (n < 0 || n > 64 * 1024 * 1024)
        return nullptr;
    auto p = static_cast<byte*>(malloc(size_t(n)));
    if (n && !in.seekg(0).read(reinterpret_cast<char*>(p), n)) {
        free(p);
        return nullptr;
    }
    return new MemoryReadStream(p, uint32(n), DisposeAfterUse::YES);
}
OutSaveFile* SaveFileManager::openForSaving(const String& name, bool) {
    if (std::filesystem::path(std::string(name)).filename() !=
        std::filesystem::path(std::string(name)))
        return nullptr;
    return new OutSaveFile(Path(directory / std::string(name)));
}
void Config::load() {
    std::ifstream in(file.c_str());
    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos != std::string::npos)
            _values[line.substr(0, pos)] = line.substr(pos + 1);
    }
}
void Config::flushToDisk() {
    if (file.empty())
        return;
    String text;
    for (auto& [key, val] : _values)
        if (key != "path" && key != "save_slot" && key != "native_probe" && key != "random_seed")
            text += key + "=" + val + "\n";
    OutSaveFile out(file);
    out.write(text.data(), text.size());
    out.finalize();
}
RandomSource::RandomSource(const char*) {
    setSeed(ConfMan.hasKey("random_seed")
                ? ConfMan.getInt("random_seed")
                : uint32(std::chrono::system_clock::now().time_since_epoch().count()));
}
void Surface::copyRectToSurface(const void* src, int srcPitch, int x, int y, int width,
                                int height) {
    for (int row = 0; row < height; ++row)
        memcpy(getBasePtr(x, y + row), static_cast<const byte*>(src) + row * srcPitch, width);
}
void TimerManager::installTimerProc(void (*fn)(void*), int interval, void* arg, const char*) {
    if (interval <= 0)
        throw std::runtime_error("Invalid timer interval");
    removeTimerProc(fn);
    _timers.push_back({fn, arg, uint64(interval), timerMicros() + uint64(interval)});
}
void TimerManager::removeTimerProc(void (*fn)(void*)) {
    std::erase_if(_timers, [fn](const Timer& t) { return t.fn == fn; });
}
void TimerManager::checkTimers() {
    uint64 now = timerMicros();
    // Callbacks may alter the timer list. Advance first and locate it again afterwards.
    for (;;) {
        auto it = std::find_if(_timers.begin(), _timers.end(),
                               [now](const Timer& t) { return t.next <= now; });
        if (it == _timers.end())
            break;
        auto fn = it->fn;
        auto arg = it->arg;
        it->next += it->interval;
        fn(arg);
    }
}
Platform::Platform() {
    SDL_SetHint(SDL_HINT_APP_ID, "fta2-native");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
        throw std::runtime_error(SDL_GetError());
    g_platform = this;
    _mixer = std::make_unique<Audio::Mixer>();
}
Platform::~Platform() {
    _mixer.reset();
    SDL_DestroyTexture(_texture);
    SDL_DestroyRenderer(_renderer);
    SDL_DestroyWindow(_window);
    SDL_Quit();
    g_platform = nullptr;
}
void Platform::initGraphics(int width, int height) {
    _automaticWidth = width;
    _automaticHeight = height;
    _window = SDL_CreateWindow("Faery Tale Adventure II", width, height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                   SDL_WINDOW_FULLSCREEN);
    if (!_window)
        error("Cannot create window: %s", SDL_GetError());
    auto* iconStream = SDL_IOFromConstMem(kApplicationIcon, sizeof(kApplicationIcon));
    auto* icon = iconStream ? SDL_LoadPNG_IO(iconStream, true) : nullptr;
    if (icon) {
        SDL_SetWindowIcon(_window, icon);
        SDL_DestroySurface(icon);
    } else {
        warning("Cannot load application icon: %s", SDL_GetError());
    }
    _renderer = SDL_CreateRenderer(_window, nullptr);
    if (!_renderer)
        error("Cannot create renderer: %s", SDL_GetError());
    SDL_SetRenderLogicalPresentation(_renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (!resizeGraphics(width, height))
        error("Cannot create framebuffer: %s", SDL_GetError());
    SDL_HideCursor();
}
bool Platform::resizeGraphics(int width, int height) {
    if (width <= 0 || height <= 0 || width > 32767 || height > 32767)
        return SDL_SetError("Invalid framebuffer dimensions");
    Surface screen;
    std::vector<uint32> rgba;
    try {
        screen.create(width, height, {});
        rgba.resize(size_t(width) * height);
    } catch (const std::bad_alloc&) {
        return SDL_SetError("Cannot allocate framebuffer");
    }
    auto* texture = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture)
        return false;
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) ||
        !SDL_SetRenderLogicalPresentation(_renderer, width, height,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_DestroyTexture(texture);
        return false;
    }
    observeWindowSize();
    SDL_DestroyTexture(_texture);
    _texture = texture;
    _screen = std::move(screen);
    _rgba = std::move(rgba);
    applyAutomaticWindowSize();
    refreshMousePosition();
    return true;
}
void Platform::observeWindowSize() {
    if (SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN)
        return;
    if (!SDL_SyncWindow(_window))
        warning("Cannot synchronize window state: %s", SDL_GetError());
    int width, height;
    if (!SDL_GetWindowSize(_window, &width, &height)) {
        warning("Cannot read window size: %s", SDL_GetError());
        return;
    }
    if ((SDL_GetWindowFlags(_window) & SDL_WINDOW_MAXIMIZED) || width != _automaticWidth ||
        height != _automaticHeight)
        _customWindowSize = true;
}
void Platform::applyAutomaticWindowSize() {
    if (_customWindowSize || (SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN))
        return;
    if (!SDL_SetWindowSize(_window, _screen.w, _screen.h)) {
        warning("Cannot resize window: %s", SDL_GetError());
        return;
    }
    if (!SDL_SyncWindow(_window))
        warning("Cannot synchronize window size: %s", SDL_GetError());
    // Desktop constraints may accept a different size. Compare future user
    // changes against the accepted dimensions, never fullscreen monitor bounds.
    if (!SDL_GetWindowSize(_window, &_automaticWidth, &_automaticHeight))
        warning("Cannot read resized window size: %s", SDL_GetError());
}
void Platform::toggleFullscreen() {
    const bool fullscreen = SDL_GetWindowFlags(_window) & SDL_WINDOW_FULLSCREEN;
    if (!fullscreen)
        observeWindowSize();
    if (!SDL_SetWindowFullscreen(_window, !fullscreen)) {
        warning("Cannot change window state: %s", SDL_GetError());
        return;
    }
    if (!SDL_SyncWindow(_window))
        warning("Cannot synchronize window state: %s", SDL_GetError());
    if (fullscreen)
        applyAutomaticWindowSize();
    refreshMousePosition();
}
void Platform::refreshMousePosition() {
    float x, y;
    SDL_GetMouseState(&x, &y);
    auto mouse = windowToGame(x, y);
    _mouseX = mouse.x;
    _mouseY = mouse.y;
}
void Platform::copyRectToScreen(const void* src, int stride, int x, int y, int width, int height) {
    auto b = static_cast<const byte*>(src);
    int left = MAX(0, x), top = MAX(0, y), right = MIN(_screen.w, x + width),
        bottom = MIN(_screen.h, y + height);
    for (int row = top; row < bottom; ++row)
        if (right > left)
            memcpy(_screen.getBasePtr(left, row), b + (row - y) * stride + left - x, right - left);
}
void Platform::fillScreen(const Rect& r, uint32 color) {
    for (int y = MAX(0, r.top); y < MIN(_screen.h, r.bottom); ++y)
        for (int x = MAX(0, r.left); x < MIN(_screen.w, r.right); ++x)
            static_cast<byte*>(_screen.pixels)[y * _screen.pitch + x] = color;
}
void Platform::updateScreen() {
    if (!_texture)
        return;
    auto bytes = reinterpret_cast<byte*>(_rgba.data());
    for (int y = 0; y < _screen.h; ++y)
        for (int x = 0; x < _screen.w; ++x) {
            int index = static_cast<byte*>(_screen.pixels)[y * _screen.pitch + x];
            if (CursorMan.visible) {
                int cx = x - _mouseX + CursorMan.hotX, cy = y - _mouseY + CursorMan.hotY;
                if (cx >= 0 && cy >= 0 && cx < CursorMan.w && cy < CursorMan.h) {
                    byte v = CursorMan.pixels[cy * CursorMan.w + cx];
                    if (v != CursorMan.key)
                        index = v;
                }
            }
            size_t p = (y * _screen.w + x) * 4;
            memcpy(bytes + p, _palette.colors.data() + index * 3, 3);
            bytes[p + 3] = 255;
        }
    SDL_UpdateTexture(_texture, nullptr, _rgba.data(), _screen.w * 4);
    SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
    SDL_RenderClear(_renderer);
    SDL_RenderTexture(_renderer, _texture, nullptr, nullptr);
    SDL_RenderPresent(_renderer);
}
Point Platform::windowToGame(float x, float y) const {
    float gx = 0, gy = 0;
    SDL_RenderCoordinatesFromWindow(_renderer, x, y, &gx, &gy);
    return {int16(std::floor(gx)), int16(std::floor(gy))};
}
int Platform::getModifierState() const {
    auto mod = SDL_GetModState();
    return (mod & SDL_KMOD_SHIFT ? KBD_SHIFT : 0) | (mod & SDL_KMOD_CTRL ? KBD_CTRL : 0) |
           (mod & SDL_KMOD_ALT ? KBD_ALT : 0);
}
bool Platform::pollEvent(Event& out) {
    timers.checkTimers();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        out = {};
        if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            out.type = EVENT_FOCUSLOST;
            return true;
        }
        if (ev.type == SDL_EVENT_WINDOW_RESIZED || ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            refreshMousePosition();
        if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat)
            diagnosticMessage("INPUT", "key=%u modifiers=%u", unsigned(ev.key.key),
                              unsigned(ev.key.mod));
        if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            diagnosticMessage("INPUT", "button=%u window_position=(%.1f,%.1f)",
                              unsigned(ev.button.button), ev.button.x, ev.button.y);
        if (ev.type == SDL_EVENT_QUIT) {
            diagnosticMessage("INPUT", "Window close requested");
            quitting = true;
            out.type = EVENT_QUIT;
            return true;
        }
        if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
            ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            auto p = ev.type == SDL_EVENT_MOUSE_MOTION ? windowToGame(ev.motion.x, ev.motion.y)
                                                       : windowToGame(ev.button.x, ev.button.y);
            _mouseX = p.x;
            _mouseY = p.y;
            out.mouse = p;
            if (ev.type == SDL_EVENT_MOUSE_MOTION)
                out.type = EVENT_MOUSEMOVE;
            else if (ev.button.button == SDL_BUTTON_LEFT)
                out.type =
                    ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EVENT_LBUTTONDOWN : EVENT_LBUTTONUP;
            else if (ev.button.button == SDL_BUTTON_RIGHT)
                out.type =
                    ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EVENT_RBUTTONDOWN : EVENT_RBUTTONUP;
            else
                continue;
            return true;
        }
        if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
            out.type = EVENT_MOUSEWHEEL;
            out.mouse = windowToGame(ev.wheel.mouse_x, ev.wheel.mouse_y);
            out.wheelY = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
            return true;
        }
        if (ev.type == SDL_EVENT_KEY_DOWN) {
            if ((ev.key.key == SDLK_RETURN || ev.key.key == SDLK_KP_ENTER) &&
                (ev.key.mod & SDL_KMOD_ALT)) {
                // Consume the shortcut, including repeats, before the game UI.
                if (!ev.key.repeat)
                    toggleFullscreen();
                continue;
            }
            out.type = EVENT_KEYDOWN;
            out.kbd.flags = getModifierState();
            auto k = ev.key.key;
            // SDL keypad keycodes are outside ASCII; share the ordinary digit controls.
            if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
                k = '1' + (k - SDLK_KP_1);
            else if (k == SDLK_KP_0)
                k = '0';
            out.kbd.ascii = k < 128 ? int(k) : 0;
            out.kbd.keycode = out.kbd.ascii;
            if ((out.kbd.flags & KBD_SHIFT) && out.kbd.ascii >= 'a' && out.kbd.ascii <= 'z')
                out.kbd.ascii -= 32;
            switch (k) {
            case SDLK_UP:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_UP;
                break;
            case SDLK_DOWN:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_DOWN;
                break;
            case SDLK_LEFT:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_LEFT;
                break;
            case SDLK_RIGHT:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_RIGHT;
                break;
            case SDLK_HOME:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_HOME;
                break;
            case SDLK_END:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_END;
                break;
            case SDLK_PAGEUP:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_PAGEUP;
                break;
            case SDLK_PAGEDOWN:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_PAGEDOWN;
                break;
            case SDLK_DELETE:
                out.kbd.ascii = out.kbd.keycode = KEYCODE_DELETE;
                break;
            default:
                break;
            }
            return true;
        }
    }
    return false;
}
void Platform::delayMillis(uint32 n) {
    timers.checkTimers();
    if (deterministic) {
        virtualMicros += uint64(n) * 1000;
        _mixer->advance(n * 48);
    } else
        SDL_Delay(n);
    timers.checkTimers();
}
void Platform::capture(const Path& p) {
    std::ofstream out(p.c_str(), std::ios::binary);
    out << "P6\n" << _screen.w << ' ' << _screen.h << "\n255\n";
    for (int y = 0; y < _screen.h; ++y)
        for (int x = 0; x < _screen.w; ++x) {
            auto index = static_cast<byte*>(_screen.pixels)[y * _screen.pitch + x];
            out.write(reinterpret_cast<char*>(_palette.colors.data() + index * 3), 3);
        }
}
} // namespace Native
namespace Audio {
SeekableAudioStream* makeRawStream(const byte* p, uint32 n, int rate, int flags,
                                   DisposeAfterUse dispose) {
    auto stream = new AudioStream;
    stream->rate = rate;
    stream->channels = flags & FLAG_STEREO ? 2 : 1;
    int step = flags & FLAG_16BITS ? 2 : 1;
    stream->samples.reserve(n / step);
    for (uint32 i = 0; i + step <= n; i += step) {
        int v;
        if (step == 2) {
            v = flags & FLAG_LITTLE_ENDIAN ? READ_LE_UINT16(p + i) : READ_BE_UINT16(p + i);
            v = flags & FLAG_UNSIGNED ? v - 32768 : int16(v);
        } else {
            v = flags & FLAG_UNSIGNED ? int(p[i]) - 128 : int8(p[i]);
            v *= 256;
        }
        stream->samples.push_back(v / 32768.0f);
    }
    if (dispose == DisposeAfterUse::YES)
        std::free(const_cast<byte*>(p));
    return stream;
}
SeekableAudioStream* makeRawStream(Native::SeekableReadStream* in, int rate, int flags) {
    std::unique_ptr<Native::SeekableReadStream> owned(in);
    std::vector<byte> bytes(in->size() - in->pos());
    in->read(bytes.data(), bytes.size());
    return makeRawStream(bytes.data(), bytes.size(), rate, flags, DisposeAfterUse::NO);
}
AudioStream* makeLoopingAudioStream(SeekableAudioStream* s, int count) {
    s->loop = count == 0;
    s->loops = MAX(1, count);
    return s;
}
Mixer::Mixer() {
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    _output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, this);
    if (_output)
        SDL_ResumeAudioStreamDevice(_output);
    else
        warning("Audio output unavailable: %s", SDL_GetError());
}
Mixer::~Mixer() { SDL_DestroyAudioStream(_output); }
void SDLCALL Mixer::callback(void* user, SDL_AudioStream* stream, int additional, int) {
    if (additional <= 0)
        return;
    int frames = (additional + 7) / 8;
    std::vector<float> data(frames * 2);
    static_cast<Mixer*>(user)->render(data.data(), frames);
    SDL_PutAudioStreamData(stream, data.data(), frames * 8);
}
void Mixer::render(float* out, int frames) {
    std::lock_guard lock(_mutex);
    std::fill(out, out + frames * 2, 0);
    if (_paused)
        return;
    if (_music)
        _music(out, frames);
    for (int i = 0; i < frames * 2; ++i)
        if (out[i] != 0)
            ++_musicNonzero;
    for (auto& c : _channels) {
        float volume = float(c.volume) / 255;
        if (c.type == kSpeechSoundType)
            volume *= float(_speechVolume) / 255;
        for (int f = 0; f < frames; ++f) {
            size_t index = size_t(c.position);
            const size_t frameCount = c.data->samples.size() / c.data->channels;
            if (index >= frameCount) {
                if (c.data->samples.empty() || (!c.data->loop && c.data->loops <= 1))
                    break;
                if (!c.data->loop)
                    --c.data->loops;
                c.position = std::fmod(c.position, double(frameCount));
                index = size_t(c.position);
            }
            const size_t sample = index * c.data->channels;
            float left = c.data->samples[sample] * volume;
            float right = c.data->samples[sample + c.data->channels - 1] * volume;
            if (left != 0 || right != 0)
                ++_effectsNonzero;
            out[f * 2] += left;
            out[f * 2 + 1] += right;
            c.position += double(c.data->rate) / 48000;
        }
    }
    std::erase_if(_channels, [](const Channel& c) {
        return !c.data->loop && c.data->loops <= 1 &&
               c.position >= c.data->samples.size() / c.data->channels;
    });
    for (int i = 0; i < frames * 2; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
}
void Mixer::playStream(int type, SoundHandle* handle, AudioStream* stream, int, int volume) {
    std::lock_guard lock(_mutex);
    if (!stream)
        return;
    handle->id = ++_id;
    _channels.push_back({*handle, std::unique_ptr<AudioStream>(stream), 0, volume, type});
}
void Mixer::stopHandle(SoundHandle h) {
    std::lock_guard lock(_mutex);
    std::erase_if(_channels, [h](const Channel& c) { return c.handle.id == h.id; });
}
bool Mixer::isSoundHandleActive(SoundHandle h) {
    std::lock_guard lock(_mutex);
    return std::any_of(_channels.begin(), _channels.end(),
                       [h](const Channel& c) { return c.handle.id == h.id; });
}
void Mixer::setChannelVolume(SoundHandle h, int v) {
    std::lock_guard lock(_mutex);
    for (auto& c : _channels)
        if (c.handle.id == h.id)
            c.volume = v;
}
void Mixer::pauseAll(bool v) {
    std::lock_guard lock(_mutex);
    _paused = v;
}
void Mixer::syncSoundSettings() {
    const int volume = ConfMan.hasKey("speech_volume") ? ConfMan.getInt("speech_volume") : 255;
    const bool muted = ConfMan.getBool("speech_mute");
    std::lock_guard lock(_mutex);
    _speechVolume = muted ? 0 : std::clamp(volume, 0, 255);
}
void Mixer::setMusic(std::function<void(float*, int)> fn) {
    std::lock_guard lock(_mutex);
    _music = std::move(fn);
}
} // namespace Audio

const char* tag2str(uint32 id) {
    thread_local char buffers[8][17];
    thread_local int slot = 0;
    auto out = buffers[(slot++) % 8];
    for (int i = 0; i < 4; ++i) {
        byte c = id >> (24 - i * 8);
        out[i] = c >= 32 && c < 127 ? c : '.';
    }
    out[4] = 0;
    return out;
}
const char* tag2strP(uint32 id) { return tag2str(id); }
bool Native::DumpFile::open(const Path& p, bool) {
    _path = g_platform->saves.directory / "dumps" / p.filename();
    std::filesystem::create_directories(_path.parent_path());
    return true;
}

void Audio::Mixer::enableDeterministic() {
    if (_output)
        SDL_PauseAudioStreamDevice(_output);
    std::lock_guard lock(_mutex);
    _deterministic = true;
}
std::vector<float> Audio::Mixer::advance(int frames) {
    std::vector<float> buffer(frames * 2);
    render(buffer.data(), frames);
    return buffer;
}
std::pair<uint64, uint64> Audio::Mixer::sampleCounts() {
    std::lock_guard lock(_mutex);
    return {_musicNonzero, _effectsNonzero};
}
