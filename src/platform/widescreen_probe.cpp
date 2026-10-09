// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/probe.h"
#include "saga2/saga2.h"

#include "saga2/actor.h"
#include "saga2/automap.h"
#include "saga2/button.h"
#include "saga2/contain.h"
#include "saga2/display.h"
#include "saga2/document.h"
#include "saga2/fontlib.h"
#include "saga2/grequest.h"
#include "saga2/gtextbox.h"
#include "saga2/intrface.h"
#include "saga2/modal.h"
#include "saga2/player.h"
#include "saga2/script.h"
#include "saga2/tile.h"
#include "saga2/uidialog.h"

namespace Saga2 {
extern BackWindow* mainWindow;
extern gTextBox* textBox;
extern WorldMapData* mapList;
extern int16 worldCount;
extern AutoMap* pAutoMap;
extern GfxOwnerSelCompButton *centerBtns[kNumViews], *aggressBtns[kNumViews],
    *bandingBtns[kNumViews];
extern GfxOwnerSelCompButton *indivCenterBtn, *indivAggressBtn, *indivBandingBtn;
extern GfxOwnerSelCompButton *julBtn, *phiBtn, *kevBtn;
extern GfxMultCompButton *portBtns[kNumViews], *indivPortBtn;
void initAutoMap();
void updateMainDisplay();
} // namespace Saga2
namespace Native {
namespace {
constexpr int metaShift = int(Saga2::kTileUVShift) + int(Saga2::kPlatShift);
void requireWide(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(std::string("Widescreen probe: ") + message);
}
void key(SDL_Keycode value) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = value;
    event.key.down = true;
    requireWide(SDL_PushEvent(&event), "cannot inject key");
}
void mouse(int x, int y, SDL_EventType type) {
    int count = 0;
    auto windows = SDL_GetWindows(&count);
    requireWide(count == 1, "expected one window");
    auto window = windows[0];
    SDL_free(windows);
    float wx, wy;
    requireWide(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), x, y, &wx, &wy),
                "cannot map input");
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = wx;
        event.motion.y = wy;
    } else {
        event.button.windowID = SDL_GetWindowID(window);
        event.button.x = wx;
        event.button.y = wy;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    }
    requireWide(SDL_PushEvent(&event), "cannot inject mouse");
}
void click(Saga2::gWindow* window, int x, int y) {
    auto r = window->getExtent();
    for (auto type :
         {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP})
        mouse(r.x + x, r.y + y, type);
}
std::vector<byte> region(int x, int y, int width, int height) {
    auto surface = g_platform->lockScreen();
    std::vector<byte> pixels;
    for (int row = y; row < y + height; ++row) {
        auto first = static_cast<byte*>(surface->getBasePtr(x, row));
        pixels.insert(pixels.end(), first, first + width);
    }
    g_platform->unlockScreen();
    return pixels;
}

struct WidescreenProbe {
    Path output;
    bool busy = false, requested = false;
    bool pregameMenu = false;
    int modalPhase = 0, modalTicks = 0, frame = 0;
    int checkboxClicks = 0, optionsLayouts = 0, optionsCaptures = 0;
    std::array<std::vector<byte>, 2> checkboxPixels;
    std::vector<byte> labelPixels;
    enum Action { Toggle, Save, Load, NewGame, Inspect } action = Toggle;
    Saga2::gWindow* options = nullptr;
    Saga2::TilePoint walkingStart;
    bool inspectingExploration = false;
    int explorationMap = -1, explorationBrother = -1, explorationStep = -1;

    std::vector<byte> explored() const {
        std::vector<byte> result;
        for (int world = 0; world < Saga2::worldCount; ++world) {
            const auto& map = Saga2::mapList[world];
            for (int i = 0; i < map.mapSize * map.mapSize; ++i)
                result.push_back(bool(map.map->mapData[i] & Saga2::kMetaTileVisited));
        }
        return result;
    }

    void exploration() {
        using namespace Saga2;
        auto vm = g_vm;
        const auto originalMode = activeDisplayMode;
        const auto originalVisited = explored();
        const auto originalActor = getCenterActorID();
        const auto originalPosition = centerActorCoords();
        const auto originalSeed = vm->_rnd->getSeed();
        std::array<TilePoint, 3> originalPositions;
        std::array<ObjectID, 3> originalParents;
        for (int brother = 0; brother < 3; ++brother) {
            auto actor = vm->_playerList[brother]->getActor();
            originalPositions[brother] = actor->getLocation();
            originalParents[brother] = actor->IDParent();
        }
        requireWide(vm->saveGameState(3, "Exploration fixture checkpoint", false) == kNoError,
                    "cannot save exploration checkpoint");
        std::vector<std::vector<byte>> classicFootprints;
        std::ofstream report(output / "exploration.json");
        report << "{\"samples\":[";
        bool first = true;
        for (int mode = 0; mode < 2; ++mode) {
            ConfMan.setBool("widescreen", bool(mode));
            requestDisplayModeChange();
            applyPendingDisplayMode();
            requireWide(vm->loadGameState(3) == kNoError, "cannot reset exploration fixture");
            initAutoMap();
            auto expected = explored();
            const int world = getCenterActor()->world()->_mapNum;
            const auto& map = mapList[world];
            requireWide(map.mapSize > 32, "exploration fixture needs a larger map");
            size_t mapOffset = 0;
            for (int i = 0; i < world; ++i)
                mapOffset += mapList[i].mapSize * mapList[i].mapSize;
            int footprint = 0;
            for (int brother = 0; brother < 3; ++brother) {
                setCenterActor(PlayerActorID(brother));
                const auto actor = getCenterActor();
                requireWide(actor->world()->_mapNum == world, "unexpected fixture world");
                const int unit = 1 << metaShift;
                const int base = (6 + brother * 8) * unit;
                const Point phases[] = {
                    {0, 0}, {unit - 1, 0}, {0, unit - 1}, {unit - 1, unit - 1}, {unit, unit}};
                for (int step = 0; step < 5; ++step) {
                    const TilePoint position(base + phases[step].x, base + phases[step].y,
                                             actor->getLocation().z);
                    actor->setLocation(position);
                    updateMainDisplay();
                    requireWide(currentWorld == actor->world() && vm->_currentMapNum == world,
                                "camera selected the wrong world");
                    const auto meta = position >> metaShift;
                    for (int du = -2; du <= 2; ++du)
                        for (int dv = -2; dv <= 2; ++dv)
                            if (ABS(du) != 2 || ABS(dv) != 2)
                                expected[mapOffset + (meta.u + du) * map.mapSize + meta.v + dv] = 1;
                    const auto actual = explored();
                    requireWide(actual == expected, "exploration radius or shared areas changed");
                    if (!mode)
                        classicFootprints.push_back(actual);
                    else
                        requireWide(actual == classicFootprints.at(footprint),
                                    "aspect ratio changed the exploration footprint");
                    ++footprint;

                    // Sample the centered viewport on the actor's ground plane.
                    // Normalize camera smoothing without moving the actor or advancing time.
                    Point16 screen;
                    TileToScreenCoords(position, screen);
                    const auto layout = displayLayout();
                    int uncovered = 0, sideUncovered = 0, sidePixels = 0;
                    for (int y = 0; y < layout.mapHeight; ++y)
                        for (int x = 0; x < layout.mapWidth; ++x) {
                            const auto ground =
                                pickTilePos(Point32(x + screen.x - layout.mapWidth / 2,
                                                    y + screen.y - layout.mapHeight / 2 - 32),
                                            position);
                            const auto groundMeta = ground >> metaShift;
                            const bool visited =
                                groundMeta.u >= 0 && groundMeta.v >= 0 &&
                                groundMeta.u < map.mapSize && groundMeta.v < map.mapSize &&
                                (map.map->mapData[groundMeta.u * map.mapSize + groundMeta.v] &
                                 kMetaTileVisited);
                            const bool side = mode && (x < 107 || x >= 547);
                            uncovered += !visited;
                            sideUncovered += side && !visited;
                            sidePixels += side;
                        }
                    if (!first)
                        report << ',';
                    first = false;
                    report << "{\"mode\":\"" << (mode ? "wide" : "classic")
                           << "\",\"brother\":" << brother << ",\"step\":" << step
                           << ",\"world\":" << world << ",\"u\":" << position.u
                           << ",\"v\":" << position.v << ",\"uncovered_pixels\":" << uncovered
                           << ",\"side_pixels\":" << sidePixels
                           << ",\"uncovered_side_pixels\":" << sideUncovered << '}';
                    if (step == 0 || step == 4) {
                        inspectingExploration = true;
                        explorationMap = world;
                        explorationBrother = brother;
                        explorationStep = step;
                        inspect([] { openAutoMap(); });
                        inspectingExploration = false;
                        requireWide(explored() == actual,
                                    "opening a brother's map changed exploration");
                    }
                }
            }
            // Verify that a selected brother's map switches worlds and retains
            // the previous world's shared exploration rather than copying it.
            const int otherWorld = (world + 1) % worldCount;
            const auto& other = mapList[otherWorld];
            requireWide(worldCount > 1 && other.mapSize >= 8, "missing second-world fixture");
            const int unit = 1 << metaShift;
            const int center = other.mapSize / 2;
            getCenterActor()->setLocation(
                Location(TilePoint(center * unit, center * unit, 0), other.worldID));
            updateMainDisplay();
            size_t otherOffset = 0;
            for (int i = 0; i < otherWorld; ++i)
                otherOffset += mapList[i].mapSize * mapList[i].mapSize;
            for (int du = -2; du <= 2; ++du)
                for (int dv = -2; dv <= 2; ++dv)
                    if (ABS(du) != 2 || ABS(dv) != 2)
                        expected[otherOffset + (center + du) * other.mapSize + center + dv] = 1;
            requireWide(explored() == expected, "exploration leaked between worlds");
            if (!mode)
                classicFootprints.push_back(expected);
            else
                requireWide(explored() == classicFootprints.at(15),
                            "world-switch exploration differs between aspect ratios");
            inspectingExploration = true;
            explorationMap = otherWorld;
            explorationBrother = 2;
            explorationStep = 5;
            inspect([] { openAutoMap(); });
            inspectingExploration = false;
            requireWide(explored() == expected, "second-world map changed exploration");
        }
        ConfMan.setBool("widescreen", originalMode == DisplayMode::Widescreen);
        requestDisplayModeChange();
        applyPendingDisplayMode();
        requireWide(vm->loadGameState(3) == kNoError, "cannot restore exploration checkpoint");
        requireWide(explored() == originalVisited && getCenterActorID() == originalActor &&
                        centerActorCoords() == originalPosition &&
                        vm->_rnd->getSeed() == originalSeed,
                    "exploration fixture leaked into gameplay");
        for (int brother = 0; brother < 3; ++brother) {
            auto actor = vm->_playerList[brother]->getActor();
            requireWide(actor->getLocation() == originalPositions[brother] &&
                            actor->IDParent() == originalParents[brother],
                        "brother's fixture position or world was not restored");
        }
        requireWide(vm->saveGameState(4, "Exploration fixture checkpoint", false) == kNoError,
                    "cannot save restored exploration checkpoint");
        report << "],\"status\":\"passed\",\"footprints_compared\":16,"
                  "\"brother_maps_checked\":14,\"shared_world_exploration\":true,\"world_switches_"
                  "checked\":2,"
                  "\"radius_metatiles\":2,\"corners_excluded\":true,\"fixture_restored\":true}\n";
    }

    void buttonStates() {
        using namespace Saga2;
        auto vm = g_vm;
        const auto originalMode = activeDisplayMode;
        const auto originalActor = getCenterActorID();
        const auto originalPosition = centerActorCoords();
        const auto originalSeed = vm->_rnd->getSeed();
        requireWide(vm->saveGameState(5, "Button redraw checkpoint", false) == kNoError,
                    "cannot save button checkpoint");
        int checks = 0, selections = 0, toggles = 0, portraits = 0;
        auto drain = [&] {
            if (GameMode::_newmodeFlag)
                GameMode::update();
            Event event;
            while (g_platform->pollEvent(event)) {
                if (event.type == EVENT_KEYDOWN)
                    vm->_toolBase->handleKeyStroke(event);
                else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                         event.type == EVENT_LBUTTONUP)
                    vm->_toolBase->handleMouse(event, g_platform->getMillis());
            }
        };
        auto press = [&](gPanel* button) {
            const auto r = button->getExtent();
            click(mainWindow, r.x + r.width / 2, r.y + r.height / 2);
            drain();
        };
        auto verify = [&](const std::string& capture = {}) {
            std::vector<gPanel*> controls;
            if (vm->_indivControlsFlag)
                controls = {indivCenterBtn, indivAggressBtn, indivBandingBtn, julBtn,
                            phiBtn,         kevBtn,          indivPortBtn};
            else
                for (int brother = 0; brother < 3; ++brother)
                    controls.insert(controls.end(), {centerBtns[brother], aggressBtns[brother],
                                                     bandingBtns[brother], portBtns[brother]});
            std::vector<std::vector<byte>> immediate;
            for (auto control : controls) {
                const auto r = control->getExtent();
                immediate.push_back(region(r.x, r.y, r.width, r.height));
            }
            // Preserve the normally updated pixels before requesting any full redraw.
            if (!capture.empty())
                g_platform->capture(output / (capture + ".ppm"));
            reDrawScreen();
            for (size_t i = 0; i < controls.size(); ++i) {
                const auto r = controls[i]->getExtent();
                requireWide(immediate[i] == region(r.x, r.y, r.width, r.height),
                            String::format("button %u at %d,%d retained stale pixels (%s panel)",
                                           unsigned(i), r.x, r.y,
                                           vm->_indivControlsFlag ? "individual" : "group")
                                .c_str());
            }
            ++checks;
        };
        for (int cycle = 0; cycle < 2; ++cycle)
            for (bool wide : {false, true}) {
                ConfMan.setBool("widescreen", wide);
                requestDisplayModeChange();
                applyPendingDisplayMode();
                requireWide(g_platform->getWidth() == (wide ? 854 : 640),
                            "button test has the wrong display mode");
                for (int individual = 0; individual < 2; ++individual) {
                    setCenterBrother(0);
                    if (individual)
                        setControlPanelsToIndividualMode(0);
                    else
                        setControlPanelsToTrioMode();
                    reDrawScreen();
                    for (int keyboard = 0; keyboard < 2; ++keyboard)
                        for (int brother : {1, 2, 0}) {
                            if (keyboard) {
                                key(SDLK_1 + brother);
                                drain();
                            } else {
                                if (individual)
                                    setControlPanelsToIndividualMode(brother);
                                press(individual ? indivCenterBtn : centerBtns[brother]);
                            }
                            requireWide(getCenterActorPlayerID() == brother,
                                        "Center On or keyboard selected the wrong brother");
                            for (int i = 0; i < 3; ++i)
                                requireWide(centerBtns[i]->isSelected() == (i == brother),
                                            "Center On radio states are inconsistent");
                            if (individual) {
                                requireWide(indivCenterBtn->isSelected() &&
                                                julBtn->isSelected() == (brother == 0) &&
                                                phiBtn->isSelected() == (brother == 1) &&
                                                kevBtn->isSelected() == (brother == 2),
                                            "individual brother states are inconsistent");
                            }
                            const std::string capture =
                                std::string("buttons-") + (wide ? "wide-" : "classic-") +
                                (individual ? "individual-" : "group-") +
                                (keyboard ? "key-" : "mouse-") + std::to_string(brother);
                            verify(cycle ? "" : capture);
                            ++selections;
                            if (keyboard)
                                continue;
                            for (bool band : {false, true}) {
                                auto button =
                                    individual
                                        ? (band ? indivBandingBtn : indivAggressBtn)
                                        : (band ? bandingBtns[brother] : aggressBtns[brother]);
                                for (int toggle = 0; toggle < 2; ++toggle) {
                                    const bool old = button->isSelected();
                                    press(button);
                                    requireWide(button->isSelected() != old,
                                                "aggression or banding failed to toggle");
                                    verify();
                                    ++toggles;
                                }
                            }
                            const int portrait = individual ? kUiIndiv : brother;
                            const auto original = Portrait->getCurrentState(portrait);
                            Portrait->set(portrait, original == kPortraitAngry ? kPortraitNormal
                                                                               : kPortraitAngry);
                            verify();
                            Portrait->set(portrait, original);
                            verify();
                            portraits += 2;
                        }
                    if (individual) {
                        // Panel switching can mask invalidation bugs by repainting the whole
                        // sidebar. Exercise this button's own state redraw without a panel switch
                        // as well.
                        indivCenterBtn->select(false);
                        verify();
                        indivCenterBtn->select(true);
                        verify();
                    }
                }
            }
        ConfMan.setBool("widescreen", originalMode == DisplayMode::Widescreen);
        requestDisplayModeChange();
        applyPendingDisplayMode();
        requireWide(vm->loadGameState(5) == kNoError && getCenterActorID() == originalActor &&
                        centerActorCoords() == originalPosition &&
                        vm->_rnd->getSeed() == originalSeed,
                    "button fixture was not restored");
        std::ofstream(output / "buttons.json")
            << "{\"status\":\"passed\",\"immediate_redraw_checks\":" << checks
            << ",\"selections\":" << selections << ",\"aggression_banding_toggles\":" << toggles
            << ",\"portrait_changes\":" << portraits
            << ",\"layouts\":4,\"repetitions\":2,\"captures\":24,\"fixture_restored\":true}\n";
    }

    void modalTick() {
        requireWide(++modalTicks < 80, "modal input timed out");
        auto dialog = Saga2::ModalWindow::_current;
        requireWide(dialog != nullptr, "missing modal");
        const auto r = dialog->getExtent();
        if (activeDisplayMode == DisplayMode::Widescreen)
            requireWide(r.x == (854 - r.width) / 2, "dialog is not centered");
        if (action == Inspect) {
            if (modalPhase++ == 0) {
                if (inspectingExploration) {
                    auto map = Saga2::pAutoMap;
                    requireWide(map && map == dialog && !map->getCheatFlag() &&
                                    map->_trackPos == Saga2::centerActorCoords() &&
                                    map->_centerCoords == (map->_trackPos >> metaShift) &&
                                    Saga2::g_vm->_currentMapNum == explorationMap &&
                                    Saga2::getCenterActor()->world()->_mapNum == explorationMap,
                                "brother's map has the wrong position, world or exploration mode");
                    g_platform->capture(output /
                                        String::format("exploration-%s-%d-%d.ppm",
                                                       activeDisplayMode == DisplayMode::Widescreen
                                                           ? "wide"
                                                           : "classic",
                                                       explorationBrother, explorationStep)
                                            .c_str());
                } else
                    g_platform->capture(output / "wide-modal.ppm");
                key(SDLK_ESCAPE);
            }
            return;
        }
        if (modalPhase == 0) {
            options = dialog;
            requireWide(r.height == 263 && r.y == 72, "Options geometry changed");
            auto wide = dialog->hitTest(Saga2::Point16(223, 168));
            auto night = dialog->hitTest(Saga2::Point16(223, 145));
            auto weapon = dialog->hitTest(Saga2::Point16(23, 168));
            requireWide(wide && night && weapon, "missing Options checkbox");
            const auto extent = wide->getExtent();
            requireWide(extent.x == night->getExtent().x && extent.y == weapon->getExtent().y &&
                            extent.width == 18 && extent.height == 17,
                        "Widescreen checkbox is not aligned");
            requireWide(Saga2::TextWidth(&Saga2::Helv11Font, "Widescreen (16:9)", -1, 0) <= 95,
                        "Widescreen label does not fit its column");
            optionsLayouts |= 1 << ((activeDisplayMode == DisplayMode::Widescreen ? 2 : 0) +
                                    (pregameMenu ? 1 : 0));
            requested = !ConfMan.getBool("widescreen");
            checkboxPixels = {};
            checkboxPixels[!requested] = region(r.x + 214, r.y + 160, 18, 17);
            labelPixels = region(r.x + 235, r.y + 160, 95, 17);
            checkboxClicks = 1;
            g_platform->capture(output / "wide-options.ppm");
            click(dialog, 223, 168);
            modalPhase = 1;
        } else if (modalPhase == 1) {
            const bool selected = checkboxClicks % 2 ? requested : !requested;
            if (ConfMan.getBool("widescreen") != selected)
                return;
            requireWide((activeDisplayMode == DisplayMode::Widescreen) != requested,
                        "mode changed before Options closed");
            const auto pixels = region(r.x + 214, r.y + 160, 18, 17);
            if (checkboxPixels[selected].empty())
                checkboxPixels[selected] = pixels;
            else
                requireWide(checkboxPixels[selected] == pixels,
                            "checkbox retained pixels from the other state");
            requireWide(labelPixels == region(r.x + 235, r.y + 160, 95, 17),
                        "checkbox redraw changed its label");
            const std::string capture =
                std::string("options-") +
                (activeDisplayMode == DisplayMode::Widescreen ? "wide-" : "classic-") +
                (pregameMenu ? "pregame-" : "game-") + (selected ? "checked" : "unchecked");
            const int captureBit = 1 << ((activeDisplayMode == DisplayMode::Widescreen ? 4 : 0) +
                                         (pregameMenu ? 2 : 0) + (selected ? 1 : 0));
            if (!(optionsCaptures & captureBit)) {
                g_platform->capture(output / (capture + ".ppm"));
                optionsCaptures |= captureBit;
            }
            if (checkboxClicks < 5) {
                // Exercise right, bottom and bottom-right pixels as real SDL clicks.
                const Point points[] = {{231, 168}, {223, 176}, {231, 176}, {223, 168}};
                const auto point = points[checkboxClicks - 1];
                ++checkboxClicks;
                click(dialog, point.x, point.y);
                return;
            }
            if (action == Toggle && pregameMenu)
                static_cast<Saga2::requestInfo*>(options->_userData)->running = false;
            else
                key(action == Save      ? SDLK_S
                    : action == Load    ? SDLK_L
                    : action == NewGame ? SDLK_N
                                        : SDLK_ESCAPE);
            modalPhase = 2;
        } else if (modalPhase == 2 && dialog != options) {
            requireWide(Saga2::textBox != nullptr, "expected save/load list");
            if (action == Save) {
                for (char c : std::string("Widescreen regression"))
                    key(SDL_Keycode(c));
                modalPhase = 3;
            } else {
                requireWide(std::string(Saga2::textBox->getLine(0)) == "Widescreen regression",
                            "Load did not find the saved game");
                key(SDLK_RETURN);
                modalPhase = 4;
            }
        } else if (modalPhase == 3 &&
                   std::string(Saga2::textBox->getLine(0)) == "Widescreen regression") {
            key(SDLK_RETURN);
            modalPhase = 4;
        } else if (modalPhase == 4 && dialog == options && action == Save) {
            key(SDLK_ESCAPE);
            modalPhase = 5;
        }
    }

    void menu(Action next, bool pregame = false) {
        // Match the outer event loop between successive synchronous dialogs.
        if (Saga2::GameMode::_newmodeFlag)
            Saga2::GameMode::update();
        action = next;
        pregameMenu = pregame;
        modalPhase = modalTicks = 0;
        Saga2::OptionsDialog(pregame);
        requireWide(!Saga2::ModalWindow::_current, "Options did not close");
        if (Saga2::GameMode::_newmodeFlag)
            Saga2::GameMode::update();
        const uint32 seed = Saga2::g_vm->_rnd->getSeed();
        const auto actor = Saga2::getCenterActorID();
        const auto position = Saga2::centerActorCoords();
        const bool individual = Saga2::g_vm->_indivControlsFlag;
        requireWide(Saga2::applyPendingDisplayMode(), "pending mode was not applied");
        requireWide((activeDisplayMode == DisplayMode::Widescreen) == requested &&
                        g_platform->getWidth() == (requested ? 854 : 640),
                    "wrong mode after closing Options");
        requireWide(seed == Saga2::g_vm->_rnd->getSeed() && actor == Saga2::getCenterActorID() &&
                        position == Saga2::centerActorCoords() &&
                        individual == Saga2::g_vm->_indivControlsFlag,
                    "transition advanced the simulation or changed the control panel");
    }

    void inspect(const std::function<void()>& open) {
        if (Saga2::GameMode::_newmodeFlag)
            Saga2::GameMode::update();
        action = Inspect;
        modalPhase = modalTicks = 0;
        open();
        requireWide(modalPhase > 0 && !Saga2::ModalWindow::_current,
                    "inspection did not open and close a dialog");
        if (Saga2::GameMode::_newmodeFlag)
            Saga2::GameMode::update();
    }

    void run() {
        auto vm = Saga2::g_vm;
        std::ofstream(output / "startup.json")
            << "{\"widescreen\":" << (ConfMan.getBool("widescreen") ? "true" : "false")
            << ",\"width\":" << g_platform->getWidth() << "}\n";
        if (activeDisplayMode == DisplayMode::Widescreen) {
            ConfMan.setBool("widescreen", false);
            Saga2::requestDisplayModeChange();
            requireWide(Saga2::applyPendingDisplayMode(), "cannot normalize startup mode");
        }
        for (int panel = 0; panel < 2; ++panel) {
            if (panel)
                Saga2::setControlPanelsToIndividualMode(0);
            else
                Saga2::setControlPanelsToTrioMode();
            for (int cycle = 0; cycle < 3; ++cycle) {
                Saga2::updateIndicators();
                Saga2::reDrawScreen();
                const auto sidebar = region(460, 20, 180, 420);
                const auto map = region(20, 20, 440, 420);
                menu(Toggle, cycle == 2);
                requireWide(sidebar == region(674, 20, 180, 420),
                            "sidebar pixels changed in widescreen");
                requireWide(map == region(127, 20, 440, 420), "map zoom or camera center changed");
                auto hit = Saga2::mainWindow->hitTest(Saga2::Point16(660, 300));
                requireWide(hit && hit->getExtent().width == 654,
                            "expanded map does not receive mouse input");
                auto pick =
                    Saga2::pickTilePos(Saga2::Point32(620, 300), Saga2::centerActorCoords());
                Saga2::Point16 screen;
                Saga2::TileToScreenCoords(pick, screen);
                requireWide(ABS(screen.x - 620) <= 4 && ABS(screen.y - 300) <= 4,
                            "expanded map coordinates do not round-trip");
                g_platform->capture(output / (panel ? "wide-individual.ppm" : "wide-group.ppm"));
                menu(Toggle, cycle == 2);
                requireWide(sidebar == region(460, 20, 180, 420),
                            "sidebar did not return to its original position");
            }
        }
        // Options must finish its original deferred save/load before resizing.
        menu(Save);
        menu(Load);
        menu(NewGame, true);
        requireWide(activeDisplayMode == DisplayMode::Widescreen,
                    "New Game discarded the requested display mode");
        requireWide(vm->loadGameState(2) == kNoError && g_platform->getWidth() == 854,
                    "original-save import changed display mode or failed");
        exploration();
        buttonStates();
        inspect([] { Saga2::openAutoMap(); });
        inspect([] { Saga2::openBook(Saga2::resImports->reserved[0]); });

        Saga2::GameObject* bag = nullptr;
        int distance = 32767;
        for (int id = 0; id < Saga2::objectCount; ++id) {
            auto object = Saga2::GameObject::objectAddress(id);
            if (dynamic_cast<Saga2::PhysicalContainerProto*>(object->proto()) &&
                !object->isOpen() && !object->isLocked() &&
                object->world() == Saga2::getCenterActor()->world()) {
                const int d =
                    (object->getWorldLocation() - Saga2::centerActorCoords()).quickHDistance();
                if (d < distance) {
                    bag = object;
                    distance = d;
                }
            }
        }
        requireWide(bag && bag->open(Saga2::getCenterActorID()), "cannot open inventory fixture");
        Saga2::updateContainerWindows();
        auto node = vm->_cnm->find(bag->thisID(), Saga2::ContainerNode::kPhysicalType);
        requireWide(node && node->getWindow(), "missing inventory window");
        auto window = node->getWindow();
        auto extent = window->getExtent();
        const int dx = 854 - extent.width - 5 - extent.x;
        extent.x += dx;
        window->setExtent(extent);
        auto decorated = dynamic_cast<Saga2::DecoratedWindow*>(window);
        requireWide(decorated != nullptr, "inventory has no decorations");
        const int decorationX = decorated->_decorations[0].extent.x;
        menu(Toggle);
        requireWide(bag->isOpen() && node->getWindow() == window &&
                        window->getExtent().x + extent.width <= 640 &&
                        decorated->_decorations[0].extent.x == decorationX,
                    "shrinking closed or stranded the inventory window");
        // Dragging can leave windows partly offscreen; expanding must not move them.
        auto classicPosition = window->getExtent();
        classicPosition.x = -5;
        window->setExtent(classicPosition);
        menu(Toggle);
        requireWide(bag->isOpen() && window->getExtent().x == classicPosition.x,
                    "expanding moved the existing inventory window");
        requireWide(bag->close(Saga2::getCenterActorID()), "cannot close inventory fixture");
        node->markForDelete();
        Saga2::updateContainerWindows();
        ConfMan.flushToDisk();
        g_platform->capture(output / "wide-final.ppm");
        walkingStart = Saga2::centerActorCoords();
        mouse(60, 330, SDL_EVENT_MOUSE_MOTION);
        mouse(60, 330, SDL_EVENT_MOUSE_BUTTON_DOWN);
    }

    void tick() {
        if (busy) {
            if (Saga2::ModalWindow::_current)
                modalTick();
            return;
        }
        busy = true;
        if (frame == 0)
            run();
        else if (frame == 60)
            mouse(60, 330, SDL_EVENT_MOUSE_BUTTON_UP);
        else if (frame == 80) {
            requireWide(optionsLayouts == 15, "not all Options layouts were checked");
            requireWide(optionsCaptures == 255, "not all checkbox states were captured");
            requireWide(Saga2::centerActorCoords() != walkingStart,
                        "clicking the new map area did not move the actor");
            std::ofstream(output / "widescreen.json")
                << "{\"status\":\"passed\",\"toggle_count\":17,"
                   "\"sidebar_pixel_parity\":true,\"map_overlap_parity\":true,"
                   "\"movement_in_expanded_area\":true,\"original_save_loaded\":true,"
                   "\"checkbox_redraw_parity\":true,\"checkbox_edge_clicks\":true,"
                   "\"options_layouts_checked\":4}\n";
            g_platform->quitting = true;
        }
        ++frame;
        busy = false;
    }
};
} // namespace

void configureWidescreenProbe(const Path& output) {
    std::filesystem::create_directories(output);
    auto state = std::make_shared<WidescreenProbe>();
    state->output = output;
    gameFrameHook = [state] { state->tick(); };
}
} // namespace Native
