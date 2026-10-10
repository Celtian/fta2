// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/resources.h"
#include "platform/startup.h"
#include "saga2/saga2.h"

#include "saga2/audio.h"
#include "saga2/button.h"
#include "saga2/gfx.h"
#include "saga2/gpointer.h"
#include "saga2/idtypes.h"
#include "saga2/saveload.h"
#include "saga2/tcoords.h"
namespace Saga2 {
extern gDisplayPort* globalPort;
}
using namespace Native;
static void check(bool condition, const char* what) {
    if (!condition)
        throw std::runtime_error(what);
}
static void mustFail(const std::function<void()>& fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    check(failed, "Malformed input was accepted");
}
int main() {
    try {
        {
            Saga2::gDisplayPort port;
            Saga2::gMousePointer pointer(port);
            byte pixels[] = {0, 1, 2, 3};
            Saga2::gPixelMap image;
            image._size = Saga2::Point16(2, 2);
            image._data = pixels;
            pointer.setImage(image, -1, -1);
            pixels[1] = 4;
            pointer.setImage(image, -1, -1);
            check(CursorMan.pixels == std::vector<byte>({0, 4, 2, 3}) && CursorMan.hotX == 1 &&
                      CursorMan.hotY == 1,
                  "Cursor pixels must refresh when the bitmap address and size are reused");
        }
        check(sha256(nullptr, 0) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "SHA256 empty");
        check(sha256(reinterpret_cast<const byte*>("abc"), 3) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256 abc");
        std::vector<byte> million(1000000, 'a');
        check(sha256(million.data(), million.size()) ==
                  "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "SHA256 long blocks");
        MemoryWriteStreamDynamic out;
        out.writeUint16LE(0xFEDC);
        out.writeSint32BE(-123456);
        out.writeSByte(-7);
        MemoryReadStream in(out.getData(), out.size());
        check(in.readUint16LE() == 0xFEDC && in.readSint32BE() == -123456 && in.readSByte() == -7,
              "Stream endian round trip");
        check(!in.eos() && in.pos() == 7, "EOF must be set by over-read");
        check(in.readByte() == 0 && in.eos(), "EOF over-read");
        check(!in.seek(-1), "Negative seek accepted");
        check(in.seek(-1, SEEK_END) && in.readSByte() == -7, "Seek end");
        std::array<byte, 128> saveHeader;
        saveHeader.fill('x');
        memcpy(saveHeader.data(), "FTA2", 4);
        MemoryReadStream headerInput(saveHeader.data(), saveHeader.size());
        Saga2::SaveFileHeader header;
        header.read(&headerInput);
        check(header.saveName == String(40, 'x'), "Save name must be bounded by its header field");
        std::vector<byte> hrs(39);
        memcpy(hrs.data(), "HRES", 4);
        WRITE_LE_UINT32(hrs.data() + 4, 12);
        WRITE_LE_UINT32(hrs.data() + 8, 12);
        WRITE_LE_UINT32(hrs.data() + 16, 24);
        WRITE_LE_UINT32(hrs.data() + 20, 12);
        WRITE_LE_UINT32(hrs.data() + 28, 36);
        WRITE_LE_UINT32(hrs.data() + 32, 3);
        validateHrs(hrs.data(), hrs.size());
        mustFail([&] { validateHrs(hrs.data(), 38); });
        WRITE_LE_UINT32(hrs.data() + 4, 0xFFFFFFFC);
        mustFail([&] { validateHrs(hrs.data(), hrs.size()); });
        auto root =
            std::filesystem::temp_directory_path() /
            String::format("fta2-test-%llu", static_cast<unsigned long long>(SDL_GetTicksNS()))
                .c_str();
        std::filesystem::create_directories(root / "data");
        std::filesystem::create_directories(root / "package/bin");
        std::filesystem::create_directories(root / "package/original-game");
        check(discoverGameDirectory({}, root / "data", root / "package/bin").empty(),
              "Missing installation accepted by discovery");
        std::ofstream(root / "package/original-game/FTA2.EXE").put('x');
        check(discoverGameDirectory({}, root / "data", root / "package/bin") ==
                  root / "package/original-game",
              "Package-relative game discovery failed");
        std::ofstream(root / "data/FTA2.EXE").put('x');
        check(discoverGameDirectory(root / "data", root, root / "package/bin") == root / "data",
              "Remembered installation was not preferred");
        std::filesystem::create_directories(root / "package/data");
        check(discoverGameDirectory(root / "data", root, root / "package/bin") ==
                  root / "package/data",
              "Incomplete bundled data silently fell back to another installation");
        std::ofstream(root / "package/data/FTA2.EXE").put('x');
        check(discoverGameDirectory(root / "data", root, root / "package/bin") ==
                  root / "package/data",
              "Bundled data did not take precedence over remembered data");
        check(discoverGameDirectory(root / "data", root, root / "package/bin/") ==
                  root / "package/data",
              "SDL base path with a trailing separator did not find bundled data");
        std::filesystem::create_directories(root / "package/bin/data");
        check(discoverGameDirectory(root / "data", root, root / "package/bin") ==
                  root / "package/bin/data",
              "Executable-adjacent data discovery failed");
        mustFail([&] { requireSeparateOutput(Path(root / "data"), Path(root / "data/cache")); });
        requireSeparateOutput(Path(root / "data"), Path(root / "saves"));
        {
            Platform startup;
            startup.initGraphics(854, 480);
            SDL_Event shortcut{};
            shortcut.type = SDL_EVENT_KEY_DOWN;
            shortcut.key.key = SDLK_KP_ENTER;
            shortcut.key.mod = SDL_KMOD_RALT;
            check(SDL_PushEvent(&shortcut), "Cannot inject initial widescreen Alt+Enter");
            Event event;
            while (startup.pollEvent(event)) {
            }
            int count = 0;
            auto windows = SDL_GetWindows(&count);
            check(count == 1, "Initial widescreen window missing");
            int width, height;
            SDL_GetWindowSize(windows[0], &width, &height);
            check(!(SDL_GetWindowFlags(windows[0]) & SDL_WINDOW_FULLSCREEN) && width == 854 &&
                      height == 480,
                  "First widescreen return has wrong size");
            SDL_free(windows);
        }
        Platform platform;
        platform.saves.directory = root / u8"saves-\u017e-\u4e16";
        std::filesystem::create_directories(platform.saves.directory);
        check(platform.saves.openForSaving("../data/999.SAV") == nullptr,
              "Save traversal accepted");
        std::unique_ptr<OutSaveFile> save(platform.saves.openForSaving("fta2.001"));
        save->write(out.getData(), out.size());
        save->finalize();
        std::unique_ptr<InSaveFile> restored(platform.saves.openForLoading("fta2.001"));
        check(restored && restored->readUint16LE() == 0xFEDC, "Save directory round trip");
        platform.initGraphics(640, 480);
        {
            Saga2::Saga2Engine engine;
            engine._audio = new Saga2::AudioInterface;
            Saga2::globalPort = &engine._mainPort;
            struct RecordingWindow : Saga2::gWindow {
                Saga2::Rect16 updated;
                RecordingWindow() : gWindow(Saga2::Rect16(0, 0, 854, 480), 0, "test", nullptr) {}
                void update(const Saga2::Rect16& area) override { updated = area; }
                void draw() override {}
            } window;
            byte unusedImages[2]{};
            void* images[] = {&unusedImages[0], &unusedImages[1]};
            auto owner = new Saga2::GfxOwnerSelCompButton(window, Saga2::Rect16(500, 20, 24, 24),
                                                          images, 2, 0, nullptr);
            auto multi = new Saga2::GfxMultCompButton(window, Saga2::Rect16(500, 60, 24, 24),
                                                      images, 2, 0, 0, nullptr);
            auto checkUpdated = [&](Saga2::gPanel* button) {
                const auto bounds = button->getExtent();
                check(window.updated.x == bounds.x && window.updated.y == bounds.y &&
                          window.updated.width == bounds.width &&
                          window.updated.height == bounds.height,
                      "Translated button invalidated its old bounds");
                check(button->hitTest(Saga2::Point16(bounds.x + 12, bounds.y + 12)) == button,
                      "Translated button hit test used old bounds");
            };
            for (int shift : {214, -214, 214}) {
                window.translateControls(shift);
                for (int selected : {1, 0, 1, 0}) {
                    owner->select(selected);
                    checkUpdated(owner);
                    multi->invalidate();
                    checkUpdated(multi);
                    static_cast<Saga2::gPanel*>(multi)->activate(Saga2::kEventMouseDown);
                    checkUpdated(multi);
                }
            }
            struct HoverControl : Saga2::gControl {
                bool tooltip = true;
                int leaves = 0, deactivations = 0;
                HoverControl(Saga2::gPanelList& panel)
                    : gControl(panel, Saga2::Rect16(10, 10, 20, 20), nullptr, 0, nullptr) {}
                void pointerMove(Saga2::gPanelMessage& message) override {
                    if (!message._pointerLeave)
                        return;
                    check(!message._pointerEnter && !message._inPanel && _window.isOpen(),
                          "Window must send pointer leave before closing");
                    tooltip = false;
                    ++leaves;
                }
                bool activate(Saga2::gEventType) override { return true; }
                void deactivate() override {
                    ++deactivations;
                    gControl::deactivate();
                }
            };
            auto hover = new HoverControl(window);
            window.open();
            Event hoverEvent;
            hoverEvent.type = EVENT_MOUSEMOVE;
            hoverEvent.mouse = {12, 12};
            engine._toolBase->handleMouse(hoverEvent, platform.getMillis());
            check(engine._toolBase->isMousePanel(hover), "Test hover control was not entered");
            engine._toolBase->setActive(hover);
            window.close();
            check(!hover->tooltip && hover->leaves == 1 && hover->deactivations == 1 &&
                      !engine._toolBase->isMousePanel(hover) && !hover->isActive(),
                  "Closing a window must clear its hover tooltip before discarding panels");
            window.close();
            check(hover->leaves == 1 && hover->deactivations == 1,
                  "Closing an already closed window must not repeat hover cleanup");
        }
        Saga2::globalPort = nullptr;
        Saga2::g_vm = nullptr;
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        check(count > 0, "Window handle");
        auto window = windows[0];
        SDL_free(windows);
        Event nativeEvent;
        auto checkWheel = [&](float gameX, float gameY) {
            while (platform.pollEvent(nativeEvent)) {
            }
            float windowX = 0, windowY = 0;
            check(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), gameX, gameY, &windowX,
                                                &windowY),
                  "Cannot convert wheel position");
            for (auto direction : {SDL_MOUSEWHEEL_NORMAL, SDL_MOUSEWHEEL_FLIPPED}) {
                for (float delta : {1.f, -1.f, 0.25f, -0.75f, 3.f, 0.f}) {
                    SDL_Event wheel{};
                    wheel.type = SDL_EVENT_MOUSE_WHEEL;
                    wheel.wheel.windowID = SDL_GetWindowID(window);
                    wheel.wheel.mouse_x = windowX;
                    wheel.wheel.mouse_y = windowY;
                    wheel.wheel.y = delta;
                    wheel.wheel.x = 2; // Horizontal movement must not become vertical input.
                    wheel.wheel.direction = direction;
                    check(SDL_PushEvent(&wheel), "Cannot inject wheel input");
                }
            }
            for (auto direction : {SDL_MOUSEWHEEL_NORMAL, SDL_MOUSEWHEEL_FLIPPED}) {
                for (float delta : {1.f, -1.f, 0.25f, -0.75f, 3.f, 0.f}) {
                    bool received = platform.pollEvent(nativeEvent);
                    check(received && nativeEvent.type == EVENT_MOUSEWHEEL &&
                              nativeEvent.mouse.x == gameX && nativeEvent.mouse.y == gameY &&
                              nativeEvent.wheelY ==
                                  (direction == SDL_MOUSEWHEEL_FLIPPED ? -delta : delta),
                          String::format(
                              "Wheel translation failed: received=%d type=%d position=%d,%d "
                              "expected=%.0f,%.0f delta=%g input=%g direction=%d",
                              received, nativeEvent.type, nativeEvent.mouse.x, nativeEvent.mouse.y,
                              gameX, gameY, nativeEvent.wheelY, delta, direction)
                              .c_str());
                }
            }
            check(!platform.pollEvent(nativeEvent), "Unexpected event after wheel input");
        };
        check(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN, "Fullscreen startup");
        checkWheel(320, 240);
        auto toggleFullscreen = [&] {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.key = SDLK_RETURN;
            event.key.mod = SDL_KMOD_ALT;
            check(SDL_PushEvent(&event), "Cannot inject Alt+Enter");
            while (platform.pollEvent(nativeEvent)) {
                check(nativeEvent.type != EVENT_KEYDOWN, "Alt+Enter leaked to the game");
            }
            SDL_SyncWindow(window);
        };
        auto checkSize = [&](int width, int height) {
            int actualWidth, actualHeight;
            check(SDL_GetWindowSize(window, &actualWidth, &actualHeight),
                  "Cannot read window size");
            check(actualWidth == width && actualHeight == height,
                  String::format("Window size %d x %d, expected %d x %d", actualWidth, actualHeight,
                                 width, height)
                      .c_str());
            checkWheel(platform.getWidth() / 2, 240);
        };
        toggleFullscreen();
        checkSize(640, 480);
        int windowX, windowY;
        SDL_GetWindowPosition(window, &windowX, &windowY);
        for (int pass = 0; pass < 2; ++pass) {
            check(platform.resizeGraphics(854, 480), "Automatic windowed widescreen");
            checkSize(854, 480);
            int x, y;
            SDL_GetWindowPosition(window, &x, &y);
            check(x == windowX && y == windowY, "Automatic resize moved the window");
            toggleFullscreen();
            check(platform.resizeGraphics(640, 480), "Classic mode while fullscreen");
            toggleFullscreen();
            checkSize(640, 480);
            toggleFullscreen();
            check(platform.resizeGraphics(854, 480), "Wide mode while fullscreen");
            toggleFullscreen();
            checkSize(854, 480);
            check(platform.resizeGraphics(640, 480), "Automatic windowed classic");
            checkSize(640, 480);
        }
        check(SDL_SetWindowMaximumSize(window, 700, 480), "Cannot constrain automatic window size");
        check(platform.resizeGraphics(854, 480), "Constrained automatic widescreen");
        checkSize(700, 480);
        check(platform.resizeGraphics(640, 480), "Automatic size after desktop constraint");
        checkSize(640, 480);
        check(SDL_SetWindowMaximumSize(window, 0, 0), "Cannot remove window size constraint");
        if (std::string(SDL_GetCurrentVideoDriver()) != "dummy") {
            check(SDL_MaximizeWindow(window) && SDL_SyncWindow(window), "Cannot maximize window");
            check(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED, "Maximize was not applied");
            check(platform.resizeGraphics(854, 480), "Mode change while maximized");
            check(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED,
                  "Mode change lost maximization");
            toggleFullscreen();
            check(platform.resizeGraphics(640, 480), "Classic mode from maximized fullscreen");
            toggleFullscreen();
            check(SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED, "Alt+Enter lost maximization");
            check(SDL_RestoreWindow(window) && SDL_SyncWindow(window), "Cannot restore window");
            checkSize(640, 480);
        }
        SDL_SetWindowSize(window, 900, 650);
        SDL_SyncWindow(window);
        SDL_GetWindowPosition(window, &windowX, &windowY);
        check(platform.resizeGraphics(854, 480), "Mode change after manual resize");
        checkSize(900, 650);
        toggleFullscreen();
        check(platform.resizeGraphics(640, 480), "Fullscreen mode after manual resize");
        toggleFullscreen();
        checkSize(900, 650);
        int restoredX, restoredY;
        SDL_GetWindowPosition(window, &restoredX, &restoredY);
        check(restoredX == windowX && restoredY == windowY, "Manual window position was lost");
        check(platform.resizeGraphics(854, 480), "Manual size when widening again");
        checkSize(900, 650);
        check(platform.resizeGraphics(640, 480), "Restore classic test framebuffer");
        check(SDL_SetWindowFullscreen(window, false), "Leave fullscreen");
        SDL_SyncWindow(window);
        SDL_SetWindowSize(window, 640, 480);
        SDL_SyncWindow(window);
        SDL_PumpEvents();
        auto point = platform.windowToGame(320, 240);
        check(point.x == 320 && point.y == 240, "Mouse 1:1 mapping");
        checkWheel(320, 240);
        while (platform.pollEvent(nativeEvent)) {
        }
        struct SelectionKey {
            SDL_Scancode scancode;
            SDL_Keycode key;
            int ascii;
            int selection;
        };
        const auto selectionKeys = {SelectionKey{SDL_SCANCODE_1, SDLK_1, '1', '1'},
                                    SelectionKey{SDL_SCANCODE_2, SDLK_2, '2', '2'},
                                    SelectionKey{SDL_SCANCODE_3, SDLK_3, '3', '3'},
                                    SelectionKey{SDL_SCANCODE_KP_1, SDLK_KP_1, '1', '1'},
                                    SelectionKey{SDL_SCANCODE_KP_2, SDLK_KP_2, '2', '2'},
                                    SelectionKey{SDL_SCANCODE_KP_3, SDLK_KP_3, '3', '3'},
                                    SelectionKey{SDL_SCANCODE_KP_4, SDLK_KP_4, '4', '4'},
                                    SelectionKey{SDL_SCANCODE_KP_5, SDLK_KP_5, '5', '5'},
                                    SelectionKey{SDL_SCANCODE_KP_6, SDLK_KP_6, '6', '6'},
                                    SelectionKey{SDL_SCANCODE_KP_7, SDLK_KP_7, '7', '7'},
                                    SelectionKey{SDL_SCANCODE_KP_8, SDLK_KP_8, '8', '8'},
                                    SelectionKey{SDL_SCANCODE_KP_9, SDLK_KP_9, '9', '9'},
                                    SelectionKey{SDL_SCANCODE_KP_0, SDLK_KP_0, '0', '0'}};
        for (auto input : selectionKeys) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID = SDL_GetWindowID(window);
            key.key.scancode = input.scancode;
            key.key.key = input.key;
            key.key.down = true;
            check(SDL_PushEvent(&key), "Cannot inject character selection key");
        }
        for (auto input : selectionKeys) {
            check(platform.pollEvent(nativeEvent) && nativeEvent.type == EVENT_KEYDOWN &&
                      nativeEvent.kbd.ascii == input.ascii &&
                      nativeEvent.kbd.keycode == input.selection,
                  "Character selection key translation or dialog text changed");
        }
        check(!platform.pollEvent(nativeEvent), "Unexpected event after digit input");
        for (auto modifiers :
             {SDL_KMOD_NONE, SDL_KMOD_LSHIFT, SDL_KMOD_RSHIFT, SDL_KMOD_LCTRL, SDL_KMOD_RALT}) {
            SDL_SetModState(modifiers);
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.windowID = SDL_GetWindowID(window);
            key.key.scancode = SDL_SCANCODE_G;
            key.key.key = SDLK_G;
            key.key.mod = modifiers;
            key.key.down = true;
            check(SDL_PushEvent(&key), "Cannot inject map shortcut key");
            bool received = false;
            for (int attempt = 0; attempt < 4 && !received; ++attempt)
                received = platform.pollEvent(nativeEvent);
            const int expectedFlags = (modifiers & SDL_KMOD_SHIFT ? KBD_SHIFT : 0) |
                                      (modifiers & SDL_KMOD_CTRL ? KBD_CTRL : 0) |
                                      (modifiers & SDL_KMOD_ALT ? KBD_ALT : 0);
            const int expectedKey = modifiers & SDL_KMOD_SHIFT ? 'G' : 'g';
            check(received && nativeEvent.type == EVENT_KEYDOWN &&
                      nativeEvent.kbd.ascii == expectedKey && nativeEvent.kbd.keycode == 'g' &&
                      nativeEvent.kbd.flags == expectedFlags,
                  "Map shortcut translation or modifiers changed");
        }
        SDL_SetModState(SDL_KMOD_NONE);
        check(!platform.pollEvent(nativeEvent), "Unexpected event after map shortcut input");
        for (auto key : {SDLK_RETURN, SDLK_KP_ENTER}) {
            for (bool repeat : {false, true}) {
                SDL_Event shortcut{};
                shortcut.type = SDL_EVENT_KEY_DOWN;
                shortcut.key.windowID = SDL_GetWindowID(window);
                shortcut.key.key = key;
                shortcut.key.mod = SDL_KMOD_RALT;
                shortcut.key.down = true;
                shortcut.key.repeat = repeat;
                check(SDL_PushEvent(&shortcut), "Cannot inject Alt+Enter");
            }
        }
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.windowID = SDL_GetWindowID(window);
        enter.key.key = SDLK_RETURN;
        enter.key.down = true;
        check(SDL_PushEvent(&enter), "Cannot inject ordinary Enter");
        check(platform.pollEvent(nativeEvent) && nativeEvent.type == EVENT_KEYDOWN &&
                  nativeEvent.kbd.ascii == ASCII_RETURN,
              "Alt+Enter reached the game or ordinary Enter was swallowed");
        check(!platform.pollEvent(nativeEvent), "Repeated Alt+Enter reached the game");
        check(SDL_SetWindowFullscreen(window, false), "Leave fullscreen after shortcuts");
        SDL_SyncWindow(window);
        SDL_SetWindowSize(window, 1280, 720);
        SDL_SyncWindow(window);
        SDL_PumpEvents();
        point = platform.windowToGame(160, 0);
        check(point.x == 0 && point.y == 0, "Letterbox left edge");
        point = platform.windowToGame(1120, 720);
        check(point.x == 640 && point.y == 480, "Letterbox right edge");
        checkWheel(0, 0);
        checkWheel(320, 240);
        auto classic = DisplayLayout::forMode(DisplayMode::Classic);
        auto wide = DisplayLayout::forMode(DisplayMode::Widescreen);
        check(classic.screenWidth == 640 && classic.mapWidth == 440 && wide.screenWidth == 854 &&
                  wide.mapWidth == 654 && wide.mapHeight == classic.mapHeight &&
                  wide.screenWidth - wide.mapX - wide.mapWidth == 180,
              "Display layout dimensions");
        Saga2::Renderer backgrounds;
        const auto windowID = SDL_GetWindowID(window);
        for (int cycle = 0; cycle < 6; ++cycle) {
            backgrounds.saveBackBuffer(Saga2::kBeforeOpeningMenu);
            check(platform.resizeGraphics(854, 480), "Resize to widescreen");
            backgrounds.restoreSavedBackBuffer(Saga2::kBeforeOpeningMenu);
            check(!backgrounds.hasSavedBackBuffer(Saga2::kBeforeOpeningMenu),
                  "Background with old dimensions was restored");
            check(platform.getWidth() == 854 && platform.lockScreen()->pitch == 854 &&
                      SDL_GetWindowID(window) == windowID,
                  "Framebuffer resize recreated the window or kept its old pitch");
            int width = 0, height = 0;
            SDL_GetWindowSize(window, &width, &height);
            check(width == 1280 && height == 720, "Framebuffer resize changed window size");
            checkWheel(20, 20);
            checkWheel(670, 435);
            checkWheel(800, 240);
            SDL_Event shortcut{};
            shortcut.type = SDL_EVENT_KEY_DOWN;
            shortcut.key.key = SDLK_RETURN;
            shortcut.key.mod = SDL_KMOD_LALT;
            shortcut.key.down = true;
            for (int toggle = 0; toggle < 2; ++toggle) {
                check(SDL_PushEvent(&shortcut), "Cannot inject wide Alt+Enter");
                while (platform.pollEvent(nativeEvent)) {
                }
                checkWheel(427, 240);
            }
            platform.fillScreen(Rect(850, 475, 854, 480), 31);
            platform.updateScreen();
            backgrounds.saveBackBuffer(Saga2::kBeforeOpeningMenu);
            check(platform.resizeGraphics(640, 480), "Resize to classic");
            backgrounds.popSavedBackBuffer(Saga2::kBeforeOpeningMenu);
            point = platform.windowToGame(160, 0);
            check(point.x == 0 && point.y == 0, "Classic letterbox after framebuffer resize");
        }
        check(!platform.resizeGraphics(0, 480) && platform.getWidth() == 640,
              "Invalid resize discarded the working framebuffer");
        ConfMan.setInt("random_seed", 42);
        RandomSource rng("test");
        check(rng.getRandomNumber(UINT_MAX) == 1570459262u, "Reference random sequence");
        Audio::Mixer mixer;
        mixer.enableDeterministic();
        auto pcm = new Audio::AudioStream;
        pcm->rate = 48000;
        pcm->samples = {0.5f, -0.5f};
        Audio::SoundHandle sound;
        mixer.playStream(Audio::Mixer::kSFXSoundType, &sound,
                         Audio::makeLoopingAudioStream(pcm, 2));
        mixer.advance(3);
        check(mixer.isSoundHandleActive(sound), "Finite audio loop ended early");
        mixer.advance(2);
        check(!mixer.isSoundHandleActive(sound) && mixer.sampleCounts().second == 4,
              "Finite audio loop did not stop after two repetitions");
        std::filesystem::remove_all(root);
        puts("Native runtime checks passed");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "runtime-test: %s\n", e.what());
        return 1;
    }
}
