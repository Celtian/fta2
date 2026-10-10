// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/probe.h"
#include "platform/resources.h"
#include "saga2/fta.h"
#include "saga2/saga2.h"
#include "saga2/actor.h"
#include "saga2/annoy.h"
#include "saga2/audio.h"
#include "saga2/music.h"
#include "saga2/panel.h"
#include "saga2/player.h"
#include "saga2/saveload.h"
#include "saga2/grequest.h"
#include "saga2/modal.h"
#include "saga2/tilemode.h"
#include "saga2/intrface.h"
#include "saga2/uidialog.h"
#include "saga2/gtextbox.h"
#include "saga2/document.h"
#include "saga2/script.h"
#include "saga2/speech.h"
#include "saga2/mouseimg.h"
#include "saga2/gpointer.h"
#include "saga2/contain.h"
#include "saga2/automap.h"
#include "saga2/motion.h"
#include "saga2/grabinfo.h"
namespace Saga2 {
extern char bookText[];
extern gTextBox* textBox;
extern GfxOwnerSelCompButton* speechTextBtn;
extern CDocumentAppearance bookAppearance, scrollAppearance, parchAppearance;
int16 pickSpeechButton(Point16 mouse, int16 size, gPort& textPort);
} // namespace Saga2
namespace Native {
std::function<void()> gameFrameHook;

namespace {
struct HotKeysProbe {
    Path output;
    bool pregame;
    int phase = 0, cycle = 0, ticks = 0;
    Saga2::gWindow* options = nullptr;
    Saga2::gWindow* help = nullptr;
    uint16 actor = 0;
    uint32 rng = 0;
    Saga2::TilePoint position;
    std::array<uint16, 3> playerFlags{};
    std::array<int, 3> volumes{};
    bool aggression = false, weapon = false, night = false, individual = false, muted = false,
         subtitles = true;
    size_t windowCount = 0;

    void require(bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(String::format("Hot keys probe: %s", message).c_str());
    }
    static void key(SDL_Keycode code) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = code;
        ev.key.down = true;
        if (!SDL_PushEvent(&ev))
            throw std::runtime_error("Hot keys probe: cannot inject key");
    }
    void click(Saga2::gPanel* panel) {
        require(panel != nullptr, "missing clickable control");
        auto box = panel->getExtent();
        clickAt(panel->getWindow(), box.x + box.width / 2, box.y + box.height / 2);
    }
    void clickAt(Saga2::gWindow* target, int localX, int localY) {
        for (auto type :
             {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
            mouseAt(target, localX, localY, type);
    }
    void mouseAt(Saga2::gWindow* target, int localX, int localY,
                 SDL_EventType type = SDL_EVENT_MOUSE_MOTION) {
        auto windowBox = target->getExtent();
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(count == 1, "expected one SDL window");
        auto window = windows[0];
        SDL_free(windows);
        float x = 0, y = 0;
        require(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), windowBox.x + localX,
                                              windowBox.y + localY, &x, &y),
                "cannot convert click coordinates");
        SDL_Event ev{};
        ev.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            ev.motion.windowID = SDL_GetWindowID(window);
            ev.motion.x = x;
            ev.motion.y = y;
        } else {
            ev.button.windowID = SDL_GetWindowID(window);
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.x = x;
            ev.button.y = y;
        }
        require(SDL_PushEvent(&ev), "cannot inject click");
    }
    void wheelAt(Saga2::gWindow* target, int localX, int localY, float delta,
                 SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL, float horizontal = 0) {
        auto box = target->getExtent();
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        require(count == 1, "expected one SDL window for wheel input");
        auto window = windows[0];
        SDL_free(windows);
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.windowID = SDL_GetWindowID(window);
        require(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), box.x + localX,
                                              box.y + localY, &ev.wheel.mouse_x, &ev.wheel.mouse_y),
                "cannot convert wheel coordinates");
        ev.wheel.y = delta;
        ev.wheel.x = horizontal;
        ev.wheel.direction = direction;
        require(SDL_PushEvent(&ev), "cannot inject wheel input");
    }
    void snapshot() {
        auto vm = Saga2::g_vm;
        actor = Saga2::getCenterActorID();
        position = Saga2::centerActorCoords();
        rng = vm->_rnd->getSeed();
        for (int i = 0; i < 3; ++i)
            playerFlags[i] = vm->_playerList[i]->_flags;
        volumes = {ConfMan.getInt("music_volume"), ConfMan.getInt("sfx_volume"),
                   ConfMan.getInt("speech_volume")};
        aggression = vm->_autoAggression;
        weapon = vm->_autoWeapon;
        night = vm->_showNight;
        individual = vm->_indivControlsFlag;
        muted = ConfMan.getBool("music_mute");
        subtitles = ConfMan.getBool("subtitles");
    }
    void unchanged(bool frozenRng = true) {
        auto vm = Saga2::g_vm;
        auto pos = Saga2::centerActorCoords();
        require(
            actor == Saga2::getCenterActorID() && pos.u == position.u && pos.v == position.v &&
                pos.z == position.z && (!frozenRng || rng == vm->_rnd->getSeed()),
            String::format("dialog changed state (actor=%u/%u u=%d/%d v=%d/%d z=%d/%d rng=%u/%u)",
                           actor, Saga2::getCenterActorID(), position.u, pos.u, position.v, pos.v,
                           position.z, pos.z, rng, vm->_rnd->getSeed())
                .c_str());
        for (int i = 0; i < 3; ++i)
            require(playerFlags[i] == vm->_playerList[i]->_flags, "help changed party flags");
        require(volumes == std::array<int, 3>{ConfMan.getInt("music_volume"),
                                              ConfMan.getInt("sfx_volume"),
                                              ConfMan.getInt("speech_volume")} &&
                    aggression == vm->_autoAggression && weapon == vm->_autoWeapon &&
                    night == vm->_showNight && individual == vm->_indivControlsFlag &&
                    muted == ConfMan.getBool("music_mute") &&
                    subtitles == ConfMan.getBool("subtitles") && subtitles == vm->_speechText,
                "help changed options or allowed gameplay shortcuts");
    }
    void tick() {
        require(++ticks < 200, "dialog input did not finish");
        auto tool = Saga2::g_vm->_toolBase;
        auto active = tool->topWindow();
        switch (phase) {
        case 0: {
            options = active;
            require(options && options->getExtent().height == Saga2::kOptBoxYSize,
                    "Options has wrong size");
            auto button = options->keyTest('H');
            require(button && button->getExtent().width == 121 && button->getExtent().height == 30,
                    "Hot Keys button is missing or resized");
            require((options->keyTest('S') != nullptr) == !pregame &&
                        (options->keyTest('N') != nullptr) == pregame,
                    "wrong Options variant");
            windowCount =
                size_t(std::distance(tool->bottomWindowIterator(), tool->topWindowIterator()));
            snapshot();
            if (!cycle)
                g_platform->capture(output / (pregame ? "pregame-options.ppm" : "options.ppm"));
            phase = 1;
            key(SDLK_G);
            if (cycle % 2)
                click(button);
            else
                key(SDLK_H);
            break;
        }
        case 1:
            help = active;
            require(help && help != options && help->getExtent().width == 487 &&
                        help->getExtent().height == 336 &&
                        size_t(std::distance(tool->bottomWindowIterator(),
                                             tool->topWindowIterator())) == windowCount + 1,
                    "Hot Keys did not open as a separate dialog");
            {
                const auto close = help->keyTest(ASCII_ESCAPE);
                require(close && close->getExtent().y + close->getExtent().height <=
                                     help->getExtent().height - 14,
                        "Hot Keys Close overlaps the bottom decoration");
            }
            unchanged();
            if (!cycle)
                g_platform->capture(output / (pregame ? "pregame-hotkeys.ppm" : "hotkeys.ppm"));
            phase = 2;
            wheelAt(help, 100, 100, -2);
            for (auto code : {SDLK_3, SDLK_A, SDLK_B, SDLK_TAB, SDLK_I, SDLK_S, SDLK_K, SDLK_G,
                              SDLK_M, SDLK_SPACE, SDLK_O})
                key(code);
            break;
        case 2:
            require(active == help, "gameplay shortcuts replaced the help dialog");
            unchanged();
            phase = 3;
            if (cycle % 2)
                click(help->keyTest(ASCII_ESCAPE));
            else
                key(SDLK_ESCAPE);
            break;
        case 3:
            require(active == options &&
                        size_t(std::distance(tool->bottomWindowIterator(),
                                             tool->topWindowIterator())) == windowCount &&
                        Saga2::ModalWindow::_current == options,
                    "closing help did not restore Options and modal focus");
            unchanged();
            if (!cycle)
                g_platform->capture(output /
                                    (pregame ? "pregame-returned.ppm" : "options-returned.ppm"));
            if (++cycle == 6) {
                phase = 4;
                if (pregame) {
                    // This variant has no Resume button; end only the test's requester.
                    static_cast<Saga2::requestInfo*>(options->_userData)->running = false;
                } else {
                    key(SDLK_ESCAPE);
                }
            } else {
                phase = 0;
            }
            break;
        default:
            break;
        }
    }
};
struct OptionsProbe {
    HotKeysProbe controls;
    enum Scenario { EmptyLoad, Credits, Save, Load } scenario;
    int phase = 0, cycle = 0, ticks = 0;
    int pageTicks = 0;
    Saga2::gWindow* options = nullptr;
    Saga2::gWindow* dialog = nullptr;

    void tick() {
        controls.require(++ticks < 150, "Options regression scenario timed out");
        auto active = Saga2::g_vm->_toolBase->topWindow();
        switch (phase) {
        case 0:
            options = active;
            controls.snapshot();
            if (scenario == Credits) {
                auto speech = Saga2::speechTextBtn;
                controls.require(speech && speech->getEnabled() && !speech->isGhosted() &&
                                     speech->isSelected() == Saga2::g_vm->_speechText,
                                 "Speech Text checkbox does not reflect the preference");
                controls.click(options->keyTest('C'));
            } else {
                controls.click(options->keyTest(scenario == Save ? 'S' : 'L'));
            }
            phase = 1;
            break;
        case 1:
            dialog = active;
            controls.require(dialog != options && Saga2::ModalWindow::_current == dialog,
                             "Options child dialog did not open");
            if (scenario == EmptyLoad) {
                controls.require(dialog->keyTest(ASCII_ESCAPE) && !dialog->keyTest('Y') &&
                                     !dialog->keyTest('N'),
                                 "empty Load still offers a destructive restart");
                if (!cycle)
                    g_platform->capture(controls.output / "load-empty.ppm");
                if (cycle % 2)
                    controls.click(dialog->keyTest(ASCII_ESCAPE));
                else
                    HotKeysProbe::key(SDLK_ESCAPE);
            } else if (scenario == Credits) {
                controls.require(Saga2::resImports->reserved[0] == 110 &&
                                     strlen(Saga2::bookText) > 100 &&
                                     strstr(Saga2::bookText, "Invalid textScript") == nullptr,
                                 "original credits text was not generated");
                auto book = dynamic_cast<Saga2::CDocument*>(dialog);
                auto ending = strstr(Saga2::bookText, "The Faery Tale Adventure licensed");
                controls.require(book && ending && String(book->text).ends_with(ending),
                                 "credits have stray characters after the final paragraph");
                if (!cycle) {
                    g_platform->capture(controls.output / "credits.ppm");
                    HotKeysProbe::key(SDLK_RIGHT);
                    phase = 5;
                    break;
                }
                HotKeysProbe::key(SDLK_ESCAPE);
            } else {
                controls.require(Saga2::textBox && Saga2::textBox->getIndex() == 0,
                                 "save/load list did not select the first slot");
                if (scenario == Save) {
                    for (auto ch : String("Options regression"))
                        HotKeysProbe::key(SDL_Keycode(ch));
                    phase = 2;
                    break;
                }
                controls.require(String(Saga2::textBox->getLine(0)) == "Options regression",
                                 "Load does not show the menu-created save");
                g_platform->capture(controls.output / "load-saved.ppm");
                // Exercise Cancel before confirming the next opening.
                if (!cycle)
                    HotKeysProbe::key(SDLK_ESCAPE);
                else {
                    controls.click(dialog->keyTest(ASCII_RETURN));
                    phase = 4;
                    break;
                }
            }
            phase = 3;
            break;
        case 2:
            if (String(Saga2::textBox->getLine(0)) != "Options regression")
                return;
            g_platform->capture(controls.output / "save-menu.ppm");
            controls.click(dialog->keyTest(ASCII_RETURN));
            phase = 3;
            break;
        case 3:
            // SDL can defer pushed input to the next event-pump iteration.
            if (active == dialog)
                return;
            controls.require(active == options && Saga2::ModalWindow::_current == options,
                             String::format("child dialog did not restore Options focus "
                                            "(scenario=%d cycle=%d top=%d modal=%d)",
                                            int(scenario), cycle, active == options,
                                            Saga2::ModalWindow::_current == options)
                                 .c_str());
            controls.unchanged();
            if (scenario == Save) {
                Saga2::SaveFileHeader header;
                controls.require(Saga2::getSaveName(0, header) &&
                                     header.saveName == "Options regression",
                                 "menu save was not written or cannot be discovered");
            }
            if (scenario != Save && ++cycle < 3) {
                HotKeysProbe::key(scenario == Credits ? SDLK_C : SDLK_L);
                phase = 1;
            } else {
                HotKeysProbe::key(SDLK_ESCAPE);
                phase = 4;
            }
            break;
        case 5:
            if (++pageTicks < 3)
                return;
            g_platform->capture(controls.output / "credits-next.ppm");
            for (int page = 2; page < 32; page += 2)
                static_cast<Saga2::CDocument*>(dialog)->gotoPage(page);
            pageTicks = 0;
            phase = 6;
            break;
        case 6:
            if (++pageTicks < 3)
                return;
            g_platform->capture(controls.output / "credits-last.ppm");
            controls.clickAt(dialog, 248, 230); // Original red bookmark closes the book.
            phase = 3;
            break;
        default:
            break;
        }
    }
};
struct DocumentCursorProbe {
    HotKeysProbe controls;
    bool pregame = false, done = false;
    int kind = 0, step = 0, ticks = 0;
    Saga2::gWindow* options = nullptr;
    Saga2::CDocument* document = nullptr;
    std::vector<std::function<void()>> actions;
    std::vector<byte> firstPage, nextPage, thirdPage, lastPage;

    Saga2::CDocumentAppearance& appearance() {
        return kind == 0   ? Saga2::bookAppearance
               : kind == 1 ? Saga2::scrollAppearance
                           : Saga2::parchAppearance;
    }
    void cursor(int id) {
        auto image = Saga2::mouseCursors[id];
        int hotspot = id == Saga2::kMouseArrowImage ? 0 : 7;
        controls.require(
            CursorMan.visible && CursorMan.w == image->_size.x && CursorMan.h == image->_size.y &&
                CursorMan.hotX == hotspot && CursorMan.hotY == hotspot &&
                CursorMan.pixels == std::vector<byte>(image->_data, image->_data + image->bytes()),
            String::format("wrong document cursor pixels or hotspot (kind=%d step=%d expected=%d)",
                           kind, step, id)
                .c_str());
        Saga2::Point16 offset;
        Saga2::g_vm->_pointer->getImage(offset);
        controls.require(offset.x == -hotspot && offset.y == -hotspot,
                         "document pointer offset does not match its hotspot");
    }
    // Normal parity captures omit cursors. These separate captures include the displayed cursor.
    void capture(const char* name) {
        auto screen = g_platform->lockScreen();
        byte palette[768];
        g_platform->getPaletteManager()->grabPalette(palette, 0, 256);
        Saga2::Point16 origin;
        Saga2::g_vm->_pointer->getImageCurPos(origin);
        std::ofstream out(
            controls.output /
                String::format("cursor-%s-%s.ppm", pregame ? "pregame" : "game", name).c_str(),
            std::ios::binary);
        out << "P6\n640 480\n255\n";
        for (int y = 0; y < 480; ++y) {
            for (int x = 0; x < 640; ++x) {
                byte color = static_cast<byte*>(screen->pixels)[y * screen->pitch + x];
                int cx = x - origin.x, cy = y - origin.y;
                if (CursorMan.visible && cx >= 0 && cy >= 0 && cx < CursorMan.w &&
                    cy < CursorMan.h) {
                    byte pixel = CursorMan.pixels[cy * CursorMan.w + cx];
                    if (pixel != CursorMan.key)
                        color = pixel;
                }
                out.write(reinterpret_cast<char*>(palette + color * 3), 3);
            }
        }
        g_platform->unlockScreen();
    }
    std::vector<byte> pagePixels() {
        auto screen = g_platform->lockScreen();
        auto box = document->getExtent();
        std::vector<byte> pixels;
        for (int y = box.y; y < box.y + box.height; ++y) {
            auto row = static_cast<byte*>(screen->pixels) + y * screen->pitch + box.x;
            pixels.insert(pixels.end(), row, row + box.width);
        }
        g_platform->unlockScreen();
        return pixels;
    }
    void move(int x, int y, int expected) {
        actions.push_back([=, this] { controls.mouseAt(document, x, y); });
        actions.push_back([=, this] { cursor(expected); });
    }
    void wheel(float delta, int x = 80, int y = 80,
               SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL, float horizontal = 0) {
        actions.push_back(
            [=, this] { controls.wheelAt(document, x, y, delta, direction, horizontal); });
    }
    void samePage(std::vector<byte>& expected) {
        actions.push_back([this, &expected] {
            controls.require(pagePixels() == expected,
                             "wheel page differs from original navigation");
            controls.require(Saga2::g_vm->_toolBase->topWindow() == document &&
                                 Saga2::ModalWindow::_current == document,
                             "wheel input changed modal focus");
            // Ambient audio continues its timed random selection during long documents.
            controls.unchanged(false);
        });
    }
    void wheelCapture(const char* name) {
        actions.push_back([=, this] {
            g_platform->capture(controls.output /
                                String::format("wheel-%s-%s-%s.ppm", pregame ? "pregame" : "game",
                                               kind == 1 ? "scroll" : "parchment", name)
                                    .c_str());
        });
    }
    void wheelChecks() {
        wheelCapture("before");
        wheel(1, 80, 190); // Up remains previous-page input even over the lower half.
        samePage(firstPage);
        actions.push_back([] { HotKeysProbe::key(SDLK_DOWN); });
        actions.push_back([this] {
            nextPage = pagePixels();
            controls.require(nextPage != firstPage, "vertical document has no second page");
        });
        actions.push_back([] { HotKeysProbe::key(SDLK_UP); });
        samePage(firstPage);
        wheel(-0.25f);
        samePage(firstPage);
        wheel(0, 80, 80, SDL_MOUSEWHEEL_NORMAL, 3);
        samePage(firstPage);
        wheel(-4, -10, 100);
        auto close = appearance().closeRect;
        wheel(-4, close.x + close.width / 2, close.y + close.height / 2);
        samePage(firstPage);
        wheel(-0.25f);
        samePage(firstPage);
        wheel(-0.5f); // Fractional input sums to exactly one notch.
        samePage(nextPage);
        wheelCapture("after");
        wheel(1);
        samePage(firstPage);
        actions.push_back([this] { controls.clickAt(document, 80, 190); });
        samePage(nextPage);
        actions.push_back([this] { controls.clickAt(document, 80, 80); });
        samePage(firstPage);
        actions.push_back([] {
            HotKeysProbe::key(SDLK_DOWN);
            HotKeysProbe::key(SDLK_DOWN);
        });
        actions.push_back([this] {
            thirdPage = pagePixels();
            controls.require(thirdPage != firstPage && thirdPage != nextPage,
                             "vertical document has no third page");
        });
        actions.push_back([] {
            HotKeysProbe::key(SDLK_UP);
            HotKeysProbe::key(SDLK_UP);
        });
        samePage(firstPage);
        wheel(-2);
        samePage(thirdPage);
        wheel(2);
        samePage(firstPage);
        wheel(1, 80, 80, SDL_MOUSEWHEEL_FLIPPED);
        samePage(nextPage);
        wheel(-1, 80, 80, SDL_MOUSEWHEEL_FLIPPED);
        samePage(firstPage);
        actions.push_back([this] {
            for (int page = 1; page < 32; ++page)
                document->gotoPage(page);
        });
        actions.push_back([this] { lastPage = pagePixels(); });
        wheel(-100);
        samePage(lastPage);
        wheel(1);
        actions.push_back([this] {
            controls.require(pagePixels() != lastPage, "wheel cannot leave the last page");
        });
        wheel(-1);
        samePage(lastPage);
        wheel(100);
        samePage(firstPage);
        actions.push_back([this] { cursor(Saga2::kMousePgUpImage); });
    }
    void prepare() {
        options = Saga2::g_vm->_toolBase->topWindow();
        controls.snapshot();
        auto app = appearance();
        bool vertical = kind != 0;
        int before = vertical ? Saga2::kMousePgUpImage : Saga2::kMousePgLeftImage;
        int after = vertical ? Saga2::kMousePgDownImage : Saga2::kMousePgRightImage;
        for (int cycle = 0; cycle < (kind == 0 ? 4 : 2); ++cycle) {
            // Position the pointer in the future document, then open without a motion event.
            int x = vertical ? 80 : cycle % 2 ? 300 : 100;
            int y = vertical ? cycle % 2 ? 190 : 80 : 120;
            actions.push_back([=, this] {
                auto box = options->getExtent();
                controls.mouseAt(options, app.windowPos.x + x - box.x, app.windowPos.y + y - box.y);
            });
            actions.push_back([this] {
                if (kind == 0)
                    HotKeysProbe::key(SDLK_C);
                else if (kind == 1)
                    Saga2::openScroll(Saga2::resImports->reserved[0]);
                else
                    Saga2::openParchment(Saga2::resImports->reserved[0]);
            });
            actions.push_back([=, this] {
                document = dynamic_cast<Saga2::CDocument*>(Saga2::g_vm->_toolBase->topWindow());
                controls.require(document && document != options &&
                                     Saga2::ModalWindow::_current == document,
                                 "document did not open with modal focus");
                cursor(cycle % 2 ? after : before);
                firstPage = pagePixels();
            });
            if (vertical) {
                if (!cycle) {
                    wheelChecks();
                } else {
                    wheel(-0.25f); // The previous opening left 0.75 of a notch pending.
                    samePage(firstPage);
                    wheel(-0.75f);
                    actions.push_back([this] {
                        controls.require(
                            pagePixels() != firstPage,
                            "fractional wheel input did not accumulate after reopening");
                    });
                    wheel(1);
                    samePage(firstPage);
                }
            } else if (!cycle) {
                wheel(-2, 100, 120);
                samePage(firstPage);
            }
            if (!cycle) {
                if (!vertical) {
                    for (int px : {100, 196, 197, 300, 196, 300, 100})
                        move(px, 120, px < 197 ? before : after);
                    actions.push_back([this] { capture("left"); });
                    move(300, 120, after);
                    actions.push_back([this] { capture("right"); });
                    actions.push_back([this] { controls.clickAt(document, 300, 120); });
                    actions.push_back([this] {
                        nextPage = pagePixels();
                        controls.require(nextPage != firstPage,
                                         "right-page click did not advance credits");
                    });
                    actions.push_back([this] { controls.clickAt(document, 100, 120); });
                    actions.push_back([this] {
                        controls.require(pagePixels() == firstPage,
                                         "left-page click did not return to the first spread");
                    });
                    actions.push_back([this] { controls.clickAt(document, 100, 120); });
                    actions.push_back([this] {
                        controls.require(pagePixels() == firstPage, "first-page limit changed");
                        cursor(Saga2::kMousePgLeftImage);
                        for (int page = 2; page < 32; page += 2)
                            document->gotoPage(page);
                    });
                    actions.push_back([this] { lastPage = pagePixels(); });
                    actions.push_back([this] { controls.clickAt(document, 300, 120); });
                    actions.push_back([this] {
                        controls.require(pagePixels() == lastPage, "last-page limit changed");
                        cursor(Saga2::kMousePgRightImage);
                    });
                    actions.push_back([this] { controls.clickAt(document, 100, 120); });
                    actions.push_back([this] {
                        controls.require(pagePixels() != lastPage, "cannot leave the last spread");
                    });
                    actions.push_back([this] { controls.clickAt(document, 300, 120); });
                    actions.push_back([this] {
                        controls.require(pagePixels() == lastPage,
                                         "cannot return to the last spread");
                    });
                } else {
                    for (int py :
                         {80, app.windowPos.height / 2 - 1, app.windowPos.height / 2, 190, 80})
                        move(80, py, py < app.windowPos.height / 2 ? before : after);
                }
                move(-10, 100, Saga2::kMouseArrowImage);
                move(app.closeRect.x + app.closeRect.width / 2,
                     app.closeRect.y + app.closeRect.height / 2, Saga2::kMouseArrowImage);
                if (!vertical)
                    actions.push_back([this] { capture("bookmark"); });
                move(x, y, before);
                actions.push_back(
                    [=, this] { controls.mouseAt(document, x, y, SDL_EVENT_MOUSE_BUTTON_DOWN); });
                int dragX = vertical ? x : 300, dragY = vertical ? 190 : y;
                move(dragX, dragY, after);
                actions.push_back([=, this] {
                    controls.mouseAt(document, dragX, dragY, SDL_EVENT_MOUSE_BUTTON_UP);
                });
                actions.push_back([=, this] { cursor(after); });
            }
            if (vertical && !cycle) {
                actions.push_back([this] { document->gotoPage(0); });
                wheel(-0.75f);
                samePage(firstPage);
            }
            move(x, y, cycle % 2 ? after : before);
            actions.push_back([=, this] {
                if ((cycle < 2 && kind == 0) || (cycle == 0 && kind != 0))
                    HotKeysProbe::key(SDLK_ESCAPE);
                else
                    controls.clickAt(document, app.closeRect.x + app.closeRect.width / 2,
                                     app.closeRect.y + app.closeRect.height / 2);
            });
            actions.push_back([=, this] {
                controls.require(Saga2::g_vm->_toolBase->topWindow() == options &&
                                     Saga2::ModalWindow::_current == options,
                                 "closing document did not restore Options focus");
                cursor(Saga2::kMouseArrowImage);
                controls.unchanged(false);
                if (!vertical)
                    capture(cycle < 2 ? "closed-escape" : "closed-bookmark");
            });
            actions.push_back([this] { controls.wheelAt(options, 100, 100, -2); });
            actions.push_back([this] {
                controls.require(Saga2::g_vm->_toolBase->topWindow() == options &&
                                     Saga2::ModalWindow::_current == options,
                                 "wheel input changed Options focus");
                cursor(Saga2::kMouseArrowImage);
                controls.unchanged(false);
            });
        }
        actions.push_back([this] {
            done = true;
            if (pregame)
                static_cast<Saga2::requestInfo*>(options->_userData)->running = false;
            else
                HotKeysProbe::key(SDLK_ESCAPE);
        });
    }
    void tick() {
        if (actions.empty())
            prepare();
        controls.require(++ticks < 1000, "document cursor scenario timed out");
        if (ticks % 3 == 0 && step < int(actions.size())) {
            auto action = actions[step++]; // Advance before a nested scroll/parchment event loop.
            try {
                action();
            } catch (const std::runtime_error& error) {
                throw std::runtime_error(
                    String::format("Document probe (kind=%d step=%d): %s", kind, step, error.what())
                        .c_str());
            }
        }
    }
};

struct SpeechToggleProbe {
    HotKeysProbe controls;
    bool pregame = false, expected = false;
    int phase = 0, ticks = 0;
    Saga2::gWindow* options = nullptr;

    void tick() {
        controls.require(++ticks < 50, "Speech Text toggle timed out");
        auto vm = Saga2::g_vm;
        auto button = Saga2::speechTextBtn;
        switch (phase) {
        case 0:
            options = vm->_toolBase->topWindow();
            controls.snapshot();
            controls.require(button->getEnabled() && !button->isGhosted() &&
                                 button->isSelected() == controls.subtitles &&
                                 vm->_speechText == controls.subtitles,
                             "Speech Text did not retain its previous value");
            expected = !controls.subtitles;
            controls.click(button);
            phase = 1;
            break;
        case 1:
            if (vm->_speechText != expected)
                return; // SDL may defer pushed input to its next pump.
            controls.require(button->isSelected() == expected &&
                                 ConfMan.getBool("subtitles") == expected,
                             "checkbox and saved preference disagree");
            ConfMan.setInt("music_volume", controls.volumes[0] / 2);
            vm->syncSoundSettings();
            ConfMan.setInt("music_volume", controls.volumes[0]);
            vm->syncSoundSettings();
            controls.require(vm->_speechText == expected &&
                                 ConfMan.getBool("subtitles") == expected,
                             "volume synchronization reset Speech Text");
            controls.subtitles = expected;
            phase = 2;
            break;
        case 2:
            controls.unchanged();
            g_platform->capture(controls.output / String::format("speech-%s-%s.ppm",
                                                                 pregame ? "pregame" : "options",
                                                                 expected ? "on" : "off")
                                                      .c_str());
            phase = 3;
            controls.clickAt(options, 15, 90);
            break;
        case 3:
            if (ConfMan.getInt("speech_volume") != 0)
                return;
            controls.clickAt(options, 182, 90);
            phase = 4;
            break;
        case 4:
            if (ConfMan.getInt("speech_volume") != 255)
                return;
            controls.clickAt(options, 99, 90);
            phase = 5;
            break;
        case 5: {
            const int volume = ConfMan.getInt("speech_volume");
            if (volume == 255)
                return;
            controls.require(volume > 0 && volume < 255, "Speech Volume midpoint is invalid");
            vm->saveConfig();
            Config saved;
            saved.file = ConfMan.file;
            saved.load();
            controls.require(saved.getInt("speech_volume") == volume,
                             "Speech Volume preference was not saved");
            ConfMan.setInt("speech_volume", controls.volumes[2]);
            vm->syncSoundSettings();
            auto night = options->hitTest(Saga2::Point16(23, 214));
            controls.require(night && night->getExtent() == Saga2::Rect16(14, 206, 18, 17),
                             "Night checkbox is not below Speech Text");
            std::ofstream(controls.output /
                          (pregame ? "options-audio-pregame.json" : "options-audio-game.json"))
                << "{\"status\":\"passed\",\"speech_levels\":[0," << volume
                << ",255],\"preference_saved\":true,\"night_bounds\":[14,206,18,17]}\n";
            controls.clickAt(options, 31, 222);
            phase = 6;
            break;
        }
        case 6:
            if (vm->_showNight == controls.night)
                return;
            controls.clickAt(options, 14, 206);
            phase = 7;
            break;
        case 7:
            if (vm->_showNight != controls.night)
                return;
            controls.unchanged();
            phase = 8;
            if (pregame)
                static_cast<Saga2::requestInfo*>(options->_userData)->running = false;
            else
                HotKeysProbe::key(SDLK_ESCAPE);
            break;
        default:
            break;
        }
    }
};

void verifyVoicePlayback(const Path& output) {
    auto require = [](bool value, const char* message) {
        if (!value)
            throw std::runtime_error(std::string("Voice probe: ") + message);
    };
    auto vm = Saga2::g_vm;
    auto audio = vm->_audio;
    auto mixer = g_platform->getMixer();
    require(vm->_speechVoice, "validated voice archive was not enabled");
    require(Saga2::speechList.speechCount() == 0, "unexpected initial dialogue");
    const auto rng = vm->_rnd->getSeed();
    const auto actor = Saga2::getCenterActorID();
    const auto position = Saga2::centerActorCoords();
    const auto subtitles = ConfMan.getBool("subtitles");
    const auto volume = ConfMan.getInt("speech_volume");
    std::ifstream input(voiceArchive.c_str(), std::ios::binary);
    byte header[12];
    require(bool(input.read(reinterpret_cast<char*>(header), 12)), "cannot read voice header");
    input.seekg(READ_LE_UINT32(header + 4));
    require(bool(input.read(reinterpret_cast<char*>(header), 12)) && !memcmp(header, "TALK", 4),
            "voice group is missing");
    const uint32 table = READ_LE_UINT32(header + 4), length = READ_LE_UINT32(header + 8);
    std::array<uint32, 3> ids{};
    std::array<int, 2> samples{};
    unsigned found = 0;
    for (uint32 entry = 0; entry < length && found < 2; entry += 12) {
        input.seekg(table + entry);
        require(bool(input.read(reinterpret_cast<char*>(header), 12)), "cannot read voice entry");
        if (header[0] < 'A' || header[0] > 'J' || !isDigit(header[1]) || !isDigit(header[2]) ||
            !isDigit(header[3]))
            continue;
        int small = (header[0] - 'A') * 1000 + (header[1] - '0') * 100 + (header[2] - '0') * 10 +
                    header[3] - '0';
        if (!small)
            continue;
        ids[found] = READ_BE_UINT32(header);
        samples[found++] = small;
    }
    require(found == 2, "not enough test recordings");
    auto drain = [&] {
        int ticks = 0;
        while (audio->saying(ids[0]) || audio->saying(ids[1])) {
            require(++ticks < 20000, "voice queue did not finish");
            if (audio->playFlag())
                audio->playMe();
            mixer->advance(4800);
        }
        require(!audio->talking(), "finished voice still playing");
    };
    uint32 missing[] = {ids[0], MKTAG('Z', '9', '9', '9'), 0};
    require(!Saga2::sayVoice(missing) && audio->_speechQueue.empty(),
            "partially missing utterance was queued");
    ConfMan.setInt("speech_volume", 0);
    vm->syncSoundSettings();
    require(!Saga2::sayVoice(ids.data()), "zero volume suppressed text fallback");
    ConfMan.setInt("speech_volume", 192);
    ConfMan.setBool("speech_mute", true);
    vm->syncSoundSettings();
    require(!Saga2::sayVoice(ids.data()), "muted voice suppressed text fallback");
    ConfMan.setBool("speech_mute", false);
    vm->syncSoundSettings();
    require(!Saga2::sayVoiceAt(ids.data(), Saga2::Point32(10000, 10000)),
            "inaudible distant voice suppressed text fallback");
    require(Saga2::sayVoice(ids.data()) && audio->_speechQueue.size() == 2,
            "valid utterance was not accepted before playback");
    audio->playMe();
    require(audio->talking(), "decoded recording did not start");
    mixer->pauseAll(true);
    mixer->advance(480000);
    mixer->pauseAll(false);
    require(audio->talking(), "pause advanced voice playback");
    audio->stopVoice();
    require(!audio->talking() && audio->_speechQueue.empty() && !audio->saying(ids[0]) &&
                !audio->saying(ids[1]),
            "aborting voice left pending segments");
    require(Saga2::sayVoice(ids.data()), "voice replay failed");
    drain();
    uint32 tail[] = {MKTAG('I', '1', '9', '4'), 0};
    require(Saga2::sayVoice(tail), "recording beyond the 256 MiB boundary was not loaded");
    audio->playMe();
    require(audio->talking(), "last archive recording did not start");
    int tailTicks = 0;
    while (audio->talking()) {
        require(++tailTicks < 20000, "last archive recording did not finish");
        mixer->advance(4800);
    }
    audio->playFlag();
    require(!audio->saying(tail[0]), "last archive recording retained an active marker");
    for (int variant = 0; variant < 3; ++variant) {
        ConfMan.setBool("subtitles", variant == 0);
        vm->syncSoundSettings();
        auto speech = Saga2::speechList.newTask(actor, Saga2::Speech::kSpNoAnimate);
        require(speech != nullptr, "cannot create voiced dialogue");
        char plain[] = "Original voice playback regression.";
        char choice[] = "Original voice playback regression.\n@Continue\n@Wait";
        require(speech->append(variant == 2 ? choice : plain, samples[0]), "append failed");
        char empty[] = "";
        require(speech->append(empty, samples[1]), "second segment append failed");
        speech->activate();
        Saga2::updateSpeech();
        require(Saga2::speechList.currentActive() == speech && !speech->longEnough(),
                "dialogue completed while voice was queued");
        const auto pixels = speech->_speechImage._data;
        const auto bytes = speech->_speechImage.bytes();
        const bool glyphs = pixels && std::any_of(pixels, pixels + bytes, [](byte p) { return p; });
        require(glyphs == (variant != 1), "Speech Text did not follow voice availability");
        require(Saga2::speechButtonCount == (variant == 2 ? 2 : 0), "voiced choices disappeared");
        audio->playMe();
        mixer->advance(1);
        require(!speech->longEnough(), "dialogue ended before audio completion");
        g_platform->capture(output / Path(String::format("voice-dialogue-%d.ppm", variant)));
        if (variant == 1) {
            speech->abortSpeech();
            require(speech->longEnough() && audio->_speechQueue.empty(),
                    "skipping dialogue retained voice segments");
        } else {
            drain();
            require(speech->longEnough(), "dialogue did not finish with its voice");
        }
        speech->dispose();
    }
    ConfMan.setInt("speech_volume", volume);
    ConfMan.setBool("subtitles", subtitles);
    vm->syncSoundSettings();
    require(vm->_rnd->getSeed() == rng && Saga2::getCenterActorID() == actor &&
                Saga2::centerActorCoords() == position,
            "voice test changed simulation state");
    std::ofstream(output / "voice-playback.json")
        << "{\"status\":\"passed\",\"recordings\":3,\"dialogue_variants\":3,"
           "\"large_archive_offset\":true,\"pause\":true,\"abort_queue\":true,\"missing_fallback\":true,"
           "\"muted_fallback\":true,\"simulation_unchanged\":true}\n";
}

struct SpeechFallbackProbe {
    HotKeysProbe controls;
    int scenario = 0, phase = 0, ticks = 0;
    Saga2::Speech* speech = nullptr;
    std::vector<byte> baseline;
    Saga2::Point16 baselineSize;
    bool done = false;

    void tick() {
        auto vm = Saga2::g_vm;
        controls.require(++ticks < 100, "unvoiced dialogue did not finish");
        switch (phase) {
        case 0: {
            controls.require(Saga2::speechList.speechCount() == 0, "unexpected pending speech");
            ConfMan.setBool("subtitles", scenario % 2 == 0);
            vm->syncSoundSettings();
            speech =
                Saga2::speechList.newTask(Saga2::getCenterActorID(), Saga2::Speech::kSpNoAnimate);
            controls.require(speech != nullptr, "cannot create fallback speech");
            char plain[] = "Unvoiced dialogue remains readable.";
            char choices[] = "Missing voice must keep this dialogue readable.\n@Continue\n@Wait";
            controls.require(
                speech->append(scenario < 2 ? plain : choices, scenario < 2 ? 0 : 9999),
                "cannot append fallback speech");
            speech->activate();
            phase = 1;
            break;
        }
        case 1: {
            if (Saga2::speechList.currentActive() == speech && !speech->_speechImage._data)
                return; // The original renderer need not draw on every loop iteration.
            controls.require(
                Saga2::speechList.currentActive() == speech && speech->_speechImage._data &&
                    !speech->longEnough(),
                String::format("fallback speech failed (scenario=%d current=%d bitmap=%d)",
                               scenario, Saga2::speechList.currentActive() == speech,
                               Saga2::speechList.currentActive() == speech &&
                                   speech->_speechImage._data != nullptr)
                    .c_str());
            auto size = speech->_speechImage._size;
            std::vector<byte> pixels(speech->_speechImage._data,
                                     speech->_speechImage._data + speech->_speechImage.bytes());
            controls.require(
                std::count_if(pixels.begin(), pixels.end(), [](byte v) { return v != 0; }) > 100,
                "fallback speech did not render readable glyphs");
            if (scenario % 2 == 0) {
                baseline = pixels;
                baselineSize = size;
            } else {
                controls.require(size.x == baselineSize.x && size.y == baselineSize.y &&
                                     pixels == baseline,
                                 "unchecked Speech Text suppressed unvoiced dialogue or choices");
                g_platform->capture(controls.output / (scenario == 1 ? "speech-no-voice.ppm"
                                                                     : "speech-missing-voice.ppm"));
            }
            if (scenario < 2) {
                HotKeysProbe::key(SDLK_SPACE);
                phase = 3;
            } else {
                controls.require(Saga2::speechButtonCount == 2, "dialogue choices disappeared");
                g_platform->delayMillis(4000); // Controlled virtual time in the smoke scenario.
                phase = 2;
            }
            break;
        }
        case 2: {
            controls.require(Saga2::speechList.currentActive() == speech && speech->longEnough(),
                             "choice dialogue did not wait for input after its reading timer");
            HotKeysProbe::key(SDLK_G);
            bool found = false;
            for (int y = 0; y < 480 && !found; y += 2) {
                for (int x = 0; x < 560; x += 2) {
                    if (Saga2::pickSpeechButton(Saga2::Point16(x, y), speech->_speechImage._size.x,
                                                speech->_textPort) == 1) {
                        controls.clickAt(vm->_toolBase->topWindow(), x, y);
                        found = true;
                        break;
                    }
                }
            }
            controls.require(found, "cannot locate the Continue choice");
            phase = 3;
            break;
        }
        case 3:
            if (Saga2::speechList.currentActive() == speech)
                return;
            controls.require(Saga2::speechList.speechCount() == 0,
                             "dismissed dialogue left a pending speech task");
            if (++scenario == 4) {
                vm->saveConfig();
                done = true;
            } else {
                phase = 0;
            }
            break;
        }
    }
};
struct ContainerEscapeProbe {
    HotKeysProbe controls;
    Saga2::GameObject* chest = nullptr;
    Saga2::gWindow* parent = nullptr;
    int phase = 0, cycle = 0, ticks = 0, menus = 0, modalTicks = 0;
    bool done = false, expectingMenu = false;

    Saga2::ContainerNode* physical() {
        return Saga2::g_vm->_cnm->find(chest->thisID(), Saga2::ContainerNode::kPhysicalType);
    }
    Saga2::ContainerNode* mental() {
        return Saga2::g_vm->_cnm->find(Saga2::getCenterActorID(),
                                       Saga2::ContainerNode::kMentalType);
    }
    void modalTick() {
        if (!expectingMenu) {
            controls.require(++modalTicks < 10, "Options did not consume its closing Escape");
            return;
        }
        controls.require(++menus <= 2, "Escape opened Options before closing containers");
        modalTicks = 0;
        auto tool = Saga2::g_vm->_toolBase;
        controls.require(tool->topWindow() == Saga2::ModalWindow::_current &&
                             tool->topWindow()->getExtent().height == Saga2::kOptBoxYSize,
                         "expected Options after container navigation");
        HotKeysProbe::key(SDLK_ESCAPE);
        expectingMenu = false;
    }
    void tick() {
        controls.require(
            ++ticks < 100,
            String::format("container Escape navigation did not finish (phase=%d cycle=%d)", phase,
                           cycle)
                .c_str());
        auto vm = Saga2::g_vm;
        auto tool = vm->_toolBase;
        switch (phase) {
        case 0: {
            parent = tool->topWindow();
            verifyContainerOwners(controls.output);
            auto position = Saga2::centerActorCoords();
            int nearest = 32767;
            for (int id = 0; id < Saga2::objectCount; ++id) {
                auto obj = Saga2::GameObject::objectAddress(id);
                if (dynamic_cast<Saga2::PhysicalContainerProto*>(obj->proto()) && !obj->isOpen() &&
                    !obj->isLocked() && obj->world() == Saga2::getCenterActor()->world()) {
                    int distance = (obj->getWorldLocation() - position).quickHDistance();
                    if (distance < nearest) {
                        chest = obj;
                        nearest = distance;
                    }
                }
            }
            controls.require(chest != nullptr && nearest <= 32,
                             "no nearby unlocked original-data container");
            controls.snapshot();
            std::ofstream(controls.output / "container-escape.json")
                << "{\"object\":" << chest->thisID() << ",\"name\":\"" << chest->objName()
                << "\"}\n";
            phase = 1;
            break;
        }
        case 1:
            controls.require(chest->open(Saga2::getCenterActorID()), "cannot open container");
            Saga2::updateContainerWindows();
            controls.require(physical() && physical()->getWindow() && chest->isOpen(),
                             "container did not open");
            if (cycle == 0) {
                g_platform->capture(controls.output / "container-open.ppm");
                expectingMenu = true;
                HotKeysProbe::key(SDLK_O);
                phase = 2;
            } else {
                phase = 3;
            }
            break;
        case 2:
            if (expectingMenu)
                return;
            controls.require(menus == 1 && physical() && physical()->getWindow() &&
                                 chest->isOpen() && tool->topWindow() == physical()->getWindow(),
                             "O or modal Escape closed the underlying container");
            phase = 3;
            break;
        case 3:
            // Close the frontmost of two windows, even with the pointer outside it.
            Saga2::OpenMindContainer(Saga2::getCenterActorPlayerID(), true, cycle % 3);
            controls.require(mental() && mental()->getWindow() &&
                                 tool->topWindow() == mental()->getWindow(),
                             "mental container did not open above the physical container");
            controls.mouseAt(parent, 4, 475);
            HotKeysProbe::key(SDLK_ESCAPE);
            phase = 4;
            break;
        case 4: {
            controls.require(!mental() && chest->isOpen() && physical() &&
                                 tool->topWindow() == physical()->getWindow(),
                             "Escape did not close only the frontmost container");
            if (cycle % 2) {
                // A visible container still has priority after the game regains keyboard focus.
                controls.clickAt(parent, 4, 475);
            }
            const auto box = physical()->getWindow()->getView().getExtent();
            controls.mouseAt(physical()->getWindow(), box.x + 8, box.y + 8);
            phase = 7;
            break;
        }
        case 7:
            controls.require(vm->_cnm->_mouseInView &&
                                 tool->isMousePanel(&physical()->getWindow()->getView()),
                             "physical inventory hover was not entered before Escape");
            HotKeysProbe::key(SDLK_ESCAPE);
            phase = 5;
            break;
        case 5:
            controls.require(!physical() && !chest->isOpen() && tool->topWindow() == parent &&
                                 Saga2::ModalWindow::_current == nullptr,
                             "Escape did not close the physical object and restore the game");
            controls.require(!vm->_cnm->_mouseInView && !vm->_cnm->_mouseText[0] &&
                                 vm->_cnm->_lastPickedObjectID == Saga2::Nothing,
                             "Escape retained physical inventory tooltip state");
            controls.unchanged(false);
            if (cycle == 0)
                g_platform->capture(controls.output / "container-closed.ppm");
            if (++cycle < 12) {
                phase = 1;
            } else {
                expectingMenu = true;
                HotKeysProbe::key(SDLK_ESCAPE);
                phase = 6;
            }
            break;
        case 6:
            if (expectingMenu)
                return;
            controls.require(menus == 2 && tool->topWindow() == parent &&
                                 Saga2::ModalWindow::_current == nullptr,
                             "Escape without containers did not open and close Options");
            controls.unchanged(false);
            std::ofstream(controls.output / "container-tooltip.json")
                << "{\"status\":\"passed\",\"physical_closes\":12,\"delayed_text_cleared\":true}\n";
            done = true;
            break;
        }
    }
};
struct MapShortcutProbe {
    HotKeysProbe controls;
    std::array<Saga2::GameObject*, 3> maps{};
    std::array<std::vector<byte>, 3> pages;
    std::array<Saga2::GameObject*, 3> bags{};
    std::array<Saga2::Location, 3> mapOrigins{}, bagOrigins{};
    std::array<uint16, 3> mapFlags{};
    std::vector<std::function<void()>> actions;
    size_t action = 0;
    std::string initialInventory, inventory;
    Saga2::GameObject* expectedMap = nullptr;
    int expectedBrother = 0;
    Saga2::gWindow* game = nullptr;
    Saga2::gWindow* parent = nullptr;
    Saga2::AutoMap* map = nullptr;
    Saga2::GameObject* bag = nullptr;
    int phase = 0, cycle = 0, modalPhase = 0, ticks = 0;
    bool seen = false, done = false;

    Saga2::GameObject* firstMap(Saga2::GameObject* owner) {
        Saga2::RecursiveContainerIterator iter(owner);
        Saga2::GameObject* item = nullptr;
        for (auto id = iter.first(&item); id != Saga2::Nothing; id = iter.next(&item))
            if (item->proto() == Saga2::g_vm->_objectProtos[320])
                return item;
        return nullptr;
    }
    std::string inventoryState() {
        std::vector<std::string> entries;
        for (int brother = 0; brother < 3; ++brother) {
            Saga2::RecursiveContainerIterator iter(
                Saga2::getPlayerActorAddress(brother)->getActor());
            Saga2::GameObject* item = nullptr;
            for (auto id = iter.first(&item); id != Saga2::Nothing; id = iter.next(&item)) {
                auto loc = item->getLocation();
                entries.push_back(String::format("%u:%u:%d:%d:%d:%u;", id, item->_data.parentID,
                                                 loc.u, loc.v, loc.z, item->_data.objectFlags)
                                      .c_str());
            }
        }
        std::sort(entries.begin(), entries.end());
        std::string result;
        for (const auto& entry : entries)
            result += entry;
        return result;
    }
    void restoreItems() {
        for (int i = 0; i < 3; ++i)
            bags[i]->move(bagOrigins[i]);
        for (int i = 0; i < 3; ++i) {
            maps[i]->move(mapOrigins[i]);
            // The ground fixture acquires the original renderer's visibility flags.
            maps[i]->_data.objectFlags = mapFlags[i];
        }
        controls.require(inventoryState() == initialInventory,
                         "map fixtures did not restore inventory");
    }
    void moveInto(Saga2::GameObject* item, Saga2::GameObject* owner) {
        Saga2::TilePoint slot;
        controls.require(owner->getAvailableSlot(item, &slot, false),
                         "map fixture has no free slot");
        item->move(Saga2::Location(slot, owner->thisID()));
    }
    void openMap(SDL_Keymod modifiers = SDL_KMOD_NONE) {
        controls.snapshot();
        inventory = inventoryState();
        expectedMap = firstMap(Saga2::getCenterActor());
        controls.require(expectedMap != nullptr, "map fixture has no map");
        expectedBrother = Saga2::getCenterActorID() - 32768;
        parent = Saga2::g_vm->_toolBase->topWindow();
        seen = false;
        modalPhase = 0;
        phase = 7;
        SDL_SetModState(modifiers);
        HotKeysProbe::key(SDLK_G);
    }
    void ignored(SDL_Keymod modifiers = SDL_KMOD_NONE) {
        actions.push_back([this, modifiers] {
            SDL_SetModState(modifiers);
            HotKeysProbe::key(SDLK_G);
        });
        for (int i = 0; i < 8; ++i)
            actions.push_back([this] {
                controls.require(!Saga2::ModalWindow::_current,
                                 "blocked map shortcut opened a dialog");
            });
        actions.push_back([] { SDL_SetModState(SDL_KMOD_NONE); });
    }
    void prepareActions() {
        actions.push_back([this] { moveInto(maps[0], Saga2::getCenterActor()); });
        actions.push_back([this] { openMap(SDL_KMOD_LSHIFT); });
        actions.push_back([this] { restoreItems(); });
        actions.push_back([this] {
            bag = bags[0];
            controls.require(bag->open(Saga2::getCenterActorID()),
                             "cannot open shortcut parent bag");
            Saga2::updateContainerWindows();
            controls.require(bagNode() && bagNode()->getWindow(),
                             "shortcut parent bag has no window");
        });
        actions.push_back([this] { openMap(); });
        actions.push_back([this] {
            controls.require(bag->isOpen() &&
                                 bagNode()->getWindow() == Saga2::g_vm->_toolBase->topWindow(),
                             "map Escape closed its parent container");
            HotKeysProbe::key(SDLK_ESCAPE);
        });
        actions.push_back([this] {
            controls.require(!bag->isOpen() && !bagNode() &&
                                 Saga2::g_vm->_toolBase->topWindow() == game,
                             "second Escape did not close the parent container");
        });
        actions.push_back([this] {
            moveInto(bags[2], Saga2::getCenterActor());
            moveInto(bags[0], bags[2]);
            controls.require(!bags[0]->isOpen() && !bags[2]->isOpen(), "nested bags are open");
        });
        actions.push_back([this] { openMap(); });
        actions.push_back([this] { restoreItems(); });
        actions.push_back([this] { moveInto(maps[1], bags[0]); });
        actions.push_back([this] { openMap(); });
        actions.push_back([this] { restoreItems(); });
        actions.push_back([this] {
            auto actor = Saga2::getCenterActor();
            maps[0]->move(Saga2::Location(actor->getLocation(), actor->_data.parentID));
            controls.require(!firstMap(actor) &&
                                 firstMap(Saga2::getPlayerActorAddress(1)->getActor()),
                             "missing-map fixture still owns a map");
        });
        ignored();
        actions.push_back([this] {
            Saga2::StatusLine->experationCheck();
            controls.require(Saga2::StatusLine->getEnabled(),
                             "missing map did not display status text");
            g_platform->capture(controls.output / "map-missing.ppm");
            moveInto(maps[0], bags[1]);
        });
        ignored();
        actions.push_back([] { HotKeysProbe::key(SDLK_2); });
        actions.push_back([this] { openMap(); });
        actions.push_back([this] {
            restoreItems();
            HotKeysProbe::key(SDLK_1);
        });
        ignored(SDL_KMOD_LCTRL);
        ignored(SDL_KMOD_RALT);
        actions.push_back([] { Saga2::LockUI(true); });
        ignored();
        actions.push_back([] { Saga2::LockUI(false); });
        actions.push_back([this] {
            Saga2::g_vm->_mouseInfo->grabObject(maps[1]);
            controls.require(firstMap(Saga2::getCenterActor()) != nullptr,
                             "held-object fixture removed the selected brother's map");
        });
        ignored();
        actions.push_back([this] {
            controls.require(Saga2::g_vm->_mouseInfo->getObject() == maps[1],
                             "ignored map shortcut replaced the held object");
            Saga2::g_vm->_mouseInfo->replaceObject();
            restoreItems();
        });
        actions.push_back([this] { controls.mouseAt(game, 4, 475, SDL_EVENT_MOUSE_BUTTON_DOWN); });
        ignored();
        actions.push_back([this] { controls.mouseAt(game, 4, 475, SDL_EVENT_MOUSE_BUTTON_UP); });
        actions.push_back([this] { restoreItems(); });
        actions.push_back([this] { openMap(); });
    }
    Saga2::ContainerNode* bagNode() {
        return Saga2::g_vm->_cnm->find(bag->thisID(), Saga2::ContainerNode::kPhysicalType);
    }
    std::vector<byte> pixels() {
        auto box = map->getExtent();
        auto screen = g_platform->lockScreen();
        std::vector<byte> result;
        // Compare the map panel above its controls, excluding its translucent bottom shadow.
        for (int y = box.y; y < box.y + 275; ++y) {
            auto row = static_cast<byte*>(screen->pixels) + y * screen->pitch + box.x;
            result.insert(result.end(), row, row + box.width);
        }
        g_platform->unlockScreen();
        return result;
    }
    void modalTick() {
        auto active = Saga2::ModalWindow::_current;
        controls.require((phase == 3 || phase == 7) && dynamic_cast<Saga2::AutoMap*>(active),
                         "unexpected dialog during map shortcut scenario");
        if (modalPhase == 0) {
            SDL_SetModState(SDL_KMOD_NONE);
            map = static_cast<Saga2::AutoMap*>(active);
            controls.require(
                Saga2::thisThread &&
                    Saga2::thisThread->_threadArgs.directObject == expectedMap->thisID() &&
                    Saga2::thisThread->_threadArgs.enactor == Saga2::getCenterActorID(),
                "map did not use the first carried map through its original script");
            controls.require(inventoryState() == inventory, "opening map changed inventory");
            auto pos = Saga2::centerActorCoords();
            controls.require(map->_trackPos == pos &&
                                 Saga2::g_vm->_currentMapNum ==
                                     Saga2::getCenterActor()->getMapNum() &&
                                 !map->getCheatFlag(),
                             "map tracks another brother or world, or enables unexplored areas");
            controls.unchanged(false);
            if (phase == 3 && cycle % 2 == 0) {
                pages[expectedBrother] = pixels();
                g_platform->capture(controls.output /
                                    String::format("map-brother-%d.ppm", cycle / 2 + 1).c_str());
            } else if (phase == 3) {
                g_platform->capture(
                    controls.output /
                    String::format("map-click-brother-%d.ppm", cycle / 2 + 1).c_str());
                controls.require(pixels() == pages[expectedBrother],
                                 "map shortcut differs from inventory double-click");
            }
            seen = true;
            for (auto key : {SDLK_G, SDLK_G, SDLK_3, SDLK_M, SDLK_O})
                HotKeysProbe::key(key);
            modalPhase = 1;
        } else if (modalPhase == 1) {
            controls.require(active == map && map->_trackPos == Saga2::centerActorCoords(),
                             "gameplay input changed the open map or selected brother");
            controls.unchanged(false);
            if (phase == 3 && cycle % 2)
                controls.clickAt(map, 15, 296);
            else
                HotKeysProbe::key(SDLK_ESCAPE);
            modalPhase = 2;
        }
    }
    void tick() {
        controls.require(
            ++ticks < 600,
            String::format("map shortcut scenario timed out (phase=%d cycle=%d)", phase, cycle)
                .c_str());
        auto vm = Saga2::g_vm;
        auto tool = vm->_toolBase;
        switch (phase) {
        case 0:
            game = tool->topWindow();
            for (int brother = 0; brother < 3; ++brother) {
                maps[brother] = firstMap(Saga2::getPlayerActorAddress(brother)->getActor());
                controls.require(maps[brother] != nullptr, "original brother has no carried map");
                bags[brother] = maps[brother]->parent();
                mapFlags[brother] = maps[brother]->_data.objectFlags;
                mapOrigins[brother] =
                    Saga2::Location(maps[brother]->getLocation(), bags[brother]->thisID());
                bagOrigins[brother] =
                    Saga2::Location(bags[brother]->getLocation(), bags[brother]->_data.parentID);
            }
            initialInventory = inventoryState();
            phase = 1;
            break;
        case 1:
            HotKeysProbe::key(SDLK_1 + cycle / 2);
            phase = 2;
            break;
        case 2: {
            controls.require(Saga2::getCenterActorID() == 32768 + cycle / 2,
                             "map test did not select its intended brother");
            controls.snapshot();
            expectedMap = maps[cycle / 2];
            expectedBrother = cycle / 2;
            bag = maps[cycle / 2]->parent();
            parent = game;
            seen = false;
            modalPhase = 0;
            phase = 3;
            if (cycle % 2 == 0) {
                controls.require(!bag->isOpen(), "map bag should initially be closed");
                HotKeysProbe::key(SDLK_G);
            } else {
                controls.require(bag->open(Saga2::getCenterActorID()), "cannot open map bag");
                Saga2::updateContainerWindows();
                controls.require(bagNode() && bagNode()->getWindow(), "map bag has no window");
                parent = bagNode()->getWindow();
                auto view = bagNode()->getView();
                auto box = view->getExtent();
                bool found = false;
                for (int y = 0; y < box.height && !found; y += 2) {
                    for (int x = 0; x < box.width; x += 2) {
                        if (view->pickObjectID(Saga2::Point16(x, y)) == maps[cycle / 2]->thisID()) {
                            g_platform->delayMillis(400);
                            controls.clickAt(parent, box.x + x, box.y + y);
                            controls.clickAt(parent, box.x + x, box.y + y);
                            found = true;
                            break;
                        }
                    }
                }
                controls.require(found, "cannot locate map icon for inventory double-click");
            }
            inventory = inventoryState();
            break;
        }
        case 3:
            if (!seen)
                return;
            controls.require(modalPhase == 2 && !Saga2::ModalWindow::_current &&
                                 tool->topWindow() == parent,
                             "closing the map did not restore its parent window");
            controls.unchanged(false);
            controls.require(inventoryState() == inventory, "closing map changed inventory");
            if (cycle % 2) {
                controls.require(bag->isOpen(), "closing map also closed its underlying bag");
                HotKeysProbe::key(SDLK_ESCAPE);
            }
            phase = 4;
            break;
        case 4:
            controls.require(!bag->isOpen() && !bagNode() && tool->topWindow() == game,
                             "map test left a container open");
            if (++cycle < 6) {
                phase = 1;
            } else {
                HotKeysProbe::key(SDLK_1);
                phase = 5;
            }
            break;
        case 5:
            controls.require(Saga2::getCenterActorID() == 32768, "map test did not restore Julian");
            controls.require(inventoryState() == initialInventory,
                             "map navigation changed inventory");
            prepareActions();
            phase = 6;
            break;
        case 6:
            if (action < actions.size())
                actions[action++]();
            else {
                controls.require(inventoryState() == initialInventory,
                                 "map fixtures changed inventory");
                done = true;
            }
            break;
        case 7:
            if (!seen)
                return;
            controls.require(modalPhase == 2 && !Saga2::ModalWindow::_current &&
                                 tool->topWindow() == parent,
                             "repeated map closure did not restore its parent");
            controls.require(inventoryState() == inventory, "repeated map use changed inventory");
            controls.unchanged(false);
            phase = 6;
            break;
        }
    }
};
} // namespace

void configureDeathProbe(const Path& output, uint16 actorID) {
    std::filesystem::create_directories(output);
    auto frames = std::make_shared<int>(0);
    gameFrameHook = [output, actorID, frames] {
        auto vm = Saga2::g_vm;
        auto actor = static_cast<Saga2::Actor*>(Saga2::GameObject::objectAddress(actorID));
        if (++*frames == 2) {
            const auto location = actor->getLocation();
            for (int i = 0; i < 3; ++i)
                Saga2::getPlayerActorAddress(i)->getActor()->move(location);
        }
        if (*frames == 20) {
            // Let the camera and active regions reach the villager before the animation.
            actor->_effectiveStats.vitality = 0;
            Saga2::MotionTask::die(*actor);
        }
        if (*frames == 250) {
            if (!actor->isDead() || actor->_moveTask != nullptr)
                throw std::runtime_error("Death probe: original death animation did not finish");
            // Also exercise repeated retirement with the actual dead actor as owner.
            Saga2::MotionTask::turn(*actor, static_cast<Saga2::Direction>(0));
            auto motion = actor->_moveTask;
            motion->finishTurn();
            const auto seed = vm->_rnd->getSeed();
            motion->finishTurn();
            if (vm->_rnd->getSeed() != seed)
                throw std::runtime_error("Death probe: repeated retirement consumed RNG");
            vm->_mTaskList->collectRetired();
            g_platform->capture(output / "villager-death.ppm");
            std::ofstream(output / "villager-death.json")
                << "{\"status\":\"passed\",\"actor\":" << actorID
                << ",\"original_death_animation\":true,\"repeated_retirement_rng_unchanged\":true}\n";
            vm->_gameRunning = false;
        }
    };
}

void configureVoiceProbe(const Path& output) {
    std::filesystem::create_directories(output);
    auto frames = std::make_shared<int>(0);
    gameFrameHook = [output, frames] {
        if (++*frames == 2) {
            verifyVoicePlayback(output);
            Saga2::g_vm->_gameRunning = false;
        }
    };
}

void configureSmokeProbe(const Path& output, int frames) {
    std::filesystem::create_directories(output);
    struct State {
        int frame = 0;
        bool active = false;
        Saga2::TilePoint initial, saved;
        uint16 selected = 0;
        int musicVolume = 0;
        std::unique_ptr<HotKeysProbe> ui;
        std::unique_ptr<OptionsProbe> options;
        std::unique_ptr<SpeechToggleProbe> toggle;
        std::unique_ptr<SpeechFallbackProbe> speech;
        std::unique_ptr<DocumentCursorProbe> cursor;
        std::unique_ptr<ContainerEscapeProbe> container;
        std::unique_ptr<MapShortcutProbe> map;
        bool initialSubtitles = true, speechChecked = false;
    };
    auto state = std::make_shared<State>();
    state->initialSubtitles = ConfMan.getBool("subtitles");
    gameFrameHook = [output, state, frames] {
        if (state->map && !state->map->done && Saga2::ModalWindow::_current) {
            state->map->modalTick();
            return;
        }
        if (state->container && !state->container->done && Saga2::ModalWindow::_current) {
            state->container->modalTick();
            return;
        }
        if (state->active) {
            if (state->ui)
                state->ui->tick();
            if (state->options)
                state->options->tick();
            if (state->toggle)
                state->toggle->tick();
            if (state->cursor)
                state->cursor->tick();
            return;
        }
        state->active = true;
        struct Guard {
            bool& flag;
            ~Guard() { flag = false; }
        } guard{state->active};
        auto& s = *state;
        auto vm = Saga2::g_vm;
        ++s.frame;
        auto keypad = [](SDL_Scancode scancode, SDL_Keycode key) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = scancode;
            ev.key.key = key;
            ev.key.down = true;
            if (!SDL_PushEvent(&ev))
                throw std::runtime_error("Smoke probe: cannot inject keypad input");
        };
        auto record = [&](const char* phase) {
            auto pos = Saga2::centerActorCoords();
            std::ofstream log(output / "state.jsonl", std::ios::app);
            log << "{\"phase\":\"" << phase << "\",\"frame\":" << s.frame
                << ",\"actor\":" << Saga2::getCenterActorID() << ",\"u\":" << pos.u
                << ",\"v\":" << pos.v << ",\"z\":" << pos.z << ",\"rng\":" << vm->_rnd->getSeed()
                << "}\n";
        };
        if (s.frame == 1) {
            if (vm->_speechText != s.initialSubtitles)
                throw std::runtime_error("Speech Text preference was reset at startup");
            std::ofstream(output / "speech-startup.json")
                << "{\"subtitles\":" << (vm->_speechText ? "true" : "false") << "}\n";
            s.initial = Saga2::centerActorCoords();
            record("initial");
            g_platform->capture(output / "initial.ppm");
        }
        if (s.frame == 20 || s.frame == 21 || s.frame == 100) {
            Event ev;
            ev.mouse = {380, 330};
            ev.type = s.frame == 20   ? EVENT_MOUSEMOVE
                      : s.frame == 21 ? EVENT_LBUTTONDOWN
                                      : EVENT_LBUTTONUP;
            vm->_toolBase->handleMouse(ev, g_platform->getMillis());
        }
        if (s.frame == 180) {
            record("moved");
            auto pos = Saga2::centerActorCoords();
            if (pos.u == s.initial.u && pos.v == s.initial.v)
                throw std::runtime_error("Smoke probe: movement did not change actor position");
        }
        if (s.frame == 181) {
            Saga2::playSound(MKTAG('C', 'L', 'K', 1));
            keypad(SDL_SCANCODE_KP_2, SDLK_KP_2);
        }
        if (s.frame == 190) {
            record("switched");
            if (Saga2::getCenterActorID() != 32769)
                throw std::runtime_error("Smoke probe: keypad 2 did not select Phillip");
            g_platform->capture(output / "controlled.ppm");
        }
        if (s.frame == 220) {
            s.saved = Saga2::centerActorCoords();
            s.selected = Saga2::getCenterActorID();
            if (vm->saveGameState(1, "Native round trip", false) != kNoError)
                throw std::runtime_error("Smoke probe: save failed");
            record("saved");
        }
        if (s.frame == 221) {
            keypad(SDL_SCANCODE_KP_3, SDLK_KP_3);
        }
        if (s.frame == 222 && Saga2::getCenterActorID() != 32770)
            throw std::runtime_error("Smoke probe: keypad 3 did not select Kevin");
        if (s.frame == 230) {
            vm->loadGameState(1);
            auto pos = Saga2::centerActorCoords();
            if (Saga2::getCenterActorID() != s.selected || pos.u != s.saved.u ||
                pos.v != s.saved.v || pos.z != s.saved.z)
                throw std::runtime_error("Smoke probe: restore changed actor state");
            record("restored");
        }
        if (s.frame == 235)
            keypad(SDL_SCANCODE_KP_1, SDLK_KP_1);
        if (s.frame == 236 && Saga2::getCenterActorID() != 32768)
            throw std::runtime_error("Smoke probe: keypad 1 did not select Julian");
        if (s.frame == 240) {
            std::unique_ptr<InSaveFile> original(
                vm->getSaveFileManager()->openForLoading("fta2.002"));
            if (original) {
                vm->loadGameState(2);
                record("original-save-loaded");
            }
        }
        if (s.frame == 241) {
            auto parent = vm->_toolBase->topWindow();
            for (bool pregame : {false, true}) {
                s.ui = std::make_unique<HotKeysProbe>();
                s.ui->output = output;
                s.ui->pregame = pregame;
                Saga2::OptionsDialog(pregame);
                // A modal close schedules stack restoration for the next event loop.
                if (Saga2::GameMode::_newmodeFlag)
                    Saga2::GameMode::update();
                s.ui->require(s.ui->phase == 4 && s.ui->cycle == 6,
                              "repeated opening scenario did not finish");
                s.ui->require(vm->_toolBase->topWindow() == parent,
                              "Options did not restore the game window");
                s.ui->unchanged();
                s.ui.reset();
            }
            record("hotkeys-checked");
        }
        if (s.frame == 242) {
            s.musicVolume = ConfMan.getInt("music_volume");
            HotKeysProbe::key(SDLK_M);
        }
        if (s.frame == 243) {
            if (!ConfMan.getBool("music_mute"))
                throw std::runtime_error("Smoke probe: M did not mute music");
            auto tone = new Audio::AudioStream;
            tone->rate = 48000;
            tone->samples = {0.25f, -0.25f};
            Audio::SoundHandle sound;
            g_platform->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &sound, tone);
            auto before = g_platform->getMixer()->sampleCounts();
            g_platform->getMixer()->advance(4800);
            auto after = g_platform->getMixer()->sampleCounts();
            if (after.first != before.first || after.second == before.second)
                throw std::runtime_error(
                    "Smoke probe: music mute silenced effects or leaked music");
            ConfMan.setInt("music_volume", s.musicVolume / 2);
            vm->syncSoundSettings();
            g_platform->getMixer()->advance(4800);
            if (g_platform->getMixer()->sampleCounts().first != before.first)
                throw std::runtime_error("Smoke probe: volume adjustment bypassed music mute");
            ConfMan.setInt("music_volume", s.musicVolume);
            vm->syncSoundSettings();
            HotKeysProbe::key(SDLK_M);
        }
        if (s.frame == 244) {
            if (ConfMan.getBool("music_mute"))
                throw std::runtime_error("Smoke probe: M did not unmute music");
            if (ConfMan.getInt("music_volume") != s.musicVolume)
                throw std::runtime_error("Smoke probe: M changed the saved music volume");
            auto before = g_platform->getMixer()->sampleCounts().first;
            g_platform->getMixer()->advance(4800);
            if (g_platform->getMixer()->sampleCounts().first == before)
                throw std::runtime_error("Smoke probe: music did not resume output");
            record("music-toggle-checked");
        }
        if (s.frame == 245) {
            auto parent = vm->_toolBase->topWindow();
            // Hide only test saves to exercise a fresh user's empty Load state.
            auto saveDir = std::filesystem::path(g_platform->saves.directory);
            auto backup = output / "menu-save-backup";
            std::filesystem::create_directories(backup);
            std::vector<std::filesystem::path> hidden;
            for (int i = 0; i < Saga2::kNumEditLines; ++i) {
                auto name = vm->getSavegameFile(i);
                auto file = saveDir / std::string(name);
                if (std::filesystem::exists(file)) {
                    std::filesystem::rename(file, backup / std::string(name));
                    hidden.push_back(file);
                }
            }
            // A malformed file must not be offered as a loadable game.
            auto damaged = saveDir / "fta2.049";
            std::ofstream(damaged, std::ios::binary) << "Not a saved game";
            HotKeysProbe menuSaved;
            for (auto scenario : {OptionsProbe::EmptyLoad, OptionsProbe::Credits,
                                  OptionsProbe::Save, OptionsProbe::Load}) {
                if (scenario == OptionsProbe::Load) {
                    Saga2::setCenterActor(Saga2::PlayerActorID(1));
                    menuSaved.require(Saga2::getCenterActorID() != menuSaved.actor,
                                      "load test did not change selection before restoring");
                }
                s.options = std::make_unique<OptionsProbe>();
                s.options->scenario = scenario;
                s.options->controls.output = output;
                Saga2::OptionsDialog(false);
                if (Saga2::GameMode::_newmodeFlag)
                    Saga2::GameMode::update();
                s.options->controls.require(s.options->phase == 4 &&
                                                vm->_toolBase->topWindow() == parent &&
                                                Saga2::ModalWindow::_current == nullptr,
                                            "Options scenario did not finish and restore the game");
                if (scenario == OptionsProbe::Load) {
                    // Loading resumes the world and redraws it; only open dialogs freeze RNG.
                    menuSaved.unchanged(false);
                } else {
                    s.options->controls.unchanged();
                    if (scenario == OptionsProbe::Save)
                        menuSaved = s.options->controls;
                }
                s.options.reset();
            }
            std::filesystem::remove(damaged);
            for (auto& file : hidden)
                std::filesystem::rename(backup / file.filename(), file);
            std::filesystem::remove(backup);
            record("options-regressions-checked");
        }
        if (s.frame == 246) {
            auto parent = vm->_toolBase->topWindow();
            for (bool pregame : {false, true}) {
                for (int kind = 0; kind < 3; ++kind) {
                    s.cursor = std::make_unique<DocumentCursorProbe>();
                    s.cursor->pregame = pregame;
                    s.cursor->kind = kind;
                    s.cursor->controls.output = output;
                    Saga2::OptionsDialog(pregame);
                    if (Saga2::GameMode::_newmodeFlag)
                        Saga2::GameMode::update();
                    s.cursor->controls.require(s.cursor->done &&
                                                   vm->_toolBase->topWindow() == parent &&
                                                   Saga2::ModalWindow::_current == nullptr,
                                               "document cursor scenario did not finish");
                    s.cursor->controls.unchanged(false);
                    s.cursor.reset();
                }
            }
            record("document-cursors-checked");
            record("document-wheel-checked");
            for (bool pregame : {false, true}) {
                int repetitions = pregame && ConfMan.getBool("subtitles") ? 5 : 4;
                for (int repeat = 0; repeat < repetitions; ++repeat) {
                    s.toggle = std::make_unique<SpeechToggleProbe>();
                    s.toggle->pregame = pregame;
                    s.toggle->controls.output = output;
                    Saga2::OptionsDialog(pregame);
                    if (Saga2::GameMode::_newmodeFlag)
                        Saga2::GameMode::update();
                    s.toggle->controls.require(s.toggle->phase == 8,
                                               "Speech Text toggle scenario did not close");
                    s.toggle->controls.unchanged();
                    s.toggle.reset();
                }
            }
            if (vm->_speechText || ConfMan.getBool("subtitles"))
                throw std::runtime_error("Mouse toggles did not leave Speech Text unchecked");
            HotKeysProbe::key(SDLK_M);
        }
        if (s.frame == 247) {
            if (vm->_speechText || !ConfMan.getBool("music_mute"))
                throw std::runtime_error("M reset Speech Text or did not mute");
            HotKeysProbe::key(SDLK_M);
        }
        if (s.frame == 248) {
            if (vm->_speechText || ConfMan.getBool("music_mute"))
                throw std::runtime_error("M reset Speech Text or did not unmute");
            uint32 missing[] = {MKTAG('Z', '9', '9', '9'), 0}, empty[] = {0};
            if (Saga2::sayVoiceAt(missing, Saga2::Point32(0, 0)) || Saga2::sayVoice(missing) ||
                Saga2::sayVoiceAt(empty, Saga2::Point32(0, 0)) ||
                Saga2::sayVoiceAt(nullptr, Saga2::Point32(0, 0)))
                throw std::runtime_error("Unavailable voice was reported as successful");
            vm->_speechVoice = true; // Missing resources must fail even with voice enabled.
            bool accepted = Saga2::sayVoiceAt(missing, Saga2::Point32(0, 0));
            vm->syncSoundSettings();
            if (accepted)
                throw std::runtime_error("Missing voice resource was reported as successful");
            s.speech = std::make_unique<SpeechFallbackProbe>();
            s.speech->controls.output = output;
        }
        if (s.speech && !s.speechChecked) {
            s.speech->tick();
            if (s.speech->done) {
                s.speechChecked = true;
                record("speech-text-checked");
            }
        }
        if (s.speechChecked && !s.container) {
            s.container = std::make_unique<ContainerEscapeProbe>();
            s.container->controls.output = output;
        }
        if (s.container && !s.container->done) {
            s.container->tick();
            if (s.container->done)
                record("container-escape-checked");
        }
        if (s.container && s.container->done && !s.map) {
            s.map = std::make_unique<MapShortcutProbe>();
            s.map->controls.output = output;
        }
        if (s.map && !s.map->done) {
            s.map->tick();
            if (s.map->done)
                record("map-shortcut-checked");
        }
        if (s.frame >= frames && s.speechChecked && s.map && s.map->done) {
            auto [music, effects] = g_platform->getMixer()->sampleCounts();
            std::ofstream audio(output / "audio.json");
            audio << "{\"music_nonzero_samples\":" << music
                  << ",\"effects_nonzero_samples\":" << effects << "}\n";
            if (!music || !effects)
                throw std::runtime_error("Smoke probe: silent music/effects");
            record("finished");
            g_platform->capture(output / "final.ppm");
            g_platform->quitting = true;
        }
    };
}
} // namespace Native
