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
            require(options && options->getExtent().height == 263, "Options has wrong size");
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
            if (cycle % 2)
                click(button);
            else
                key(SDLK_H);
            break;
        }
        case 1:
            help = active;
            require(help && help != options && help->getExtent().width == 487 &&
                        help->getExtent().height == 320 &&
                        size_t(std::distance(tool->bottomWindowIterator(),
                                             tool->topWindowIterator())) == windowCount + 1,
                    "Hot Keys did not open as a separate dialog");
            unchanged();
            if (!cycle)
                g_platform->capture(output / (pregame ? "pregame-hotkeys.ppm" : "hotkeys.ppm"));
            phase = 2;
            for (auto code : {SDLK_3, SDLK_A, SDLK_B, SDLK_TAB, SDLK_I, SDLK_S, SDLK_K, SDLK_M,
                              SDLK_SPACE, SDLK_O})
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
    std::vector<byte> firstPage, nextPage, lastPage;

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
                controls.unchanged();
                if (!vertical)
                    capture(cycle < 2 ? "closed-escape" : "closed-bookmark");
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
            action();
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
} // namespace

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
        bool initialSubtitles = true, speechChecked = false;
    };
    auto state = std::make_shared<State>();
    state->initialSubtitles = ConfMan.getBool("subtitles");
    gameFrameHook = [output, state, frames] {
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
                    s.cursor->controls.unchanged();
                    s.cursor.reset();
                }
            }
            record("document-cursors-checked");
            for (bool pregame : {false, true}) {
                int repetitions = pregame && ConfMan.getBool("subtitles") ? 5 : 4;
                for (int repeat = 0; repeat < repetitions; ++repeat) {
                    s.toggle = std::make_unique<SpeechToggleProbe>();
                    s.toggle->pregame = pregame;
                    s.toggle->controls.output = output;
                    Saga2::OptionsDialog(pregame);
                    if (Saga2::GameMode::_newmodeFlag)
                        Saga2::GameMode::update();
                    s.toggle->controls.require(s.toggle->phase == 3,
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
        if (s.frame >= frames && s.speechChecked) {
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
