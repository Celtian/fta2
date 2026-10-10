// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/probe.h"
#include "saga2/saga2.h"

#include "saga2/actor.h"
#include "saga2/automap.h"
#include "saga2/button.h"
#include "saga2/calendar.h"
#include "saga2/code.h"
#include "saga2/contain.h"
#include "saga2/display.h"
#include "saga2/document.h"
#include "saga2/fontlib.h"
#include "saga2/gpointer.h"
#include "saga2/grabinfo.h"
#include "saga2/grequest.h"
#include "saga2/gtextbox.h"
#include "saga2/intrface.h"
#include "saga2/inventory_ui.h"
#include "saga2/localize.h"
#include "saga2/motion.h"
#include "saga2/modal.h"
#include "saga2/player.h"
#include "saga2/script.h"
#include "saga2/methods.r"
#include "saga2/tile.h"
#include "saga2/uidialog.h"

namespace Saga2 {
extern BackWindow* mainWindow;
extern int lockUINest;
extern gTextBox* textBox;
extern WorldMapData* mapList;
extern int16 worldCount;
extern AutoMap* pAutoMap;
extern GfxOwnerSelCompButton *centerBtns[kNumViews], *aggressBtns[kNumViews],
    *bandingBtns[kNumViews];
extern GfxOwnerSelCompButton *indivCenterBtn, *indivAggressBtn, *indivBandingBtn;
extern GfxOwnerSelCompButton *julBtn, *phiBtn, *kevBtn;
extern GfxMultCompButton *portBtns[kNumViews], *indivPortBtn;
extern GfxCompButton* menConBtn;
extern GfxCompButton* trioMentalBtns[kNumViews];
extern gPanelList *trioControls, *indivControls;
extern ReadyContainerView *TrioCviews[kNumViews], *indivCviewTop, *indivCviewBot;
extern StaticPoint16 fineScroll;
extern ObjectID pickedObject;
uint16 *builtinVTableAddress(int16, uint8 *, CallTable **);
uint8 *segmentAddress(uint16, uint16);
void initAutoMap();
void updateMainDisplay();
void updateWindowSection(const Rect16& clip);
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
    // Aim at pixel centers so round-trip floating-point error cannot cross an edge.
    requireWide(
        SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), x + 0.5f, y + 0.5f, &wx, &wy),
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

// Normal parity captures exclude the separately presented cursor. Include its actual
// indexed bitmap here so tooltip evidence also shows the text and its placement.
void captureWithCursor(const Path& output) {
    const int width = g_platform->getWidth(), height = g_platform->getHeight();
    auto pixels = region(0, 0, width, height);
    Saga2::Point16 position;
    auto cursor = Saga2::g_vm->_pointer->getImageCurPos(position);
    for (int y = 0; y < cursor->_size.y; ++y)
        for (int x = 0; x < cursor->_size.x; ++x) {
            const int sx = position.x + x, sy = position.y + y;
            const byte color = cursor->_data[y * cursor->_size.x + x];
            if (color && sx >= 0 && sx < width && sy >= 0 && sy < height)
                pixels[sy * width + sx] = color;
        }
    std::ofstream file(output.c_str(), std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    const auto& colors = g_platform->getPaletteManager()->colors;
    for (auto pixel : pixels)
        file.write(reinterpret_cast<const char*>(colors.data() + pixel * 3), 3);
    requireWide(bool(file), "cannot capture gold tooltip");
}

struct WidescreenProbe {
    Path output;
    bool busy = false, requested = false;
    bool pregameMenu = false;
    int modalPhase = 0, modalTicks = 0, frame = 0;
    bool sidebarBefore = false;
    int sidebarMenus = 0;
    int checkboxClicks = 0, optionsLayouts = 0, optionsCaptures = 0;
    std::array<std::vector<byte>, 2> checkboxPixels;
    std::vector<byte> labelPixels;
    enum Action { Toggle, Save, Load, NewGame, Inspect, Sidebar } action = Toggle;
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

    void sidebarMenu(bool pregame) {
        action = Sidebar;
        pregameMenu = pregame;
        modalPhase = modalTicks = 0;
        sidebarBefore = ConfMan.getBool("alternate_sidebar");
        Saga2::OptionsDialog(pregame);
        requireWide(ConfMan.getBool("alternate_sidebar") != sidebarBefore &&
                        !Saga2::ModalWindow::_current,
                    "sidebar checkbox did not toggle and close");
        if (Saga2::GameMode::_newmodeFlag)
            Saga2::GameMode::update();
        requireWide(Saga2::applyPendingDisplayMode(), "sidebar layout was not applied");
        ++sidebarMenus;
    }

    void sidebarLayout() {
        using namespace Saga2;
        auto vm = g_vm;
        const auto seed = vm->_rnd->getSeed();
        const auto actor = getCenterActorID();
        const auto position = centerActorCoords();
        const auto originalMode = activeDisplayMode;
        requireWide(vm->saveGameState(6, "Sidebar layout checkpoint", false) == kNoError,
                    "cannot checkpoint sidebar fixture");
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
        int goldTooltipChecks = 0, goldInactiveChecks = 0, goldPassiveClicks = 0, footerChecks = 0;
        auto verifyGoldText = [&](const char* expected) {
            Point16 actualOffset, expectedOffset;
            auto cursor = vm->_pointer->getImage(actualOffset);
            const auto size = cursor->_size;
            const std::vector<byte> pixels(cursor->_data, cursor->_data + size.x * size.y);
            vm->_mouseInfo->setText(expected);
            cursor = vm->_pointer->getImage(expectedOffset);
            requireWide(size == cursor->_size && actualOffset == expectedOffset &&
                            pixels ==
                                std::vector<byte>(cursor->_data, cursor->_data + size.x * size.y),
                        "gold tooltip is missing, stale or not cleared");
            ++goldTooltipChecks;
        };
        auto hoverGold = [&] {
            const auto main = mainWindow->getExtent();
            mouse(main.x + 507 + displayLayout().sidebarOffset, main.y + 286,
                  SDL_EVENT_MOUSE_MOTION);
            drain();
        };
        auto verifyGold = [&] {
            const int x = displayLayout().sidebarOffset;
            updateIndicators();
            const auto text = String::format("%s %llu", GOLD_HINT,
                                             static_cast<unsigned long long>(getCarriedGold(
                                                 vm->_playerList[indivBrother]->getActor())));
            const auto main = mainWindow->getExtent();
            auto control = indivControls->hitTest(Point16(486 + x, 265));
            requireWide(control && control != menConBtn, "missing gold hover control");
            if (vm->_toolBase->isMousePanel(control))
                verifyGoldText(
                    text.c_str()); // Inventory updates must refresh a stationary tooltip.
            for (const auto point : {Point16(486 + x, 265), Point16(530 + x, 265),
                                     Point16(486 + x, 307), Point16(530 + x, 307)}) {
                requireWide(indivControls->hitTest(point) == control &&
                                !control->hitTest(Point16(531 + x, point.y)) &&
                                !control->hitTest(Point16(point.x, 308)),
                            "gold hover bounds are inconsistent");
                mouse(main.x + point.x, main.y + point.y, SDL_EVENT_MOUSE_MOTION);
                drain();
                verifyGoldText(text.c_str());
            }
            vm->_toolBase->leavePanel();
            verifyGoldText(nullptr);
            const auto immediate = region(486 + x, 265, 133, 43);
            reDrawScreen();
            requireWide(immediate == region(486 + x, 265, 133, 43),
                        "carry footer retained stale pixels");
            requireWide(control->getExtent() == Rect16(486 + x, 265, 45, 43) &&
                            region(522 + x, 266, 16, 40) == region(567 + x, 266, 16, 40),
                        "gold-to-mass joint differs from the original mass-to-bulk joint");
            for (const auto left : {539, 584}) {
                const auto pie = indivControls->hitTest(Point16(left + x, 274));
                requireWide(pie && pie->getExtent() == Rect16(left + x, 274, 28, 26) &&
                                indivControls->hitTest(Point16(left + x + 27, 299)) == pie,
                            "joined decoration moved a mass or bulk hit region");
            }
            ++footerChecks;
        };
        std::array<uint64, 3> originalGold;
        for (int i = 0; i < 3; ++i)
            originalGold[i] = getCarriedGold(vm->_playerList[i]->getActor());
        int bagProto = -1;
        for (size_t i = 0; i < vm->_objectProtos.size(); ++i)
            if (dynamic_cast<PhysicalContainerProto*>(vm->_objectProtos[i])) {
                bagProto = i;
                break;
            }
        requireWide(bagProto >= 0, "no bag prototype for gold fixture");
        auto create = [&](int prototype, ObjectID parent, int amount) {
            auto object = GameObject::newObject();
            requireWide(object != nullptr, "no spare object for gold fixture");
            object->setProtoNum(prototype);
            object->setExtra(amount);
            object->move(Location(0, 0, 1, parent));
            return object;
        };
        auto bag = create(bagProto, ActorBaseID, 0);
        auto nested = create(bagProto, bag->thisID(), 0);
        auto direct = create(354, ActorBaseID, 12345);
        auto coins = create(354, nested->thisID(), 23456);
        create(352, nested->thisID(), 1); // Jewellery is not spendable Gold Coin currency.
        requireWide(!bag->isOpen() && !nested->isOpen() &&
                        getCarriedGold(vm->_playerList[0]->getActor()) == originalGold[0] + 35801,
                    "nested closed bags or coin stacks were counted incorrectly");
        int mentalClicks = 0, groupMentalClicks = 0, mentalRedrawChecks = 0;
        int mentalPressedChecks = 0, goldChecks = 0;
        int mentalDefaultPositions = 0, mentalPreservedPositions = 0, mentalSavedPositions = 0;
        int hoverCloses = 0, itemHoverCloses = 0;
        int gridCells = 0, gridEdges = 0, gridTooltips = 0, hpRedraws = 0, gridButtonRedraws = 0;
        std::vector<byte> coinPixels;
        for (bool wide : {false, true}) {
            ConfMan.setBool("widescreen", wide);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            const int x = displayLayout().sidebarOffset;
            setCenterBrother(0);
            setControlPanelsToIndividualMode(0);
            updateContainerWindows();
            updateIndicators();
            // Load both historical defaults and a custom position through the original node
            // serializer, then hide/save/reopen each at its accepted position.
            const std::array<Rect16, 3> savedPositions{
                Rect16(478, 114, 158, 215), Rect16(692, 114, 158, 215), Rect16(60, 80, 158, 215)};
            for (int part = 0; part < 3; ++part) {
                ContainerNode node(*vm->_cnm, ActorBaseID, ContainerNode::kMentalType);
                node.getPosition() = savedPositions[part];
                node._mindType = part;
                const auto expected =
                    part == 2 ? savedPositions[part] : Rect16(478 + x, 114, 158, 215);
                for (int repeat = 0; repeat < 2; ++repeat) {
                    MemoryWriteStreamDynamic saved;
                    node.write(&saved);
                    MemoryReadStream input(saved.getData(), saved.size());
                    node.read(&input);
                    node.show();
                    requireWide(node.getWindow()->getExtent() == expected && node._mindType == part,
                                "loaded mental default or custom position was not restored");
                    ++mentalSavedPositions;
                    node.hide();
                }
            }
            reDrawScreen();
            const auto originalPanel = region(460 + x, 20, 180, 420);
            g_platform->capture(
                output /
                String::format("sidebar-original-%s.ppm", wide ? "wide" : "classic").c_str());
            setControlPanelsToTrioMode();
            updateIndicators();
            reDrawScreen();
            const auto originalGroup = region(460 + x, 20, 180, 420);
            setControlPanelsToIndividualMode(0);
            for (bool pregame : {false, true}) {
                sidebarMenu(pregame);
                requireWide(menConBtn->getExtent().x == 559 + x &&
                                indivCenterBtn->getExtent().y == 54 &&
                                indivCviewTop->getExtent().x == 476 + x &&
                                indivCviewTop->getExtent().y == 113,
                            "new sidebar geometry is incorrect");
                sidebarMenu(pregame);
                requireWide(indivControls->hitTest(Point16(486 + x, 265)) == menConBtn,
                            "disabled gold control obscures the original mental control");
                ++goldInactiveChecks;
                if (originalPanel != region(460 + x, 20, 180, 420))
                    g_platform->capture(output / "sidebar-disabled-failed.ppm");
                requireWide(originalPanel == region(460 + x, 20, 180, 420),
                            "disabling the layout did not restore original pixels");
                key(SDLK_TAB);
                drain();
                requireWide(!vm->_indivControlsFlag &&
                                originalGroup == region(460 + x, 20, 180, 420),
                            "disabling the layout did not restore group pixels");
                for (auto button : trioMentalBtns) {
                    const auto r = button->getExtent();
                    requireWide(!button->getEnabled() && !button->hitTest(Point16(r.x, r.y)) &&
                                    !button->hitTest(Point16(r.x + 27, r.y + 26)),
                                "disabled group mental control still receives clicks");
                }
                key(SDLK_TAB);
                drain();
            }
            sidebarMenu(false);
            for (int brother = 0; brother < 3; ++brother) {
                setCenterBrother(brother);
                setControlPanelsToIndividualMode(brother);
                verifyGold();
                ++goldChecks;
                g_platform->capture(
                    output / String::format("sidebar-%s-%d.ppm", wide ? "wide" : "classic", brother)
                                 .c_str());
                hoverGold();
                verifyGoldText(String::format("%s %llu", GOLD_HINT,
                                              static_cast<unsigned long long>(getCarriedGold(
                                                  vm->_playerList[brother]->getActor())))
                                   .c_str());
                captureWithCursor(output / String::format("gold-tooltip-%s-%d.ppm",
                                                          wide ? "wide" : "classic", brother)
                                               .c_str());
                vm->_toolBase->leavePanel();
                verifyGoldText(nullptr);
                const auto icon = region(495 + x, 274, 25, 12);
                if (coinPixels.empty())
                    coinPixels = icon;
                requireWide(icon == coinPixels, "coin artwork changed between brothers or ratios");
                for (bool group : {false, true}) {
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode(brother);
                    auto button = group ? trioMentalBtns[brother] : menConBtn;
                    if (group) {
                        requireWide(!indivControls->getEnabled(),
                                    "gold panel remains enabled in group view");
                        ++goldInactiveChecks;
                    }
                    const auto r = button->getExtent();
                    requireWide(r.x == 559 + x && r.y == 22 + (group ? 150 * brother : 0) &&
                                    r.width == 28 && r.height == 27,
                                "mental button geometry is incorrect");
                    auto panel = group ? trioControls : indivControls;
                    const int top = 22 + (group ? 150 * brother : 0);
                    auto hp = panel->hitTest(Point16(592 + x, top));
                    auto center = group ? centerBtns[brother] : indivCenterBtn;
                    auto band = group ? bandingBtns[brother] : indivBandingBtn;
                    auto aggression = group ? aggressBtns[brother] : indivAggressBtn;
                    auto armor = panel->hitTest(Point16(592 + x, top + 64));
                    ArmorAttributes armorAttributes;
                    vm->_playerList[brother]->getActor()->totalArmorAttributes(armorAttributes);
                    const std::array<gPanel*, 6> cells{button, hp, center, band, aggression, armor};
                    auto stats = vm->_playerList[brother]->getEffStats();
                    const int base = vm->_playerList[brother]->getBaseStats().vitality;
                    const auto healthText =
                        String::format("%s %d/%d", HEALTH_HINT, stats->vitality, base);
                    const auto armorText =
                        String::format(DESC_ARMOR, armorAttributes.damageAbsorbtion,
                                       armorAttributes.damageDivider, armorAttributes.defenseBonus);
                    const std::array<const char*, 6> labels{
                        IDEAS_INVENT,
                        healthText.c_str(),
                        getCenterActorPlayerID() == brother ? ON_CENTER : OFF_CENTER,
                        isBanded(brother) ? ON_BANDED : OFF_BANDED,
                        isAggressive(brother) ? ON_AGRESS : OFF_AGRESS,
                        armorAttributes.damageAbsorbtion == 0 && armorAttributes.defenseBonus == 0
                            ? NO_ARMOR
                            : armorText.c_str()};
                    for (int cell = 0; cell < 6; ++cell) {
                        auto control = cells[cell];
                        requireWide(control != nullptr, "missing grid control");
                        const auto bounds = control->getExtent();
                        requireWide(bounds.x == 559 + x + 33 * (cell % 2) &&
                                        bounds.y == top + 32 * (cell / 2) && bounds.width == 28 &&
                                        bounds.height == 27,
                                    "sidebar cell does not follow the 2 by 3 grid");
                        ++gridCells;
                        for (const auto point :
                             {Point16(bounds.x, bounds.y), Point16(bounds.x + 27, bounds.y),
                              Point16(bounds.x, bounds.y + 26),
                              Point16(bounds.x + 27, bounds.y + 26)}) {
                            requireWide(panel->hitTest(point) == control,
                                        "grid corner belongs to another control");
                            requireWide(!control->hitTest(Point16(bounds.x + 28, point.y)) &&
                                            !control->hitTest(Point16(point.x, bounds.y + 27)),
                                        "grid hit region extends into its gap");
                            ++gridEdges;
                            const auto main = mainWindow->getExtent();
                            mouse(main.x + 100, main.y + 100, SDL_EVENT_MOUSE_MOTION);
                            drain();
                            mouse(main.x + point.x, main.y + point.y, SDL_EVENT_MOUSE_MOTION);
                            drain();
                            Point16 actualOffset, expectedOffset;
                            auto cursor = vm->_pointer->getImage(actualOffset);
                            const auto size = cursor->_size;
                            const std::vector<byte> pixels(cursor->_data,
                                                           cursor->_data + size.x * size.y);
                            // The mental control retains three tooltip regions within its cell.
                            const char* mentalLabels[] = {IDEAS_INVENT, SPELL_INVENT, SKILL_INVENT};
                            vm->_mouseInfo->setText(
                                cell == 0 ? mentalLabels[(point.x - bounds.x) * 3 / 28]
                                          : labels[cell]);
                            cursor = vm->_pointer->getImage(expectedOffset);
                            requireWide(size == cursor->_size && actualOffset == expectedOffset &&
                                            pixels ==
                                                std::vector<byte>(cursor->_data,
                                                                  cursor->_data + size.x * size.y),
                                        "grid corner tooltip belongs to another control");
                            ++gridTooltips;
                        }
                    }
                    auto verifyGrid = [&] {
                        const auto immediate = region(559 + x, top, 61, 91);
                        reDrawScreen();
                        requireWide(immediate == region(559 + x, top, 61, 91),
                                    "compact grid retained stale button or HP pixels");
                    };
                    const auto vitality = stats->vitality;
                    for (int value : {0, 1, base / 4, base / 2, base}) {
                        stats->vitality = value;
                        updateIndicators();
                        verifyGrid();
                        ++hpRedraws;
                    }
                    stats->vitality = vitality;
                    updateIndicators();
                    for (auto control : {center, band, aggression}) {
                        const auto bounds = control->getExtent();
                        const auto main = mainWindow->getExtent();
                        for (const auto point :
                             {Point16(bounds.x, bounds.y), Point16(bounds.x + 27, bounds.y + 26)}) {
                            mouse(main.x + point.x, main.y + point.y, SDL_EVENT_MOUSE_MOTION);
                            drain();
                            mouse(main.x + point.x, main.y + point.y, SDL_EVENT_MOUSE_BUTTON_DOWN);
                            drain();
                            verifyGrid();
                            mouse(main.x + point.x, main.y + point.y, SDL_EVENT_MOUSE_BUTTON_UP);
                            drain();
                            verifyGrid();
                            gridButtonRedraws += 2;
                        }
                    }
                    auto open = [&](int part, int px, int py) {
                        auto existing =
                            vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                        const auto expected =
                            existing ? (existing->getWindow() ? existing->getWindow()->getExtent()
                                                              : existing->getPosition())
                                     : Rect16(478 + x, 114, 158, 215);
                        if (group)
                            setCenterBrother((brother + 1) % 3);
                        const auto main = mainWindow->getExtent();
                        mouse(main.x + r.x + px, main.y + r.y + py, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        Point16 actualOffset, expectedOffset;
                        auto cursor = vm->_pointer->getImage(actualOffset);
                        const auto actualSize = cursor->_size;
                        const std::vector<byte> actualText(
                            cursor->_data, cursor->_data + actualSize.x * actualSize.y);
                        const char* labels[] = {IDEAS_INVENT, SPELL_INVENT, SKILL_INVENT};
                        vm->_mouseInfo->setText(labels[part]);
                        cursor = vm->_pointer->getImage(expectedOffset);
                        requireWide(actualSize == cursor->_size && actualOffset == expectedOffset &&
                                        actualText ==
                                            std::vector<byte>(cursor->_data,
                                                              cursor->_data + cursor->_size.x *
                                                                                  cursor->_size.y),
                                    "mental tooltip did not match the hovered category");
                        mouse(main.x + r.x + px, main.y + r.y + py, SDL_EVENT_MOUSE_BUTTON_DOWN);
                        drain();
                        const auto pressed = region(r.x, r.y, r.width, r.height);
                        if (part == 0 && px == 0 && py == 0)
                            g_platform->capture(
                                output / String::format("sidebar-pressed-%s-%s-%d.ppm",
                                                        wide ? "wide" : "classic",
                                                        group ? "group" : "individual", brother)
                                             .c_str());
                        reDrawScreen();
                        requireWide(pressed == region(r.x, r.y, r.width, r.height),
                                    "pressed mental button retained stale pixels");
                        ++mentalPressedChecks;
                        mouse(main.x + r.x + px, main.y + r.y + py, SDL_EVENT_MOUSE_BUTTON_UP);
                        drain();
                        updateContainerWindows();
                        auto node =
                            vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                        requireWide(
                            node && node->getWindow() && node->_mindType == part &&
                                getCenterActorPlayerID() == brother &&
                                bool(vm->_indivControlsFlag) != group,
                            String::format("mental control mismatch: wide=%d group=%d brother=%d "
                                           "requested=%d actual=%d center=%d panel=%d visible=%d",
                                           wide, group, brother, part, node ? node->_mindType : -1,
                                           getCenterActorPlayerID(), vm->_indivControlsFlag,
                                           node && node->getWindow())
                                .c_str());
                        requireWide(
                            node->getWindow()->getExtent() == expected,
                            "mental window did not follow its default or remembered position");
                        if (existing)
                            ++mentalPreservedPositions;
                        else {
                            ++mentalDefaultPositions;
                            if (part == 0 && px == 0 && py == 0)
                                g_platform->capture(
                                    output / String::format("mental-position-%s-%s-%d.ppm",
                                                            wide ? "wide" : "classic",
                                                            group ? "group" : "individual", brother)
                                                 .c_str());
                        }
                        // Check the normally updated mental and selection/HP pixels first.
                        const auto immediate = region(559 + x, r.y - 2, 64, 93);
                        reDrawScreen();
                        requireWide(immediate == region(559 + x, r.y - 2, 64, 93),
                                    "mental or selection control retained stale pixels");
                        ++mentalClicks;
                        groupMentalClicks += group;
                        ++mentalRedrawChecks;
                        return node;
                    };
                    int closeIndex = 0;
                    auto close = [&] {
                        auto node =
                            vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                        auto window = node->getWindow();
                        auto& view = window->getView();
                        const auto viewBox = view.getExtent();
                        const auto windowBox = window->getExtent();
                        bool itemHovered = false;
                        for (int py = 0; py < viewBox.height && !itemHovered; py += 8)
                            for (int px = 0; px < viewBox.width && !itemHovered; px += 8)
                                if (auto item = view.pickObject(Point16(px, py))) {
                                    mouse(windowBox.x + viewBox.x + px,
                                          windowBox.y + viewBox.y + py, SDL_EVENT_MOUSE_MOTION);
                                    drain();
                                    requireWide(vm->_cnm->_mouseInView &&
                                                    vm->_cnm->_lastPickedObjectID ==
                                                        item->thisID() &&
                                                    vm->_cnm->_mouseText[0],
                                                "inventory hover did not cache its item text");
                                    // Display the actual cached object label without advancing
                                    // simulation.
                                    vm->_mouseInfo->setText(vm->_cnm->_mouseText);
                                    itemHovered = true;
                                    ++itemHoverCloses;
                                }
                        if (!itemHovered) {
                            mouse(windowBox.x + 50, windowBox.y + 4, SDL_EVENT_MOUSE_MOTION);
                            drain();
                        }
                        const bool capture = closeIndex++ == 0;
                        const auto name = String::format("%s-%s-%d", wide ? "wide" : "classic",
                                                         group ? "group" : "individual", brother);
                        if (capture)
                            captureWithCursor(
                                output / (String("tooltip-before-close-") + name + ".ppm").c_str());
                        key(SDLK_ESCAPE);
                        drain();
                        updateContainerWindows();
                        requireWide(
                            !vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType),
                            "mental container did not close");
                        requireWide(!vm->_cnm->_mouseInView && !vm->_cnm->_mouseText[0] &&
                                        vm->_cnm->_lastPickedObjectID == Nothing,
                                    "closed inventory retained delayed tooltip state");
                        Point16 actualOffset, expectedOffset;
                        auto cursor = vm->_pointer->getImage(actualOffset);
                        const auto size = cursor->_size;
                        const std::vector<byte> pixels(cursor->_data,
                                                       cursor->_data + size.x * size.y);
                        vm->_mouseInfo->setText(nullptr);
                        cursor = vm->_pointer->getImage(expectedOffset);
                        requireWide(size == cursor->_size && actualOffset == expectedOffset &&
                                        pixels ==
                                            std::vector<byte>(cursor->_data,
                                                              cursor->_data + cursor->_size.x *
                                                                                  cursor->_size.y),
                                    "Escape retained visible tooltip pixels");
                        ++hoverCloses;
                        if (capture)
                            captureWithCursor(
                                output / (String("tooltip-after-close-") + name + ".ppm").c_str());
                    };
                    for (int part = 0; part < 3; ++part) {
                        // Both corners of every horizontal category, including subdivision
                        // boundaries.
                        open(part, (part * r.width + 2) / 3, 0);
                        close();
                        open(part, ((part + 1) * r.width + 2) / 3 - 1, r.height - 1);
                        close();
                    }
                    auto node = open(0, 0, r.height / 2);
                    auto window = node->getWindow();
                    // The original default window overlaps lower sidebar rows. Move it aside
                    // using the same partial redraw as dragging, before clicking behind it.
                    auto oldExtent = window->getExtent();
                    auto newExtent = oldExtent;
                    newExtent.x = 60;
                    window->setExtent(newExtent);
                    updateWindowSection(oldExtent);
                    updateWindowSection(newExtent);
                    // Switching category for the selected brother must reuse the existing window.
                    click(mainWindow, r.x + r.width - 1, r.y + r.height / 2);
                    drain();
                    updateContainerWindows();
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    requireWide(node && node->getWindow() == window && node->_mindType == 2 &&
                                    window->getExtent() == newExtent,
                                "switching mental category did not reuse its window");
                    ++mentalPreservedPositions;
                    ++mentalClicks;
                    groupMentalClicks += group;
                    const auto immediate = region(559 + x, r.y - 2, 64, 93);
                    if (group)
                        g_platform->capture(output / String::format("sidebar-group-%s-%d.ppm",
                                                                    wide ? "wide" : "classic",
                                                                    brother)
                                                         .c_str());
                    reDrawScreen();
                    requireWide(immediate == region(559 + x, r.y - 2, 64, 93),
                                "reused mental control retained stale pixels");
                    ++mentalRedrawChecks;
                    if (group)
                        open(1, (r.width + 2) / 3, r.height / 2);
                    close();
                }
            }
            setCenterBrother(0);
            setControlPanelsToIndividualMode(0);
            const auto oldGold = region(486 + x, 265, 133, 43);
            hoverGold();
            const auto goldControl = indivControls->hitTest(Point16(507 + x, 286));
            const auto beforeClickSeed = vm->_rnd->getSeed();
            click(mainWindow, 507 + x, 286);
            drain();
            requireWide(vm->_toolBase->isMousePanel(goldControl) && getCenterActorPlayerID() == 0 &&
                            isIndivMode() && vm->_rnd->getSeed() == beforeClickSeed &&
                            oldGold == region(486 + x, 265, 133, 43),
                        "clicking the gold tooltip changed gameplay or its image");
            ++goldPassiveClicks;
            direct->setExtra(54321);
            verifyGold();
            ++goldChecks;
            requireWide(oldGold != region(486 + x, 265, 133, 43) &&
                            getCarriedGold(vm->_playerList[0]->getActor()) ==
                                originalGold[0] + 77777,
                        "gold display did not update after a stack change");
            coins->move(Location(0, 0, 1, ActorBaseID + 1));
            requireWide(getCarriedGold(vm->_playerList[0]->getActor()) == originalGold[0] + 54321 &&
                            getCarriedGold(vm->_playerList[1]->getActor()) ==
                                originalGold[1] + 23456,
                        "transferred gold was counted for the wrong brother");
            hoverGold();
            // Mutating a carried stack while hovered must update the amount without mouse motion.
            coins->move(Location(0, 0, 1, ActorBaseID));
            updateIndicators();
            coins->move(Location(0, 0, 1, ActorBaseID + 1));
            verifyGold();
            ++goldChecks;
            // Loading must recreate all ready views at the active sidebar coordinates.
            requireWide(vm->saveGameState(7, "Sidebar layout fixture", false) == kNoError &&
                            vm->loadGameState(7) == kNoError,
                        "new layout save/load failed");
            for (int i = 0; i < 3; ++i)
                requireWide(TrioCviews[i]->getExtent().x == 476 + x, "loaded trio slots misplaced");
            requireWide(indivCviewTop->getExtent().x == 476 + x &&
                            indivCviewTop->getExtent().y == 113 &&
                            indivCviewBot->getExtent().x == 476 + x,
                        "loaded individual slots misplaced");
            verifyGold();
            ++goldChecks;
            // A zero balance retains the same currency artwork and updates the number.
            std::vector<GameObject*> emptyCoins;
            RecursiveContainerIterator contents(vm->_playerList[2]->getActor());
            GameObject* item = nullptr;
            for (ObjectID id = contents.first(&item); id != Nothing; id = contents.next(&item))
                if (isObject(id) && item->proto() == vm->_objectProtos[354])
                    emptyCoins.push_back(item);
            for (auto item : emptyCoins)
                item->move(Location(0, 0, 1, ActorBaseID + 1));
            setCenterBrother(2);
            setControlPanelsToIndividualMode(2);
            verifyGold();
            requireWide(getCarriedGold(vm->_playerList[2]->getActor()) == 0 &&
                            region(495 + x, 274, 25, 12) == coinPixels,
                        "zero gold balance removed or changed the coin artwork");
            g_platform->capture(
                output / String::format("sidebar-zero-%s.ppm", wide ? "wide" : "classic").c_str());
            ++goldChecks;
            // Reloading destroys fixture object addresses; reacquire them for the next ratio.
            requireWide(vm->loadGameState(6) == kNoError, "cannot reset sidebar fixture");
            bag = create(bagProto, ActorBaseID, 0);
            nested = create(bagProto, bag->thisID(), 0);
            direct = create(354, ActorBaseID, 12345);
            coins = create(354, nested->thisID(), 23456);
            create(352, nested->thisID(), 1);
            sidebarMenu(false);
        }
        ConfMan.setBool("widescreen", originalMode == DisplayMode::Widescreen);
        requestDisplayModeChange();
        applyPendingDisplayMode();
        requireWide(vm->loadGameState(6) == kNoError && getCenterActorID() == actor &&
                        centerActorCoords() == position && vm->_rnd->getSeed() == seed,
                    "sidebar fixture leaked into gameplay");
        for (int i = 0; i < 3; ++i)
            requireWide(getCarriedGold(vm->_playerList[i]->getActor()) == originalGold[i],
                        "fixture gold was not restored");
        requireWide(vm->saveGameState(8, "Sidebar layout checkpoint", false) == kNoError,
                    "cannot save restored sidebar checkpoint");
        // Leave the option enabled so the next process verifies its persisted startup value.
        sidebarMenu(false);
        std::ofstream(output / "sidebar-layout.json")
            << "{\"status\":\"passed\",\"mental_clicks\":" << mentalClicks
            << ",\"group_mental_clicks\":" << groupMentalClicks
            << ",\"mental_redraw_checks\":" << mentalRedrawChecks
            << ",\"mental_pressed_checks\":" << mentalPressedChecks
            << ",\"mental_tooltip_checks\":" << mentalPressedChecks
            << ",\"mental_default_positions\":" << mentalDefaultPositions
            << ",\"mental_preserved_positions\":" << mentalPreservedPositions
            << ",\"mental_saved_positions\":" << mentalSavedPositions
            << ",\"container_hover_closes\":" << hoverCloses
            << ",\"container_item_hover_closes\":" << itemHoverCloses
            << ",\"carry_footer_checks\":" << footerChecks << ",\"gold_checks\":" << goldChecks
            << ",\"options_openings\":" << sidebarMenus << ",\"grid_cells\":" << gridCells
            << ",\"grid_edge_checks\":" << gridEdges << ",\"grid_tooltip_checks\":" << gridTooltips
            << ",\"hp_redraw_checks\":" << hpRedraws
            << ",\"grid_button_redraw_checks\":" << gridButtonRedraws
            << ",\"gold_tooltip_checks\":" << goldTooltipChecks
            << ",\"gold_inactive_checks\":" << goldInactiveChecks
            << ",\"gold_passive_clicks\":" << goldPassiveClicks
            << ",\"fixture_restored\":true,\"preference_enabled\":true}\n";
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
        if (action == Sidebar) {
            if (modalPhase == 0) {
                auto checkbox = dialog->hitTest(Saga2::Point16(223, 168));
                requireWide(checkbox && checkbox->getExtent() == Saga2::Rect16(214, 160, 18, 17),
                            "missing New layout checkbox");
                click(dialog, 231, 176); // Include the bottom-right hit/redraw edge.
                modalPhase = 1;
            } else if (modalPhase == 1 && ConfMan.getBool("alternate_sidebar") != sidebarBefore) {
                const auto name = String::format(
                    "sidebar-options-%s-%s-%s.ppm",
                    activeDisplayMode == DisplayMode::Widescreen ? "wide" : "classic",
                    pregameMenu ? "pregame" : "game", sidebarBefore ? "off" : "on");
                g_platform->capture(output / name.c_str());
                if (pregameMenu)
                    static_cast<Saga2::requestInfo*>(dialog->_userData)->running = false;
                else
                    key(SDLK_ESCAPE);
                modalPhase = 2;
            }
            return;
        }
        if (modalPhase == 0) {
            options = dialog;
            requireWide(r.height == 263 && r.y == 72, "Options geometry changed");
            auto wide = dialog->hitTest(Saga2::Point16(223, 145));
            auto night = dialog->hitTest(Saga2::Point16(23, 214));
            auto aggression = dialog->hitTest(Saga2::Point16(23, 145));
            requireWide(wide && night && aggression, "missing Options checkbox");
            const auto extent = wide->getExtent();
            requireWide(extent.x == 214 && extent.y == aggression->getExtent().y &&
                            extent.width == 18 && extent.height == 17,
                        "Widescreen checkbox is not aligned");
            requireWide(night->getExtent() == Saga2::Rect16(14, 206, 18, 17),
                        "Night checkbox is not at the bottom of the first column");
            requireWide(Saga2::TextWidth(&Saga2::Helv11Font, "Widescreen (16:9)", -1, 0) <= 95,
                        "Widescreen label does not fit its column");
            optionsLayouts |= 1 << ((activeDisplayMode == DisplayMode::Widescreen ? 2 : 0) +
                                    (pregameMenu ? 1 : 0));
            requested = !ConfMan.getBool("widescreen");
            checkboxPixels = {};
            checkboxPixels[!requested] = region(r.x + 214, r.y + 137, 18, 17);
            labelPixels = region(r.x + 235, r.y + 137, 95, 17);
            checkboxClicks = 1;
            g_platform->capture(output / "wide-options.ppm");
            click(dialog, 223, 145);
            modalPhase = 1;
        } else if (modalPhase == 1) {
            const bool selected = checkboxClicks % 2 ? requested : !requested;
            if (ConfMan.getBool("widescreen") != selected)
                return;
            requireWide((activeDisplayMode == DisplayMode::Widescreen) != requested,
                        "mode changed before Options closed");
            const auto pixels = region(r.x + 214, r.y + 137, 18, 17);
            if (checkboxPixels[selected].empty())
                checkboxPixels[selected] = pixels;
            else
                requireWide(checkboxPixels[selected] == pixels,
                            "checkbox retained pixels from the other state");
            requireWide(labelPixels == region(r.x + 235, r.y + 137, 95, 17),
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
                const Point points[] = {{231, 145}, {223, 153}, {231, 153}, {223, 145}};
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
        std::ofstream(output / "sidebar-startup.json")
            << "{\"alternate_sidebar\":"
            << (ConfMan.getBool("alternate_sidebar") ? "true" : "false") << "}\n";
        std::ofstream(output / "clock-startup.json")
            << "{\"show_game_time\":" << (vm->_showGameTime ? "true" : "false") << "}\n";
        verifyContainerOwners(output);
        ConfMan.setBool("alternate_sidebar", false);
        ConfMan.setBool("show_game_time", false); // Compare the original unobstructed map below.
        Saga2::requestDisplayModeChange();
        Saga2::applyPendingDisplayMode();
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
        sidebarLayout();
        ConfMan.setBool("show_game_time", true);
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

static void verifySpellOrdering(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    requireWide(vm->saveGameState(17, "Spell order checkpoint", false) == kNoError,
                "cannot save spell order checkpoint");
    const auto seed = vm->_rnd->getSeed();
    uint32 timestamp = g_platform->getMillis();
    bool quickClick = false;
    int moves = 0, swaps = 0, cancels = 0, wheels = 0, redraws = 0, loads = 0;
    int clicks = 0, captures = 0;
    auto drain = [&] {
        if (GameMode::_newmodeFlag)
            GameMode::update();
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP || event.type == EVENT_MOUSEWHEEL ||
                     event.type == EVENT_FOCUSLOST) {
                // Independent clicks must not accidentally exercise the double-click path.
                if (event.type == EVENT_LBUTTONDOWN)
                    timestamp += quickClick ? 100 : 400;
                vm->_toolBase->handleMouse(event, timestamp);
            }
        }
    };
    auto wheel = [&](int x, int y, float delta) {
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        requireWide(count == 1, "spell fixture needs one window");
        auto window = windows[0];
        SDL_free(windows);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = SDL_GetWindowID(window);
        requireWide(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), x + 0.5f, y + 0.5f,
                                                  &event.wheel.mouse_x, &event.wheel.mouse_y),
                    "cannot map spell wheel");
        event.wheel.y = delta;
        requireWide(SDL_PushEvent(&event), "cannot inject spell wheel");
        drain();
        ++wheels;
    };
    for (int wide = 0; wide < 2; ++wide)
        for (int sidebar = 0; sidebar < 2; ++sidebar)
            for (int group = 0; group < 2; ++group)
                for (int brother = 0; brother < 3; ++brother) {
                    requireWide(vm->loadGameState(17) == kNoError, "cannot reset spell fixture");
                    ConfMan.setBool("widescreen", bool(wide));
                    ConfMan.setBool("alternate_sidebar", bool(sidebar));
                    requestDisplayModeChange();
                    applyPendingDisplayMode();
                    setCenterBrother(brother);
                    updateContainerWindows();
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode(brother);
                    auto node = OpenMindContainer(brother, 1, 1);
                    requireWide(node && node->getWindow(), "cannot open spells");
                    auto window = node->getWindow();
                    const Rect16 box(40, 60, 158, 215);
                    window->setExtent(box);
                    node->getPosition() = box;
                    auto& view = window->getView();
                    const auto containerID = view._containerObject->thisID();
                    // Reuse original spell objects; the checkpoint restores every fixture change.
                    std::vector<ObjectID> spellIDs;
                    for (int id = 0; id < objectCount && spellIDs.size() < 2; ++id) {
                        auto object = GameObject::objectAddress(id);
                        if (dynamic_cast<SkillProto*>(object->proto()) && !object->isTrueSkill() &&
                            isObject(object->IDParent()))
                            spellIDs.push_back(id);
                    }
                    requireWide(spellIDs.size() == 2, "fixture needs two original spells");
                    ContainerIterator iter(view._containerObject);
                    GameObject* child;
                    while (iter.next(&child) != Nothing)
                        child->setLocation(TilePoint(0, 0, 0));
                    auto first = GameObject::objectAddress(spellIDs[0]);
                    auto second = GameObject::objectAddress(spellIDs[1]);
                    first->move(Location(TilePoint(0, 0, 1), containerID));
                    second->move(Location(TilePoint(0, 1, 1), containerID));
                    const auto grid = view.getExtent();
                    const int gx = box.x + grid.x, gy = box.y + grid.y;
                    const int x = gx + view._iconOrigin.x + 16;
                    const int y = gy + view._iconOrigin.y + 16;
                    reDrawScreen();
                    const auto fixtureSeed = vm->_rnd->getSeed();
                    auto identities = [&] {
                        std::vector<ObjectID> ids;
                        ContainerIterator children(GameObject::objectAddress(containerID));
                        GameObject* object;
                        ObjectID id;
                        while ((id = children.next(&object)) != Nothing)
                            ids.push_back(id);
                        std::sort(ids.begin(), ids.end());
                        return ids;
                    };
                    const auto originalIDs = identities();
                    std::array<std::array<int16, kNumManas>, 3> mana{};
                    for (int b = 0; b < 3; ++b)
                        for (int m = 0; m < kNumManas; ++m)
                            mana[b][m] = getPlayerActorAddress(b)->getActor()->getStats()->mana(m);
                    auto parity = [&] {
                        const auto before = region(gx, gy, grid.width, grid.height);
                        reDrawScreen();
                        requireWide(before == region(gx, gy, grid.width, grid.height),
                                    "spell partial redraw differs from full redraw");
                        ++redraws;
                    };
                    auto start = [&](int sx, int sy) {
                        mouse(sx, sy, SDL_EVENT_MOUSE_MOTION);
                        mouse(sx, sy, SDL_EVENT_MOUSE_BUTTON_DOWN);
                        drain();
                        requireWide(!view.isSpellDragging(), "press started drag without motion");
                        mouse(sx + 3, sy + 3, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        requireWide(!view.isSpellDragging(), "sub-threshold motion started drag");
                        mouse(sx + 4, sy, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        requireWide(view.isSpellDragging() && first->IDParent() == containerID,
                                    "spell drag did not begin at four logical pixels");
                    };
                    auto release = [&](int dx, int dy) {
                        mouse(dx, dy, SDL_EVENT_MOUSE_MOTION);
                        mouse(dx, dy, SDL_EVENT_MOUSE_BUTTON_UP);
                        drain();
                        requireWide(!view.isSpellDragging() && !vm->_mouseInfo->getObject(),
                                    "spell release retained drag or cursor");
                        parity();
                    };
                    auto location = [&](GameObject* object, int row, int col) {
                        requireWide(object->IDParent() == containerID &&
                                        object->getLocation() == TilePoint(row, col, 1),
                                    "spell location or owner changed unexpectedly");
                    };
                    // Ordinary click and slight motion keep the original click-to-use cursor.
                    mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                    mouse(x, y, SDL_EVENT_MOUSE_BUTTON_DOWN);
                    mouse(x + 3, y + 3, SDL_EVENT_MOUSE_MOTION);
                    mouse(x + 3, y + 3, SDL_EVENT_MOUSE_BUTTON_UP);
                    drain();
                    requireWide(vm->_mouseInfo->getObject() == first && !view.isSpellDragging(),
                                "spell ordering broke ordinary selection");
                    vm->_mouseInfo->replaceObject();
                    ++clicks;
                    start(x, y);
                    release(x + 72, y);
                    location(first, 0, 2);
                    location(second, 0, 1);
                    ++moves;
                    start(x + 72, y);
                    release(x + 36, y);
                    location(first, 0, 1);
                    location(second, 0, 2);
                    ++swaps;
                    start(x + 36, y);
                    release(x + 36, y);
                    location(first, 0, 1);
                    ++cancels;
                    start(x + 36, y);
                    release(10, 10);
                    location(first, 0, 1);
                    ++cancels;
                    // Wheel remains blocked on a mere press and on covered grids.
                    mouse(x + 36, y, SDL_EVENT_MOUSE_MOTION);
                    mouse(x + 36, y, SDL_EVENT_MOUSE_BUTTON_DOWN);
                    drain();
                    wheel(x + 36, y, -1);
                    requireWide(view._scrollPosition == 0, "press allowed wheel before drag");
                    mouse(x + 40, y, SDL_EVENT_MOUSE_MOTION);
                    drain();
                    wheel(x + 40, y, -0.25f);
                    requireWide(view._scrollPosition == 0, "fractional drag wheel moved early");
                    wheel(x + 40, y, -0.75f);
                    requireWide(view._scrollPosition == 1,
                                "fractional drag wheel did not accumulate");
                    wheel(gx - 1, gy, -1);
                    requireWide(view._scrollPosition == 1, "wheel outside spell grid moved it");
                    wheel(x, y, -100);
                    requireWide(view._scrollPosition == 16, "drag did not reach last four rows");
                    release(x + 108, y + 108);
                    location(first, 19, 3);
                    ++moves;
                    start(x + 108, y + 108);
                    wheel(x, y, 100);
                    requireWide(view._scrollPosition == 0, "drag did not return to first row");
                    release(gx, gy);
                    location(first, 0, 0);
                    ++moves;
                    // Last included pixel of the grid resolves the last visible slot.
                    start(x, y);
                    release(gx + grid.width - 1, gy + grid.height - 1);
                    location(first, 3, 3);
                    ++moves;
                    start(x + 108, y + 108);
                    release(gx + grid.width, gy + grid.height - 1);
                    location(first, 3, 3);
                    ++cancels;
                    start(x + 108, y + 108);
                    release(gx + grid.width - 1, gy + grid.height);
                    location(first, 3, 3);
                    ++cancels;
                    start(x + 108, y + 108);
                    auto cover = new ContainerNode(*vm->_cnm, ActorBaseID + (brother + 1) % 3,
                                                   ContainerNode::kMentalType);
                    cover->show();
                    cover->getWindow()->setExtent(box);
                    reDrawScreen();
                    const auto row = view._scrollPosition;
                    wheel(x, y, -1);
                    requireWide(view._scrollPosition == row &&
                                    cover->getView()->_scrollPosition == 0,
                                "drag wheel reached a covered or foreign grid");
                    release(x, y);
                    location(first, 3, 3);
                    delete cover;
                    reDrawScreen();
                    ++cancels;
                    start(x + 108, y + 108);
                    OpenMindContainer(brother, 1, 0);
                    requireWide(!view.isSpellDragging() && !vm->_mouseInfo->getObject(),
                                "category switch retained a spell drag");
                    mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                    drain();
                    OpenMindContainer(brother, 1, 1);
                    location(first, 3, 3);
                    window->update(grid);
                    parity();
                    ++cancels;
                    start(x + 108, y + 108);
                    SDL_Event lost{};
                    lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                    requireWide(SDL_PushEvent(&lost), "cannot inject focus loss");
                    drain();
                    requireWide(!view.isSpellDragging() && !vm->_mouseInfo->getObject(),
                                "focus loss retained spell drag");
                    mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                    drain();
                    location(first, 3, 3);
                    ++cancels;
                    start(x + 108, y + 108);
                    requireWide(vm->saveGameState(18, "Ordered spells fixture", false) == kNoError,
                                "cannot save reordered spells");
                    requireWide(!view.isSpellDragging() && !vm->_mouseInfo->getObject(),
                                "saving retained a spell drag");
                    mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                    drain();
                    ++cancels;
                    requireWide(vm->_rnd->getSeed() == fixtureSeed,
                                "reordering or cancelling consumed RNG");
                    requireWide(identities() == originalIDs,
                                "spell drag created, removed or transferred an object");
                    for (int b = 0; b < 3; ++b)
                        for (int m = 0; m < kNumManas; ++m)
                            requireWide(
                                mana[b][m] ==
                                    getPlayerActorAddress(b)->getActor()->getStats()->mana(m),
                                "spell drag consumed mana");
                    requireWide(vm->loadGameState(18) == kNoError, "cannot load ordered spells");
                    updateContainerWindows();
                    first = GameObject::objectAddress(spellIDs[0]);
                    second = GameObject::objectAddress(spellIDs[1]);
                    location(first, 3, 3);
                    location(second, 0, 2);
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    requireWide(node && node->_mindType == 1 && node->getWindow(),
                                "ordered spell window did not reload");
                    auto beginCurrent = [&] {
                        node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                        requireWide(node && node->getWindow(), "spell cancellation lost window");
                        mouse(x + 108, y + 108, SDL_EVENT_MOUSE_MOTION);
                        mouse(x + 108, y + 108, SDL_EVENT_MOUSE_BUTTON_DOWN);
                        mouse(x + 112, y + 108, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        requireWide(node->getView()->isSpellDragging(),
                                    "cannot restart spell drag");
                    };
                    auto cancelled = [&] {
                        requireWide(!vm->_mouseInfo->getObject(), "cancel retained spell cursor");
                        location(first, 3, 3);
                        location(second, 0, 2);
                        mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                        drain();
                        ++cancels;
                    };
                    beginCurrent();
                    ConfMan.setBool("widescreen", !bool(wide));
                    requestDisplayModeChange();
                    applyPendingDisplayMode();
                    cancelled();
                    ConfMan.setBool("widescreen", bool(wide));
                    requestDisplayModeChange();
                    applyPendingDisplayMode();
                    beginCurrent();
                    setCenterBrother((brother + 1) % 3);
                    updateContainerWindows();
                    cancelled();
                    setCenterBrother(brother);
                    updateContainerWindows();
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode(brother);
                    beginCurrent();
                    requireWide(vm->loadGameState(18) == kNoError, "cannot load during spell drag");
                    updateContainerWindows();
                    first = GameObject::objectAddress(spellIDs[0]);
                    second = GameObject::objectAddress(spellIDs[1]);
                    cancelled();
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    // Original map clicking must consume the selected spell via the world handler.
                    click(node->getWindow(), grid.x + 3 + 108 + 16, grid.y + 108 + 16);
                    drain();
                    requireWide(vm->_mouseInfo->getObject() == first,
                                "cannot select reordered spell for map use");
                    mouse(220, 240, SDL_EVENT_MOUSE_MOTION);
                    mouse(220, 240, SDL_EVENT_MOUSE_BUTTON_DOWN);
                    mouse(220, 240, SDL_EVENT_MOUSE_BUTTON_UP);
                    drain();
                    requireWide(!vm->_mouseInfo->getObject(),
                                "map click did not consume spell cursor");
                    location(first, 3, 3);
                    ++clicks;
                    // Reload retires the queued world action without advancing the simulation.
                    requireWide(vm->loadGameState(18) == kNoError, "cannot reset map spell action");
                    updateContainerWindows();
                    first = GameObject::objectAddress(spellIDs[0]);
                    second = GameObject::objectAddress(spellIDs[1]);
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    click(node->getWindow(), grid.x + 3 + 108 + 16, grid.y + 108 + 16);
                    drain();
                    requireWide(vm->_mouseInfo->getObject() == first, "double-click setup failed");
                    quickClick = true;
                    click(node->getWindow(), grid.x + 3 + 108 + 16, grid.y + 108 + 16);
                    drain();
                    quickClick = false;
                    requireWide(!vm->_mouseInfo->getObject() &&
                                    getPlayerActorAddress(brother)->getActor()->_moveTask,
                                "spell double-click did not queue the original use action");
                    location(first, 3, 3);
                    ++clicks;
                    requireWide(vm->loadGameState(18) == kNoError,
                                "cannot reset double-click action");
                    updateContainerWindows();
                    first = GameObject::objectAddress(spellIDs[0]);
                    second = GameObject::objectAddress(spellIDs[1]);
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    reDrawScreen();
                    g_platform->capture(output / String::format("spell-order-%d-%d-%d-%d.ppm", wide,
                                                                sidebar, group, brother).c_str());
                    ++captures;
                    ++loads;
                    beginCurrent();
                    key(SDLK_ESCAPE);
                    drain();
                    updateContainerWindows();
                    requireWide(
                        !vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType) &&
                            vm->_cnm->_mouseText[0] == 0,
                        "Escape retained reordered spell window or tooltip");
                    cancelled();
                }
    requireWide(vm->loadGameState(17) == kNoError, "cannot restore spell order fixture");
    vm->_rnd->setSeed(seed);
    std::ofstream(output / "spell-order.json")
        << "{\"status\":\"passed\",\"layouts\":4,\"panels\":2,\"brothers\":3,\"moves\":" << moves
        << ",\"swaps\":" << swaps << ",\"cancels\":" << cancels << ",\"wheel_checks\":" << wheels
        << ",\"redraw_checks\":" << redraws << ",\"click_checks\":" << clicks
        << ",\"save_load_checks\":" << loads << ",\"captures\":" << captures
        << ",\"fixture_restored\":true}\n";
}

static void verifyMentalScrolling(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    requireWide(vm->saveGameState(15, "Mental scroll checkpoint", false) == kNoError,
                "cannot save mental scroll checkpoint");
    const auto seed = vm->_rnd->getSeed();
    auto drain = [&] {
        if (GameMode::_newmodeFlag)
            GameMode::update();
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP || event.type == EVENT_MOUSEWHEEL)
                vm->_toolBase->handleMouse(event, g_platform->getMillis());
        }
    };
    auto wheel = [&](int x, int y, float delta) {
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        requireWide(count == 1, "mental wheel fixture needs one SDL window");
        auto window = windows[0];
        SDL_free(windows);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = SDL_GetWindowID(window);
        requireWide(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), x + 0.5f, y + 0.5f,
                                                  &event.wheel.mouse_x, &event.wheel.mouse_y),
                    "cannot map mental wheel");
        event.wheel.y = delta;
        requireWide(SDL_PushEvent(&event), "cannot inject mental wheel");
        drain();
    };
    int wheels = 0, numbers = 0, redraws = 0, categories = 0, interactions = 0, loads = 0;
    for (int wide = 0; wide < 2; ++wide)
        for (int sidebar = 0; sidebar < 2; ++sidebar) {
            ConfMan.setBool("widescreen", bool(wide));
            ConfMan.setBool("alternate_sidebar", bool(sidebar));
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int group = 0; group < 2; ++group)
                for (int brother = 0; brother < 3; ++brother) {
                    for (int b = 0; b < 3; ++b)
                        if (auto old = vm->_cnm->find(ActorBaseID + b, ContainerNode::kMentalType))
                            delete old;
                    setCenterBrother(brother);
                    updateContainerWindows();
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode(brother);
                    auto node = OpenMindContainer(brother, 1, 0);
                    requireWide(node && node->getWindow(), "cannot open mental scroll fixture");
                    auto window = node->getWindow();
                    auto box = Rect16(30, 65, 158, 215);
                    window->setExtent(box);
                    for (int part = 0; part < 3; ++part) {
                        click(window, part == 0 ? 55 : part == 1 ? 75 : 88, 10);
                        drain();
                        auto& view = window->getView();
                        requireWide(node->_mindType == part && view._scrollPosition == 0 &&
                                        view._wheelRemainder == 0 && view._totalRows == 20 &&
                                        view._visibleRows == 4 && view._visibleCols == 4,
                                    "mental category changed capacity or retained scroll offset");
                        auto container = view._containerObject;
                        std::vector<std::pair<ObjectID, TilePoint>> children;
                        ContainerIterator iter(container);
                        GameObject* child;
                        while (iter.next(&child) != Nothing)
                            children.emplace_back(child->thisID(), child->getLocation());
                        ObjectID borrowedParent = Nothing;
                        if (children.empty()) {
                            // Empty original categories borrow one existing intangible for this
                            // isolated test, then restore its location and original parent.
                            for (int id = 0; id < objectCount; ++id) {
                                auto candidate = GameObject::objectAddress(id);
                                if (dynamic_cast<IntangibleObjProto*>(candidate->proto()) &&
                                    isObject(candidate->IDParent())) {
                                    borrowedParent = candidate->IDParent();
                                    children.emplace_back(id, candidate->getLocation());
                                    break;
                                }
                            }
                        }
                        requireWide(!children.empty(), "mental fixture needs an original ability");
                        // Keep the actual category's abilities, temporarily out of visible rows.
                        for (const auto& [id, location] : children)
                            GameObject::objectAddress(id)->move(
                                Location(TilePoint(19, 0, 1), container->thisID()));
                        auto item = GameObject::objectAddress(children.front().first);
                        item->move(Location(TilePoint(19, 3, 1), container->thisID()));
                        reDrawScreen();
                        const auto grid = view.getExtent();
                        const int gx = box.x + grid.x, gy = box.y + grid.y;
                        const int x = gx + view._iconOrigin.x + 16;
                        const int y = gy + view._iconOrigin.y + 16;
                        auto position = [&](int expected) {
                            requireWide(view._scrollPosition == expected,
                                        "mental wheel position mismatch");
                            ++wheels;
                        };
                        auto parity = [&] {
                            const auto before = region(gx, gy, grid.width, grid.height);
                            reDrawScreen();
                            requireWide(before == region(gx, gy, grid.width, grid.height),
                                        "mental immediate redraw differs from full redraw");
                            ++redraws;
                        };
                        auto slot = [&](int row, int col) {
                            return region(gx + view._iconOrigin.x + col * 36,
                                          gy + view._iconOrigin.y + row * 36, 32, 32);
                        };
                        const auto first = slot(0, 0);
                        requireWide(std::count(first.begin(), first.end(), byte(37)) > 0 &&
                                        std::count(first.begin(), first.end(), byte(24)) > 0,
                                    "mental placeholder is missing muted digits or outline");
                        ++numbers;
                        mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        wheel(x, y, -0.25f);
                        position(0);
                        wheel(x, y, -0.75f);
                        position(1);
                        parity();
                        requireWide(first != slot(0, 0), "mental numbering did not advance");
                        ++numbers;
                        wheel(x, y, -100);
                        position(16);
                        parity();
                        wheel(x, y, -1);
                        position(16);
                        wheel(x, y, 1);
                        position(15);
                        wheel(x, y, 100);
                        position(0);
                        parity();
                        wheel(x, y, -0.5f);
                        mouse(10, 10, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        wheel(x, y, -0.5f);
                        position(0);
                        wheel(x, y, -0.5f);
                        position(1);
                        wheel(gx - 1, gy, -1);
                        position(1);
                        wheel(gx, gy - 1, -1);
                        position(1);
                        wheel(gx, gy, -1);
                        position(2);
                        wheel(gx + grid.width - 1, gy + grid.height - 1, -1);
                        position(3);
                        wheel(gx + grid.width, gy, -1);
                        position(3);
                        wheel(gx, gy + grid.height, -1);
                        position(3);
                        vm->_cnm->_objToGet = item;
                        wheel(x, y, -1);
                        position(3);
                        vm->_cnm->_objToGet = nullptr;
                        mouse(x, y, SDL_EVENT_MOUSE_BUTTON_DOWN);
                        drain();
                        wheel(x, y, -1);
                        position(3);
                        mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                        drain();
                        // Cover this grid with another brother's real mental window.
                        auto cover = new ContainerNode(*vm->_cnm, ActorBaseID + (brother + 1) % 3,
                                                       ContainerNode::kMentalType);
                        cover->show();
                        cover->getWindow()->setExtent(box);
                        wheel(x, y, -1);
                        position(3);
                        requireWide(cover->getView()->_scrollPosition == 1,
                                    "frontmost mental window did not receive wheel");
                        delete cover;
                        wheel(x, y, -100);
                        position(16);
                        const int ix = x + 3 * 36, iy = y + 3 * 36;
                        mouse(ix, iy, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        requireWide(view.pickObject(Point16(view._iconOrigin.x + 3 * 36 + 16,
                                                            view._iconOrigin.y + 3 * 36 + 16)) ==
                                            item &&
                                        vm->_cnm->_lastPickedObjectID == item->thisID(),
                                    "last mental row does not resolve its actual ability");
                        const auto occupied = slot(3, 3);
                        // Use the original take/replace path on a newly revealed ability.
                        click(window, grid.x + view._iconOrigin.x + 3 * 36 + 16,
                              grid.y + view._iconOrigin.y + 3 * 36 + 16);
                        drain();
                        requireWide(
                            vm->_mouseInfo->getObject() == (item->isTrueSkill() ? nullptr : item),
                            String::format(
                                "mental selection %d/%d/%d/%d/%d item=%u trueSkill=%d held=%u",
                                wide, sidebar, group, brother, part, item->thisID(),
                                item->isTrueSkill(), vm->_mouseInfo->getObjectId())
                                .c_str());
                        if (item->isTrueSkill())
                            vm->_mouseInfo->copyObject(item);
                        wheel(x, y, 1);
                        position(15);
                        vm->_mouseInfo->replaceObject();
                        wheel(x, y, -1);
                        position(16);
                        window->update(grid);
                        parity();
                        requireWide(slot(3, 3) == occupied,
                                    "mental take/replace changed ability artwork");
                        ++interactions;
                        item->move(Location(TilePoint(18, 3, 1), container->thisID()));
                        window->update(grid);
                        parity();
                        const auto last = slot(3, 3);
                        requireWide(last != occupied &&
                                        std::count(last.begin(), last.end(), byte(37)) > 0,
                                    "slot 80 did not regain its placeholder");
                        ++numbers;
                        mouse(10, 10, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        mouse(ix, iy - 36, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        requireWide(vm->_cnm->_lastPickedObjectID == item->thisID(),
                                    "mental hover did not follow moved ability");
                        wheel(ix, iy - 36, 1);
                        position(15);
                        requireWide(vm->_cnm->_lastPickedObjectID == Nothing &&
                                        vm->_cnm->_mouseText[0] == 0,
                                    "mental wheel retained stale tooltip under stationary pointer");
                        wheel(x, y, -0.5f);
                        OpenMindContainer(brother, 1, (part + 1) % 3);
                        requireWide(view._scrollPosition == 0 && view._wheelRemainder == 0,
                                    "sidebar category path did not reset mental scroll");
                        ++categories;
                        OpenMindContainer(brother, 1, part);
                        wheel(x, y, -2.5f);
                        position(2);
                        OpenMindContainer(brother, 1, part);
                        requireWide(view._scrollPosition == 2 && view._wheelRemainder == -0.5f,
                                    "same category unexpectedly reset mental scroll");
                        ++categories;
                        // Keyboard changes must use the same reset, including a stationary grid
                        // cursor.
                        mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                        drain();
                        key(part == 0 ? SDLK_S : part == 1 ? SDLK_K : SDLK_I);
                        drain();
                        requireWide(node->_mindType == (part + 1) % 3 &&
                                        view._scrollPosition == 0 && view._wheelRemainder == 0,
                                    "keyboard category path did not reset mental scroll");
                        ++categories;
                        OpenMindContainer(brother, 1, part);
                        window->update(grid);
                        parity();
                        for (const auto& [id, location] : children)
                            GameObject::objectAddress(id)->move(
                                Location(location, borrowedParent == Nothing ? container->thisID()
                                                                             : borrowedParent));
                        wheel(x, y, -16);
                        position(16);
                        window->update(grid);
                        parity();
                        g_platform->capture(output /
                                            String::format("mental-scroll-%d-%d-%d-%d-%d.ppm", wide,
                                                           sidebar, group, brother, part)
                                                .c_str());
                    }
                    // Moving the window retains scroll and uses its current grid bounds.
                    box.x = 70;
                    box.y = 90;
                    window->setExtent(box);
                    reDrawScreen();
                    wheel(box.x + 20, box.y + 80, 1);
                    requireWide(window->getView()._scrollPosition == 15,
                                "moved mental window used old wheel bounds");
                    ++wheels;
                    node->getPosition() = box;
                    requireWide(vm->saveGameState(16, "Mental scroll load fixture", false) ==
                                        kNoError &&
                                    vm->loadGameState(16) == kNoError,
                                "cannot reload mental scroll fixture");
                    updateContainerWindows();
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    requireWide(
                        node && node->getWindow() && node->_mindType == 2 &&
                            node->getWindow()->getExtent() == box &&
                            node->getView()->_scrollPosition == 0 &&
                            node->getView()->_wheelRemainder == 0,
                        String::format(
                            "mental load %d/%d/%d/%d: node=%p window=%p type=%d rect=%d,%d row=%d "
                            "remainder=%f center=%u",
                            wide, sidebar, group, brother, node, node ? node->getWindow() : nullptr,
                            node ? node->_mindType : -1,
                            node && node->getWindow() ? node->getWindow()->getExtent().x : -1,
                            node && node->getWindow() ? node->getWindow()->getExtent().y : -1,
                            node && node->getView() ? node->getView()->_scrollPosition : -1,
                            node && node->getView() ? node->getView()->_wheelRemainder : -1,
                            getCenterActorID())
                            .c_str());
                    ++loads;
                    key(SDLK_ESCAPE);
                    drain();
                    updateContainerWindows();
                    requireWide(
                        !vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType) &&
                            vm->_cnm->_mouseText[0] == 0,
                        "Escape retained mental window or tooltip");
                }
        }
    requireWide(vm->loadGameState(15) == kNoError, "mental scroll fixture was not restored");
    // Loading reactivates regions; discard any activation RNG used by this isolated fixture.
    vm->_rnd->setSeed(seed);
    std::ofstream(output / "mental-scroll.json")
        << "{\"status\":\"passed\",\"layouts\":4,\"panels\":2,\"brothers\":3,\"categories\":3,"
        << "\"wheel_checks\":" << wheels << ",\"placeholder_checks\":" << numbers
        << ",\"redraw_checks\":" << redraws << ",\"category_checks\":" << categories
        << ",\"ability_interactions\":" << interactions << ",\"save_load_checks\":" << loads
        << ",\"captures\":72,\"fixture_restored\":true}\n";
}

static void verifyMentalOwners(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    requireWide(vm->saveGameState(11, "Mental owner checkpoint", false) == kNoError,
                "cannot save mental owner checkpoint");
    for (int brother = 0; brother < 3; ++brother)
        if (auto node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType))
            delete node;
    auto drain = [&] {
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP)
                vm->_toolBase->handleMouse(event, g_platform->getMillis());
        }
    };
    int redraws = 0, tooltips = 0, edges = 0, passive = 0, closes = 0, loads = 0;
    auto verifyText = [&](const char* expected) {
        Point16 actualOffset, expectedOffset;
        auto image = vm->_pointer->getImage(actualOffset);
        const auto size = image->_size;
        const std::vector<byte> pixels(image->_data, image->_data + size.x * size.y);
        vm->_mouseInfo->setText(expected);
        image = vm->_pointer->getImage(expectedOffset);
        requireWide(size == image->_size && actualOffset == expectedOffset &&
                        pixels == std::vector<byte>(image->_data, image->_data + size.x * size.y),
                    String::format("mental owner tooltip %d expected %s (%d,%d -> %d,%d)", tooltips,
                                   expected ? expected : "empty", size.x, size.y, image->_size.x,
                                   image->_size.y)
                        .c_str());
        ++tooltips;
    };
    static const char* labels[] = {"Julian's abilities", "Phillip's abilities",
                                   "Kevin's abilities"};
    for (int wide = 0; wide < 2; ++wide)
        for (int sidebar = 0; sidebar < 2; ++sidebar) {
            ConfMan.setBool("widescreen", bool(wide));
            ConfMan.setBool("alternate_sidebar", bool(sidebar));
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int group = 0; group < 2; ++group)
                for (int brother = 0; brother < 3; ++brother) {
                    setCenterBrother((brother + 1) % 3);
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode((brother + 1) % 3);
                    auto node = new ContainerNode(*vm->_cnm, ActorBaseID + brother,
                                                  ContainerNode::kMentalType);
                    node->show();
                    auto window = node->getWindow();
                    auto box = window->getExtent();
                    box.x = 30;
                    box.y = 70;
                    window->setExtent(box);
                    reDrawScreen();
                    auto hover = [&](int x, int y) {
                        mouse(box.x + x, box.y + y, SDL_EVENT_MOUSE_MOTION);
                        drain();
                    };
                    auto parity = [&] {
                        const auto pixels = region(box.x + 76, box.y + 42, 25, 21);
                        reDrawScreen();
                        const auto after = region(box.x + 76, box.y + 42, 25, 21);
                        if (pixels != after)
                            for (size_t i = 0; i < pixels.size(); ++i)
                                if (pixels[i] != after[i])
                                    throw std::runtime_error(
                                        String::format("Mental owner redraw %d at %d,%d (%u/%u)",
                                                       redraws, int(i % 25), int(i / 25), pixels[i],
                                                       after[i])
                                            .c_str());
                        ++redraws;
                    };
                    auto marker = window->hitTest(Point16(88, 52));
                    requireWide(marker && marker->getExtent() == Rect16(76, 42, 25, 21) &&
                                    window->getView().getExtent() == Rect16(2, 64, 156, 149) &&
                                    box.width == 158 && box.height == 215,
                                "mental owner marker changed window or grid geometry");
                    for (int part = 0; part < 3; ++part) {
                        // Original header category regions remain available above the badge.
                        click(window, part == 0 ? 55 : part == 1 ? 75 : 88, 10);
                        drain();
                        updateContainerWindows();
                        requireWide(node->getWindow() == window && node->_mindType == part,
                                    "mental header category change did not reuse its window");
                        parity();
                        hover(88, 52);
                        verifyText(labels[brother]);
                        for (auto point : {Point16(76, 42), Point16(100, 42), Point16(76, 62),
                                           Point16(100, 62)}) {
                            requireWide(window->hitTest(point) == marker,
                                        "mental owner marker inside edge misses");
                            hover(point.x, point.y);
                            verifyText(labels[brother]);
                            ++edges;
                        }
                        for (auto point : {Point16(75, 52), Point16(101, 52), Point16(88, 41),
                                           Point16(88, 63)}) {
                            requireWide(window->hitTest(point) != marker,
                                        "mental owner marker extends outside its bounds");
                            ++edges;
                        }
                        vm->_toolBase->leavePanel();
                        verifyText(nullptr);
                        const auto selected = getCenterActorID();
                        const auto seed = vm->_rnd->getSeed();
                        click(window, 88, 52);
                        drain();
                        verifyText(labels[brother]);
                        requireWide(getCenterActorID() == selected && vm->_rnd->getSeed() == seed &&
                                        node->_mindType == part && node->getWindow() == window,
                                    "mental owner badge click changed gameplay or category");
                        ++passive;
                        parity();
                        setCenterBrother((brother + part + 1) % 3);
                        updateIndicators();
                        updateContainerWindows();
                        // Preserve the original selection-driven hide/show behavior.
                        const bool hidden = getCenterActorID() != ActorBaseID + brother;
                        requireWide(bool(node->getWindow()) != hidden,
                                    "selection changed the original mental window visibility");
                        if (hidden)
                            verifyText(nullptr);
                        // Explicitly show an unselected actor's node to check its own identity.
                        node->show();
                        window = node->getWindow();
                        marker = window->hitTest(Point16(88, 52));
                        requireWide(window->getExtent() == box && marker &&
                                        marker->getExtent() == Rect16(76, 42, 25, 21),
                                    "reopened mental owner geometry changed");
                        hover(88, 52);
                        verifyText(labels[brother]);
                        parity();
                        hover(100, 52); // Keep the actual cursor clear of the initial.
                        captureWithCursor(output / String::format("mental-owner-%d-%d-%d-%d-%d.ppm",
                                                                  wide, sidebar, group, brother,
                                                                  part)
                                                       .c_str());
                    }
                    const auto previous = box;
                    box.x = 60;
                    box.y = 80;
                    window->setExtent(box);
                    updateWindowSection(previous);
                    updateWindowSection(box);
                    hover(88, 52);
                    verifyText(labels[brother]);
                    parity();
                    node->hide();
                    verifyText(nullptr);
                    node->show();
                    window = node->getWindow();
                    hover(88, 52);
                    verifyText(labels[brother]);
                    parity();
                    requireWide(vm->saveGameState(12, "Mental owner load fixture", false) ==
                                        kNoError &&
                                    vm->loadGameState(12) == kNoError,
                                "cannot reload mental owner fixture");
                    updateContainerWindows();
                    node = vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType);
                    requireWide(node && node->getWindow() && node->_mindType == 2 &&
                                    node->getWindow()->getExtent() == box,
                                "saved mental owner window was not restored");
                    window = node->getWindow();
                    hover(88, 52);
                    verifyText(labels[brother]);
                    parity();
                    ++loads;
                    key(SDLK_ESCAPE);
                    drain();
                    updateContainerWindows();
                    verifyText(nullptr);
                    requireWide(!vm->_cnm->find(ActorBaseID + brother, ContainerNode::kMentalType),
                                "Escape retained mental owner window");
                    ++closes;
                }
        }
    requireWide(vm->loadGameState(11) == kNoError, "cannot restore mental owner fixture");
    std::ofstream(output / "mental-owner.json")
        << "{\"status\":\"passed\",\"layouts\":4,\"panels\":2,\"brothers\":3,\"categories\":3,"
        << "\"redraw_checks\":" << redraws << ",\"tooltip_checks\":" << tooltips
        << ",\"edge_checks\":" << edges << ",\"passive_clicks\":" << passive
        << ",\"escape_closes\":" << closes << ",\"save_load_checks\":" << loads
        << ",\"captures\":72,\"fixture_restored\":true}\n";
}

static void verifyInventoryUI(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    const bool originalClock = ConfMan.getBool("show_game_time");
    const auto originalCalendar = *vm->_calendar;
    const auto hook = gameFrameHook;
    std::array<GameObject*, 3> bags{};
    int found = 0;
    for (int id = 0; id < objectCount && found < 3; ++id) {
        auto object = GameObject::objectAddress(id);
        if (dynamic_cast<PhysicalContainerProto*>(object->proto()) && !object->isOpen() &&
            !vm->_cnm->find(id))
            bags[found++] = object;
    }
    requireWide(found == 3, "inventory fixture needs three physical containers");
    GameObject* item = nullptr;
    for (int id = 0; id < objectCount && !item; ++id) {
        auto object = GameObject::objectAddress(id);
        if (object->proto() && dynamic_cast<InventoryProto*>(object->proto()) &&
            !dynamic_cast<PhysicalContainerProto*>(object->proto()))
            item = object;
    }
    requireWide(item != nullptr, "inventory fixture needs an inventory item");
    const std::array<ObjectID, 3> bagIDs = {bags[0]->thisID(), bags[1]->thisID(),
                                            bags[2]->thisID()};
    const ObjectID itemID = item->thisID();
    for (auto bag : bags) {
        while (auto child = bag->child())
            child->move(Location(TilePoint(0, 0, 0), getCenterActorID()));
        bag->move(Location(TilePoint(0, 0, 0), getCenterActorID()));
    }
    auto drain = [&] {
        if (GameMode::_newmodeFlag)
            GameMode::update();
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP || event.type == EVENT_MOUSEWHEEL)
                vm->_toolBase->handleMouse(event, g_platform->getMillis());
        }
    };
    auto wheel = [&](int x, int y, float delta) {
        int count = 0;
        auto windows = SDL_GetWindows(&count);
        requireWide(count == 1, "wheel fixture needs one SDL window");
        auto window = windows[0];
        SDL_free(windows);
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = SDL_GetWindowID(window);
        requireWide(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window), x + 0.5f, y + 0.5f,
                                                  &event.wheel.mouse_x, &event.wheel.mouse_y),
                    "cannot map inventory wheel");
        event.wheel.y = delta;
        requireWide(SDL_PushEvent(&event), "cannot inject inventory wheel");
        drain();
    };
    int wheelChecks = 0, placeholders = 0, redraws = 0, options = 0, clocks = 0;
    const auto appearance = bags[0]->proto()->appearanceType;
    for (int wide = 0; wide < 2; ++wide) {
        for (int sidebar = 0; sidebar < 2; ++sidebar) {
            ConfMan.setBool("widescreen", bool(wide));
            ConfMan.setBool("alternate_sidebar", bool(sidebar));
            ConfMan.setBool("show_game_time", false);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int decoration = 0; decoration < 4; ++decoration) {
                bags[0]->proto()->appearanceType = decoration;
                auto node =
                    new ContainerNode(*vm->_cnm, bags[0]->thisID(), ContainerNode::kPhysicalType);
                bags[0]->setFlags(kObjectOpen, kObjectOpen);
                node->show();
                auto window = node->getWindow();
                window->setExtent(
                    Rect16(30, 65, window->getExtent().width, window->getExtent().height));
                reDrawScreen();
                auto& view = window->getView();
                const auto grid = view.getExtent();
                const int gx = 30 + grid.x, gy = 65 + grid.y;
                const int x = gx + view._iconOrigin.x + 16, y = gy + view._iconOrigin.y + 16;
                auto checkPosition = [&](int expected) {
                    requireWide(view._scrollPosition == expected,
                                "inventory wheel position mismatch");
                    ++wheelChecks;
                };
                auto parity = [&] {
                    const auto before = region(gx, gy, 230, grid.height);
                    reDrawScreen();
                    requireWide(before == region(gx, gy, 230, grid.height),
                                "inventory partial redraw differs from full redraw");
                    ++redraws;
                };
                auto slot = [&](int row, int col) {
                    return region(gx + view._iconOrigin.x + col * (32 + view._iconSpacing.x),
                                  gy + view._iconOrigin.y + row * (32 + view._iconSpacing.y), 32,
                                  32);
                };
                const auto first = slot(0, 0);
                const auto number = region(x - 6, y - 6, 12, 12);
                requireWide(std::count(number.begin(), number.end(), byte(37)) > 0 &&
                                std::count(number.begin(), number.end(), byte(24)) > 0,
                            "empty physical slot lacks a readable outlined number");
                ++placeholders;
                mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                drain();
                wheel(x, y, -0.25f);
                checkPosition(0);
                wheel(x, y, -0.75f);
                checkPosition(1);
                parity();
                const auto secondRow = slot(0, 0);
                requireWide(first != secondRow, "slot number did not change after scrolling");
                ++placeholders;
                wheel(x, y, -100);
                checkPosition(view._totalRows - view._visibleRows);
                parity();
                wheel(x, y, 1);
                checkPosition(view._totalRows - view._visibleRows - 1);
                wheel(x, y, 100);
                checkPosition(0);
                parity();
                vm->_mouseInfo->copyObject(item);
                wheel(x, y, -1);
                checkPosition(1);
                vm->_mouseInfo->replaceObject();
                wheel(x, y, 100);
                checkPosition(0);
                wheel(x, y, -0.5f);
                mouse(10, 10, SDL_EVENT_MOUSE_MOTION);
                drain();
                mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                drain();
                wheel(x, y, -0.5f);
                checkPosition(0);
                wheel(x, y, -0.5f);
                checkPosition(1);
                wheel(gx - 1, gy, -1);
                checkPosition(1);
                wheel(gx, gy - 1, -1);
                checkPosition(1);
                wheel(gx, gy, -1);
                checkPosition(2);
                wheel(gx, gy + grid.height - 1, -1);
                checkPosition(3);
                wheel(gx, gy + grid.height, -1);
                checkPosition(3);
                const int right = 30 + window->getExtent().width - 1;
                wheel(right, gy, -1);
                checkPosition(4);
                wheel(right + 1, gy, 1);
                checkPosition(4);
                wheel(gx + grid.width, gy, 1);
                checkPosition(4);
                wheel(x, y, 100);
                checkPosition(0);
                vm->_cnm->_objToGet = item;
                wheel(x, y, -1);
                checkPosition(0);
                vm->_cnm->_objToGet = nullptr;
                mouse(x, y, SDL_EVENT_MOUSE_BUTTON_DOWN);
                drain();
                wheel(x, y, -1);
                checkPosition(0);
                mouse(x, y, SDL_EVENT_MOUSE_BUTTON_UP);
                drain();

                // A second physical window owns the same screen pixels; only it scrolls.
                auto cover =
                    new ContainerNode(*vm->_cnm, bags[1]->thisID(), ContainerNode::kPhysicalType);
                bags[1]->setFlags(kObjectOpen, kObjectOpen);
                cover->show();
                cover->getWindow()->setExtent(window->getExtent());
                wheel(x, y, -1);
                checkPosition(0);
                requireWide(cover->getView()->_scrollPosition == 1,
                            "covered inventory received wheel input");
                ++wheelChecks;
                delete cover;
                bags[1]->setFlags(0, kObjectOpen);

                // Occupied slots never retain their placeholder, even after a transfer back out.
                item->move(Location(TilePoint(0, 0, 1), bags[0]->thisID()));
                window->update(grid);
                const auto occupied = slot(0, 0);
                requireWide(occupied != first, "inventory item did not replace its placeholder");
                parity();
                mouse(10, 10, SDL_EVENT_MOUSE_MOTION);
                drain();
                mouse(x, y, SDL_EVENT_MOUSE_MOTION);
                drain();
                requireWide(vm->_cnm->_lastPickedObjectID == item->thisID() &&
                                vm->_cnm->_mouseText[0] != 0,
                            "inventory hover fixture did not select its item");
                wheel(x, y, -1);
                requireWide(vm->_cnm->_lastPickedObjectID == Nothing &&
                                vm->_cnm->_mouseText[0] == 0,
                            "stationary inventory hover retained scrolled-away object");
                ++wheelChecks;
                wheel(x, y, 100);
                item->move(Location(TilePoint(0, 0, 0), getCenterActorID()));
                window->update(grid);
                requireWide(slot(0, 0) == first, "placeholder did not return after removing item");
                ++placeholders;
                parity();
                g_platform->capture(
                    output /
                    String::format("inventory-%d-%d-%d.ppm", wide, sidebar, decoration).c_str());
                key(SDLK_ESCAPE);
                drain();
                updateContainerWindows();
                requireWide(
                    !vm->_cnm->find(bags[0]->thisID()),
                    String::format(
                        "Escape inventory %d/%d/%d: top=%p expected=%p held=%p modal=%p lock=%d",
                        wide, sidebar, decoration, vm->_toolBase->topWindow(), window,
                        vm->_mouseInfo->getObject(), ModalWindow::_current, lockUINest)
                        .c_str());
            }
            for (int pregame = 0; pregame < 2; ++pregame) {
                if (GameMode::_newmodeFlag)
                    GameMode::update();
                auto beneath =
                    new ContainerNode(*vm->_cnm, bags[0]->thisID(), ContainerNode::kPhysicalType);
                bags[0]->setFlags(kObjectOpen, kObjectOpen);
                beneath->show();
                beneath->getWindow()->setExtent(Rect16(0, 150, 268, 320));
                auto mentalBeneath =
                    new ContainerNode(*vm->_cnm, getCenterActorID(), ContainerNode::kMentalType);
                mentalBeneath->show();
                mentalBeneath->getWindow()->setExtent(Rect16(300, 260, 158, 215));
                int phase = -3;
                ModalWindow* parentOptions = nullptr;
                int helpClosingTicks = 0;
                const bool before = vm->_showGameTime;
                const bool nightBefore = vm->_showNight;
                const int speechBefore = ConfMan.getInt("speech_volume");
                gameFrameHook = [&] {
                    auto dialog = ModalWindow::_current;
                    if (!dialog)
                        return;
                    if (phase == -3) {
                        parentOptions = dialog;
                        key(SDLK_H);
                        ++phase;
                    } else if (phase == -2) {
                        requireWide(dialog != parentOptions && dialog->getExtent().width == 487 &&
                                        dialog->getExtent().height == 336,
                                    "Hot Keys did not open above Options");
                        const auto close = dialog->keyTest(ASCII_ESCAPE);
                        requireWide(close &&
                                        close->getExtent().x ==
                                            (kOptBoxXSzNS - close->getExtent().width) / 2 &&
                                        close->getExtent().y + close->getExtent().height <=
                                            dialog->getExtent().height - 14,
                                    "Hot Keys Close is not centered inside the bottom frame");
                        g_platform->capture(
                            output /
                            String::format("hotkeys-%d-%d-%d.ppm", wide, sidebar, pregame).c_str());
                        if (pregame)
                            click(dialog, close->getExtent().x + close->getExtent().width - 1,
                                  close->getExtent().y + close->getExtent().height - 1);
                        else
                            key(SDLK_ESCAPE);
                        ++phase;
                    } else if (phase == -1) {
                        // The nested event loop can tick once after receiving Close,
                        // before the help window is destroyed and modal focus restored.
                        requireWide(++helpClosingTicks < 10,
                                    "Hot Keys did not restore Options modal focus");
                        if (dialog == parentOptions)
                            ++phase;
                    } else if (phase == 0) {
                        wheel(40, 400, -1);
                        requireWide(beneath->getView()->_scrollPosition == 0,
                                    "modal dialog allowed inventory wheel through");
                        ++wheelChecks;
                        wheel(320, 400, -1);
                        requireWide(mentalBeneath->getView()->_scrollPosition == 0,
                                    "modal dialog allowed mental wheel through");
                        ++wheelChecks;
                        auto control = dialog->hitTest(Point16(231, 199));
                        requireWide(control && control->getExtent() == Rect16(214, 183, 18, 17),
                                    "missing game-time checkbox or incorrect hit bounds");
                        click(dialog, 231, 199);
                        ++phase;
                    } else if (phase == 1 && ConfMan.getBool("show_game_time") != before) {
                        requireWide(vm->_showGameTime == before,
                                    "clock applied before Options closed");
                        g_platform->capture(output / String::format("clock-options-%d-%d-%d.ppm",
                                                                    wide, sidebar, pregame)
                                                         .c_str());
                        click(dialog, 15, 90);
                        ++phase;
                    } else if (phase == 2 && ConfMan.getInt("speech_volume") == 0) {
                        click(dialog, 182, 90);
                        ++phase;
                    } else if (phase == 3 && ConfMan.getInt("speech_volume") == 255) {
                        click(dialog, 99, 90);
                        ++phase;
                    } else if (phase == 4 && ConfMan.getInt("speech_volume") != 255) {
                        requireWide(ConfMan.getInt("speech_volume") > 0,
                                    "Speech Volume midpoint is not audible");
                        auto night = dialog->hitTest(Point16(31, 222));
                        requireWide(night && night->getExtent() == Rect16(14, 206, 18, 17),
                                    "Night is not below Speech Text");
                        click(dialog, 31, 222);
                        ++phase;
                    } else if (phase == 5 && vm->_showNight != nightBefore) {
                        click(dialog, 14, 206);
                        ++phase;
                    } else if (phase == 6 && vm->_showNight == nightBefore) {
                        if (pregame)
                            static_cast<requestInfo*>(dialog->_userData)->running = false;
                        else
                            key(SDLK_ESCAPE);
                        ++phase;
                    }
                };
                OptionsDialog(pregame);
                gameFrameHook = hook;
                if (GameMode::_newmodeFlag)
                    GameMode::update();
                requireWide(phase == 7 && vm->_showGameTime == before,
                            "clock checkbox timing failed");
                Config saved;
                saved.file = ConfMan.file;
                saved.load();
                const int speech = ConfMan.getInt("speech_volume");
                requireWide(saved.getInt("speech_volume") == speech && speech > 0 && speech < 255,
                            "Options did not save the adjusted Speech Volume");
                std::ofstream(
                    output /
                    String::format("options-audio-%d-%d-%d.json", wide, sidebar, pregame).c_str())
                    << "{\"status\":\"passed\",\"speech_levels\":[0," << speech
                    << ",255],\"preference_saved\":true,\"night_bounds\":[14,206,18,17]}\n";
                ConfMan.setInt("speech_volume", speechBefore);
                vm->syncSoundSettings();
                vm->saveConfig();
                applyPendingDisplayMode();
                requireWide(vm->_showGameTime != before,
                            "clock preference not applied after Options");
                delete mentalBeneath;
                delete beneath;
                bags[0]->setFlags(0, kObjectOpen);
                ++options;
            }
            ConfMan.setBool("show_game_time", true);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int sample = 0; sample < 3; ++sample) {
                vm->_calendar->_days = sample == 2 ? 1 : 0;
                vm->_calendar->_hour = sample == 0 ? 5 : (sample == 1 ? 23 : 0);
                vm->_calendar->_frameInHour =
                    sample == 0 ? CalendarTime::kFramesPerHour / 2
                                : (sample == 1 ? CalendarTime::kFramesPerHour - 1 : 0);
                drawMainDisplay();
                const auto clock = region(28, 28, 112, 12);
                requireWide(std::count(clock.begin(), clock.end(), byte(34)) > 0,
                            "game clock not visible");
                updateWindowSection(Rect16(28, 28, 112, 12));
                requireWide(clock == region(28, 28, 112, 12), "partial redraw damaged game clock");
                reDrawScreen();
                requireWide(clock == region(28, 28, 112, 12),
                            "normal clock differs from full redraw");
                auto cover =
                    new ContainerNode(*vm->_cnm, bags[2]->thisID(), ContainerNode::kPhysicalType);
                bags[2]->setFlags(kObjectOpen, kObjectOpen);
                cover->show();
                cover->getWindow()->setExtent(Rect16(20, 20, 268, 320));
                drawMainDisplay();
                delete cover;
                bags[2]->setFlags(0, kObjectOpen);
                requireWide(clock == region(28, 28, 112, 12),
                            "closing window did not restore clock background");
                // Capture the next normal world frame after the immediate clock-region check.
                drawMainDisplay();
                vm->_calendar->_calendarPaused = true;
                const auto frame = vm->_calendar->frameInDay();
                updateCalendar();
                requireWide(vm->_calendar->frameInDay() == frame,
                            "clock advanced a paused calendar");
                if (sample == 1) {
                    vm->_calendar->_calendarPaused = false;
                    updateCalendar();
                    requireWide(vm->_calendar->_days == 1 && vm->_calendar->_hour == 0 &&
                                    vm->_calendar->_frameInHour == 0,
                                "clock did not follow calendar midnight rollover");
                }
                g_platform->capture(
                    output / String::format("clock-%d-%d-%d.ppm", wide, sidebar, sample).c_str());
                ++clocks;
            }
            auto saved =
                new ContainerNode(*vm->_cnm, bags[0]->thisID(), ContainerNode::kPhysicalType);
            bags[0]->setFlags(kObjectOpen, kObjectOpen);
            saved->show();
            saved->getWindow()->setExtent(Rect16(30, 65, 268, 320));
            wheel(80, 176, -2);
            requireWide(saved->getView()->_scrollPosition == 2, "save fixture did not scroll");
            requireWide(vm->saveGameState(14, "Inventory UI checkpoint", false) == kNoError,
                        "cannot save clock fixture");
            vm->_calendar->_hour = 12;
            requireWide(vm->loadGameState(14) == kNoError && vm->_calendar->_hour == 0,
                        "loading did not restore game clock");
            // Loading rebuilds the object pool; retain IDs across that ownership boundary.
            for (size_t i = 0; i < bags.size(); ++i)
                bags[i] = GameObject::objectAddress(bagIDs[i]);
            item = GameObject::objectAddress(itemID);
            updateContainerWindows();
            saved = vm->_cnm->find(bags[0]->thisID(), ContainerNode::kPhysicalType);
            requireWide(
                saved && saved->getWindow() && saved->getView()->_scrollPosition == 0,
                String::format("loaded physical window %d/%d: node=%p window=%p row=%d owner=%d "
                               "actor=%u parent=%u",
                               wide, sidebar, saved, saved ? saved->getWindow() : nullptr,
                               saved && saved->getView() ? saved->getView()->_scrollPosition : -1,
                               saved ? saved->getOwnerIndex() : -1, getCenterActorID(),
                               bags[0]->IDParent())
                    .c_str());
            delete saved;
            bags[0]->setFlags(0, kObjectOpen);
            *vm->_calendar = originalCalendar;
        }
    }
    bags[0]->proto()->appearanceType = appearance;
    ConfMan.setBool("show_game_time", originalClock);
    requestDisplayModeChange();
    applyPendingDisplayMode();
    std::ofstream(output / "inventory-ui.json")
        << "{\"status\":\"passed\",\"layouts\":4,\"decorations\":4,\"wheel_checks\":" << wheelChecks
        << ",\"placeholder_checks\":" << placeholders << ",\"redraw_checks\":" << redraws
        << ",\"options_checks\":" << options << ",\"clock_checks\":" << clocks
        << ",\"save_load_checks\":4,\"fixture_restored\":true}\n";
}

static void verifyPhysicalOrdering(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    requireWide(vm->saveGameState(19, "Physical order checkpoint", false) == kNoError,
                "cannot checkpoint physical ordering");
    const auto seed = vm->_rnd->getSeed();
    int bagProto = -1;
    for (size_t i = 0; i < vm->_objectProtos.size(); ++i)
        if (dynamic_cast<PhysicalContainerProto*>(vm->_objectProtos[i])) {
            bagProto = int(i);
            break;
        }
    requireWide(bagProto >= 0, "physical ordering needs a bag prototype");
    const auto appearance = vm->_objectProtos[bagProto]->appearanceType;
    uint32 timestamp = g_platform->getMillis();
    int cases = 0, swaps = 0, priorities = 0, guards = 0, redraws = 0, loads = 0;
    auto drain = [&] {
        if (GameMode::_newmodeFlag)
            GameMode::update();
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP || event.type == EVENT_MOUSEWHEEL) {
                if (event.type == EVENT_LBUTTONDOWN)
                    timestamp += 400;
                vm->_toolBase->handleMouse(event, timestamp);
            }
        }
    };
    for (int wide = 0; wide < 2; ++wide)
        for (int sidebar = 0; sidebar < 2; ++sidebar)
            for (int brother = 0; brother < 3; ++brother) {
                requireWide(vm->loadGameState(19) == kNoError, "cannot reset physical ordering");
                ConfMan.setBool("widescreen", bool(wide));
                ConfMan.setBool("alternate_sidebar", bool(sidebar));
                requestDisplayModeChange();
                applyPendingDisplayMode();
                setCenterBrother(brother);
                updateContainerWindows();
                auto actor = getCenterActor();
                auto cancelTask = [&] {
                    actor->setInterruptablity(true);
                    actor->stopMoving();
                    vm->_mTaskList->collectRetired();
                };
                cancelTask();
                auto create = [&](int prototype, ObjectID parent, int row, int col, int amount) {
                    auto object = GameObject::newObject();
                    requireWide(object != nullptr, "no spare object for physical ordering");
                    object->setProtoNum(prototype);
                    object->setExtra(amount);
                    object->move(Location(row, col, 1, parent));
                    return object;
                };
                auto bag = create(bagProto, actor->thisID(), 0, 0, 0);
                auto outer = create(bagProto, actor->thisID(), 0, 1, 0);
                auto nested = create(bagProto, bag->thisID(), 0, 3, 0);
                auto first = create(352, bag->thisID(), 0, 0, 1);
                auto second = create(354, bag->thisID(), 0, 1, 30);
                auto hidden = create(352, outer->thisID(), 0, 0, 1);
                auto coins = create(354, outer->thisID(), 0, 1, 20);
                requireWide(!first->isMergeable() && second->isMergeable(),
                            "physical ordering fixture prototype mismatch");
                bag->proto()->appearanceType = cases % 4;
                auto node =
                    new ContainerNode(*vm->_cnm, bag->thisID(), ContainerNode::kPhysicalType);
                bag->setFlags(kObjectOpen, kObjectOpen);
                node->show();
                auto window = node->getWindow();
                // Exercise a moved window and the original absolute-slot mapping.
                const Rect16 box(30 + brother * 3, 60, window->getExtent().width,
                                 window->getExtent().height);
                window->setExtent(box);
                auto& view = window->getView();
                const auto grid = view.getExtent();
                const int gx = box.x + grid.x, gy = box.y + grid.y;
                const int x = gx + view._iconOrigin.x + 16;
                const int y = gy + view._iconOrigin.y + 16;
                const int dx = 32 + view._iconSpacing.x;
                auto clickAt = [&](int px, int py) {
                    for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN,
                                      SDL_EVENT_MOUSE_BUTTON_UP})
                        mouse(px, py, type);
                    drain();
                };
                auto pick = [&](int col) {
                    clickAt(x + col * dx, y);
                    requireWide(vm->_mouseInfo->getObject() != nullptr,
                                "physical click did not pick up an item");
                };
                auto at = [&](GameObject* object, int row, int col, int z = 1) {
                    requireWide(object->IDParent() == bag->thisID() &&
                                    object->getLocation() == TilePoint(row, col, z),
                                "physical ordering changed identity, owner or stack coordinates");
                };
                auto parity = [&] {
                    const auto before = region(gx, gy, grid.width, grid.height);
                    reDrawScreen();
                    requireWide(before == region(gx, gy, grid.width, grid.height),
                                "physical ordering partial redraw differs from full redraw");
                    ++redraws;
                };
                auto reset = [&] {
                    vm->_mouseInfo->replaceObject();
                    cancelTask();
                    first->move(Location(0, 0, 1, bag->thisID()));
                    second->move(Location(0, 1, 1, bag->thisID()));
                    hidden->move(Location(0, 0, 1, outer->thisID()));
                    coins->move(Location(0, 1, 1, outer->thisID()));
                    view._scrollPosition = 0;
                    view._wheelRemainder = 0;
                    reDrawScreen();
                };
                reset();
                const auto fixtureSeed = vm->_rnd->getSeed();
                const auto mass = bag->totalContainedMass(), bulk = bag->totalContainedBulk();
                pick(0);
                clickAt(x + dx, y);
                at(first, 0, 1);
                at(second, 0, 0);
                requireWide(!actor->_moveTask && !vm->_mouseInfo->getObject() &&
                                second->getExtra() == 30 && vm->_rnd->getSeed() == fixtureSeed &&
                                mass == bag->totalContainedMass() &&
                                bulk == bag->totalContainedBulk(),
                            "pure physical swap changed actions, quantity, weight, volume or RNG");
                parity();
                ++swaps;
                pick(1);
                clickAt(x, y);
                at(first, 0, 0);
                at(second, 0, 1);
                parity();
                ++swaps;
                pick(0);
                clickAt(x, y);
                at(first, 0, 0);
                requireWide(!actor->_moveTask, "same-slot drop queued an action");
                ++guards;
                // Every hidden member of an incompatible destination stack travels with its base.
                reset();
                first->setLocation(TilePoint(0, 1, 2));
                hidden->move(Location(0, 1, 0, bag->thisID()));
                second->setLocation(TilePoint(0, 0, 1));
                reDrawScreen();
                pick(0);
                clickAt(x + dx, y);
                at(second, 0, 1);
                at(first, 0, 0, 2);
                at(hidden, 0, 0, 0);
                parity();
                ++swaps;
                // A source stack or partially selected merged pile cannot vacate its entire slot.
                pick(0);
                clickAt(x + dx, y);
                at(first, 0, 0, 2);
                at(hidden, 0, 0, 0);
                at(second, 0, 1);
                requireWide(actor->_moveTask, "source-stack drop bypassed original handling");
                ++guards;
                reset();
                pick(1);
                vm->_mouseInfo->setMoveCount(5);
                clickAt(x, y);
                at(first, 0, 0);
                at(second, 0, 1);
                requireWide(actor->_moveTask && second->getExtra() == 30,
                            "partial pile was exchanged or split by reordering");
                ++guards;
                // Original merging, stacking, empty-slot drops and nested insertion still queue
                // actions. Ground fixtures avoid the carrier's capacity limit from the three large
                // chests.
                bag->move(Location(actor->getLocation(), actor->IDParent()));
                outer->move(Location(actor->getLocation(), actor->IDParent()));
                reset();
                coins->move(Location(0, 2, 1, bag->thisID()));
                reDrawScreen();
                pick(2);
                vm->_mouseInfo->setMoveCount(5);
                clickAt(x + dx, y);
                requireWide(actor->_moveTask && coins->getExtra() == 20 && second->getExtra() == 30,
                            "merge drop was replaced by a swap");
                cancelTask();
                const bool merged = coins->dropOn(actor->thisID(), second->thisID(), 5);
                // Original partial merging can replace the source with an extracted remainder.
                coins = view.getObject(TilePoint(0, 2, 1));
                requireWide(merged && coins && coins->getExtra() == 15 && second->getExtra() == 35,
                            String::format("original coin merge: result=%d source=%p remaining=%d "
                                           "target=%d capacity=%d bulk=%d mass=%d",
                                           merged, static_cast<void*>(coins),
                                           coins ? coins->getExtra() : -1, second->getExtra(),
                                           bag->bulkCapacity(), bag->totalContainedBulk(),
                                           bag->totalContainedMass())
                                .c_str());
                second->setExtra(30);
                coins->setExtra(20);
                ++priorities;
                reset();
                hidden->move(Location(0, 2, 1, bag->thisID()));
                reDrawScreen();
                pick(2);
                clickAt(x, y);
                requireWide(actor->_moveTask, "stack drop was replaced by a swap");
                cancelTask();
                requireWide(hidden->dropOn(actor->thisID(), first->thisID()),
                            "original stack action failed");
                requireWide(first->getLocation().u == hidden->getLocation().u &&
                                first->getLocation().v == hidden->getLocation().v,
                            "original stacking did not combine slots");
                ++priorities;
                reset();
                pick(0);
                clickAt(x + 2 * dx, y);
                requireWide(actor->_moveTask, "empty-slot drop bypassed original handling");
                ++priorities;
                reset();
                pick(0);
                clickAt(x + 3 * dx, y);
                requireWide(actor->_moveTask && nested->getLocation() == TilePoint(0, 3, 1),
                            "nested-container drop was replaced by a swap");
                cancelTask();
                requireWide(first->dropOn(actor->thisID(), nested->thisID()) &&
                                first->IDParent() == nested->thisID(),
                            "original nested insertion failed");
                ++priorities;
                reset();
                nested->setFlags(kObjectLocked, kObjectLocked);
                pick(0);
                clickAt(x + 3 * dx, y);
                requireWide(actor->_moveTask && nested->getLocation() == TilePoint(0, 3, 1),
                            "locked nested container triggered a swap");
                cancelTask();
                first->dropOn(actor->thisID(), nested->thisID());
                requireWide(nested->getLocation() == TilePoint(0, 3, 1) &&
                                first->IDParent() != nested->thisID(),
                            "failed insertion exchanged or entered a locked bag");
                nested->setFlags(0, kObjectLocked);
                ++priorities;
                reset();
                pick(0);
                vm->_mouseInfo->setIntent(GrabInfo::kIntUse);
                clickAt(x + dx, y);
                requireWide(actor->_moveTask && first->getLocation() == TilePoint(0, 0, 1) &&
                                second->getLocation() == TilePoint(0, 1, 1),
                            "use cursor invoked a physical swap");
                ++priorities;
                // Only the front window receives a click; a foreign source never swaps.
                reset();
                auto cover =
                    new ContainerNode(*vm->_cnm, outer->thisID(), ContainerNode::kPhysicalType);
                outer->setFlags(kObjectOpen, kObjectOpen);
                cover->show();
                cover->getWindow()->setExtent(box);
                clickAt(x + dx, y);
                requireWide(vm->_mouseInfo->getObject() == coins, "covered window received pickup");
                delete cover;
                outer->setFlags(0, kObjectOpen);
                clickAt(x, y);
                at(first, 0, 0);
                requireWide(coins->IDParent() == outer->thisID() && actor->_moveTask,
                            "cross-container drop exchanged items");
                ++guards;
                reset();
                {
                    ModalWindow modal(Rect16(0, 0, 100, 100), 0, nullptr);
                    modal.open();
                    clickAt(x, y);
                    requireWide(!vm->_mouseInfo->getObject(), "modal allowed container pickup");
                    modal.close();
                }
                ++guards;
                reset();
                second->setLocation(TilePoint(view._visibleRows - 1, view._visibleCols - 1, 1));
                reDrawScreen();
                clickAt(gx, gy);
                requireWide(vm->_mouseInfo->getObject() == first, "first grid pixel missed pickup");
                clickAt(std::min(gx + grid.width, int(box.x + box.width)) - 1,
                        gy + grid.height - 1);
                at(first, view._visibleRows - 1, view._visibleCols - 1);
                at(second, 0, 0);
                parity();
                ++swaps;
                // Wheel scrolling with a held object preserves its absolute source slot.
                reset();
                second->setLocation(TilePoint(view._totalRows - 1, 1, 1));
                reDrawScreen();
                pick(0);
                Event wheel;
                wheel.type = EVENT_MOUSEWHEEL;
                wheel.mouse = Point{static_cast<int16>(x), static_cast<int16>(y)};
                wheel.wheelY = -100;
                vm->_toolBase->handleMouse(wheel, timestamp);
                requireWide(view._scrollPosition == view._totalRows - view._visibleRows,
                            "held physical object blocked wheel scrolling");
                const int bottom = y + (view._visibleRows - 1) * (32 + view._iconSpacing.y);
                clickAt(x + dx, bottom);
                at(first, view._totalRows - 1, 1);
                at(second, 0, 0);
                parity();
                ++swaps;
                const auto bagID = bag->thisID(), firstID = first->thisID(),
                           secondID = second->thisID();
                const int lastRow = view._totalRows - 1;
                g_platform->capture(
                    output /
                    String::format("physical-order-%d-%d-%d.ppm", wide, sidebar, brother).c_str());
                requireWide(vm->saveGameState(20, "Physical order fixture", false) == kNoError &&
                                vm->loadGameState(20) == kNoError,
                            "cannot reload physical ordering");
                requireWide(GameObject::objectAddress(firstID)->IDParent() == bagID &&
                                GameObject::objectAddress(firstID)->getLocation().u == lastRow &&
                                GameObject::objectAddress(secondID)->IDParent() == bagID &&
                                GameObject::objectAddress(secondID)->getLocation() ==
                                    TilePoint(0, 0, 1),
                            "save/load changed physical ordering");
                ++loads;
                updateContainerWindows();
                key(SDLK_ESCAPE);
                drain();
                updateContainerWindows();
                requireWide(!vm->_cnm->find(bagID) && !vm->_mouseInfo->getObject() &&
                                vm->_cnm->_mouseText[0] == 0,
                            "Escape retained physical ordering window or tooltip");
                ++cases;
            }
    vm->_objectProtos[bagProto]->appearanceType = appearance;
    requireWide(vm->loadGameState(19) == kNoError, "cannot restore physical order fixture");
    vm->_rnd->setSeed(seed);
    std::ofstream(output / "physical-order.json")
        << "{\"status\":\"passed\",\"cases\":" << cases << ",\"swaps\":" << swaps
        << ",\"priority_checks\":" << priorities << ",\"guard_checks\":" << guards
        << ",\"redraw_checks\":" << redraws << ",\"save_load_checks\":" << loads
        << ",\"fixture_restored\":true}\n";
}

// Temporarily exercise real interpreter return values without editing any input archive.
class ReceiveScriptFixture {
    Saga2::GameObject* object;
    uint16 script;
    uint16* entry;
    std::array<uint16, 2> method;
    uint8* code;
    std::vector<uint8> bytes;

  public:
    ReceiveScriptFixture(Saga2::Actor* actor, Saga2::GameObject* target, int which, int result,
                         bool changeItem = false)
        : object(target), script(target->_data.script) {
        using namespace Saga2;
        target->_data.script = actor->scriptClass();
        CallTable* table = nullptr;
        auto vtable = builtinVTableAddress(kBuiltinTypeObject,
                                           reinterpret_cast<uint8*>(&target->_data), &table);
        requireWide(vtable != nullptr, "receive fixture needs a script class");
        entry = vtable + 2 * which;
        method = {entry[0], entry[1]};
        uint16* body = nullptr;
        for (int index = Method_Actor_onTalkTo; index <= Method_Actor_onReceive; ++index)
            if (vtable[index * 2] != 0xffff && vtable[index * 2 + 1] > 32) {
                body = vtable + index * 2;
                break;
            }
        requireWide(body != nullptr, "receive fixture needs executable script code");
        code = segmentAddress(body[0], body[1]);
        std::vector<uint8> program{uint8(kOpEnter), 0, 0};
        auto word = [&](int value) {
            program.push_back(uint8(value));
            program.push_back(uint8(value >> 8));
        };
        if (changeItem) {
            program.push_back(kOpConstint);
            word(42);
            program.push_back(kOpPutbyteV);
            program.push_back(skAddrDeref);
            program.push_back(skAddrThread);
            word(offsetof(scriptCallFrame, indirectObject));
            word(kBuiltinTypeObject);
            word(offsetof(ObjectData, hitPoints));
        }
        program.push_back(kOpConstint);
        word(result);
        program.push_back(kOpReturn);
        bytes.assign(code, code + program.size());
        entry[0] = body[0];
        entry[1] = body[1];
        std::copy(program.begin(), program.end(), code);
    }
    ~ReceiveScriptFixture() {
        std::copy(bytes.begin(), bytes.end(), code);
        entry[0] = method[0];
        entry[1] = method[1];
        object->_data.script = script;
    }
};

static void verifyCarriedStorage(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    requireWide(vm->saveGameState(21, "Carried storage checkpoint", false) == kNoError,
                "cannot checkpoint carried storage");
    const auto seed = vm->_rnd->getSeed();
    auto original = std::find_if(vm->_objectProtos.begin(), vm->_objectProtos.end(),
                                 [](auto p) { return dynamic_cast<PhysicalContainerProto*>(p); });
    requireWide(original != vm->_objectProtos.end(), "carried storage needs a container");
    const auto protoCount = vm->_objectProtos.size();
    auto bagProto = new PhysicalContainerProto(**original);
    bagProto->mass = 0;
    bagProto->bulk = 1;
    auto fillerProto = new InventoryProto(*vm->_objectProtos[352]);
    fillerProto->mass = fillerProto->bulk = 0;
    const int bagIndex = int(protoCount), fillerIndex = bagIndex + 1;
    vm->_objectProtos.push_back(bagProto);
    vm->_objectProtos.push_back(fillerProto);
    uint32 timestamp = g_platform->getMillis();
    int cases = 0, placements = 0, guards = 0, redraws = 0, scripts = 0, ui = 0, loads = 0;
    auto drain = [&] {
        if (GameMode::_newmodeFlag)
            GameMode::update();
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP) {
                if (event.type == EVENT_LBUTTONDOWN)
                    timestamp += 400;
                vm->_toolBase->handleMouse(event, timestamp);
            }
        }
    };
    for (int wide = 0; wide < 2; ++wide)
        for (int sidebar = 0; sidebar < 2; ++sidebar)
            for (int brother = 0; brother < 3; ++brother) {
                requireWide(vm->loadGameState(21) == kNoError, "cannot reset carried storage");
                ConfMan.setBool("widescreen", bool(wide));
                ConfMan.setBool("alternate_sidebar", bool(sidebar));
                requestDisplayModeChange();
                applyPendingDisplayMode();
                setCenterBrother(brother);
                updateContainerWindows();
                auto actor = getCenterActor();
                actor->setInterruptablity(true);
                actor->stopMoving();
                vm->_mTaskList->collectRetired();
                std::vector<GameObject*> existing;
                ContainerIterator carried(actor);
                GameObject* child;
                while (carried.next(&child) != Nothing)
                    if (child->containmentSet() & ProtoObj::kIsTangible)
                        existing.push_back(child);
                for (auto object : existing)
                    object->move(Location(0, 0, 1, ImportantLimbo));
                auto create = [&](int proto, ObjectID parent, int row, int col, int amount = 1) {
                    auto object = GameObject::newObject();
                    requireWide(object != nullptr, "no spare object for carried storage");
                    object->setProtoNum(proto);
                    object->setExtra(amount);
                    object->move(Location(row, col, 1, parent));
                    return object;
                };
                // Creation order deliberately differs from visible slot order.
                auto second = create(bagIndex, actor->thisID(), 0, 1);
                auto first = create(bagIndex, actor->thisID(), 0, 0);
                std::vector<GameObject*> fillers;
                for (int slot = 2; slot < 9; ++slot)
                    fillers.push_back(create(fillerIndex, actor->thisID(), slot / 3, slot % 3));
                auto nested = create(bagIndex, first->thisID(), 0, 0);
                auto deepest = create(bagIndex, nested->thisID(), 0, 0);
                auto source = create(352, actor->IDParent(), 0, 0);
                const Location home(actor->getLocation(), actor->IDParent());
                source->move(home);
                auto ground = [&] { source->move(home); };
                auto receive = [&](GameObject* item, int count = 1) {
                    const auto before = vm->_rnd->getSeed();
                    requireWide(item->dropOn(actor->thisID(), actor->thisID(), count),
                                "carried storage rejected drop dispatch");
                    requireWide(vm->_rnd->getSeed() == before,
                                "successful carried storage consumed RNG");
                    ++placements;
                };
                auto at = [&](GameObject* object, GameObject* parent) {
                    requireWide(
                        object->IDParent() == parent->thisID(),
                        String::format(
                            "carried storage wrong destination: item=%u parent=%u expected=%u",
                            object->thisID(), object->IDParent(), parent->thisID())
                            .c_str());
                };
                auto fill = [&](GameObject* container) {
                    for (int row = 0; row < container->proto()->getMaxRows(); ++row)
                        for (int col = 0; col < container->proto()->getMaxCols(); ++col)
                            create(fillerIndex, container->thisID(), row, col);
                };
                receive(source);
                at(source, first);
                requireWide(!first->isOpen(), "automatic storage opened a closed bag");
                ++guards;
                auto giver = getPlayerActorAddress((brother + 1) % 3)->getActor();
                const auto giverSlot = giver->getFirstEmptySlot(source);
                requireWide(giverSlot != Nowhere, "carried storage giver needs a source slot");
                source->move(Location(giverSlot, giver->thisID()));
                const auto transferSeed = vm->_rnd->getSeed();
                requireWide(source->dropOn(giver->thisID(), actor->thisID()) &&
                                vm->_rnd->getSeed() == transferSeed,
                            "automatic brother-to-brother transfer failed or consumed RNG");
                at(source, first);
                ++placements;
                ground();
                // A free direct bag precedes a free nested bag, even under the first bag.
                fill(first);
                receive(source);
                at(source, second);
                ground();
                fill(second);
                receive(source);
                at(source, nested);
                ground();
                fill(nested);
                receive(source);
                at(source, deepest);
                ground();
                first->setFlags(kObjectLocked, kObjectLocked);
                // Both locked ancestors and their unlocked descendants are excluded.
                second->setFlags(kObjectLocked, kObjectLocked);
                source->dropOn(actor->thisID(), actor->thisID());
                requireWide(isWorld(source->IDParent()), "locked tree received an item");
                ++guards;
                first->setFlags(0, kObjectLocked);
                second->setFlags(0, kObjectLocked);
                fill(deepest);
                ground();
                source->dropOn(actor->thisID(), actor->thisID());
                requireWide(isWorld(source->IDParent()),
                            "full carried tree did not retain ground fallback");
                ++guards;
                std::vector<GameObject*> lastRowItems;
                ContainerIterator lastRow(deepest);
                while (lastRow.next(&child) != Nothing)
                    lastRowItems.push_back(child);
                for (auto object : lastRowItems)
                    object->move(Location(0, 0, 1, ImportantLimbo));
                // Freeing the first main slot restores original ready-inventory precedence.
                fillers.front()->move(Location(0, 0, 1, ImportantLimbo));
                ground();
                receive(source);
                at(source, actor);
                ground();
                const auto originalMass = vm->_objectProtos[352]->mass;
                vm->_objectProtos[352]->mass = 255;
                source->dropOn(actor->thisID(), actor->thisID());
                requireWide(isWorld(source->IDParent()),
                            "rejected primary insertion entered a bag");
                vm->_objectProtos[352]->mass = originalMass;
                ++guards;
                fillers.front()->move(Location(0, 2, 1, actor->thisID()));
                ground();
                // Preserve original merge/stack actions, including partial quantities.
                auto coins = create(354, deepest->thisID(), 0, 0, 20);
                auto incoming = create(354, actor->IDParent(), 0, 0, 30);
                incoming->move(home);
                receive(incoming, 5);
                requireWide(coins->getExtra() == 25, "automatic partial merge lost quantity");
                int remaining = 0;
                GameObject* remainder = nullptr;
                for (int id = 4; id < objectCount; ++id) {
                    auto object = GameObject::objectAddress(id);
                    if (object->proto() == coins->proto() && object->IDParent() == home._context &&
                        object->getLocation() == TilePoint(home)) {
                        remaining += object->getExtra();
                        remainder = object;
                    }
                }
                requireWide(remaining == 25, "automatic partial merge lost its source remainder");
                ++guards;
                fillers[1]->move(Location(0, 0, 1, ImportantLimbo));
                coins->move(Location(1, 0, 1, actor->thisID()));
                receive(remainder, 5);
                requireWide(coins->getExtra() == 30 && coins->IDParent() == actor->thisID(),
                            "full main inventory did not prioritize its existing merge");
                coins->move(Location(0, 0, 1, deepest->thisID()));
                fillers[1]->move(Location(1, 0, 1, actor->thisID()));
                ++guards;
                auto ring = create(352, deepest->thisID(), 0, 1);
                receive(source);
                at(source, deepest);
                requireWide(source->getLocation().z == 0 && ring->getLocation().z == 2,
                            "automatic stacking bypassed the original stack action");
                ground();
                coins->setExtra(32765);
                auto large = create(354, actor->IDParent(), 0, 0, 30);
                large->move(home);
                receive(large, 30);
                at(large, deepest);
                requireWide(large->getExtra() == 30 && coins->getExtra() == 32765 &&
                                large->getLocation() != coins->getLocation(),
                            "automatic merge overflow discarded coins");
                ++guards;
                // Reset full bags while keeping the two main-container slots fixed.
                std::vector<GameObject*> temporary;
                for (auto container : {first, second, nested, deepest}) {
                    ContainerIterator iter(container);
                    while (iter.next(&child) != Nothing)
                        temporary.push_back(child);
                }
                for (auto object : temporary)
                    object->move(Location(0, 0, 1, ImportantLimbo));
                nested->move(Location(0, 0, 1, first->thisID()));
                deepest->move(Location(0, 0, 1, nested->thisID()));
                // A container cannot be stored inside itself or any of its descendants.
                first->move(home);
                auto ninth = create(fillerIndex, actor->thisID(), 0, 0);
                receive(first);
                at(first, second);
                requireWide(first->IDParent() != nested->thisID() &&
                                first->IDParent() != deepest->thisID(),
                            "automatic storage formed a cycle");
                ++guards;
                first->move(Location(0, 0, 1, actor->thisID()));
                ninth->move(Location(0, 0, 1, ImportantLimbo));
                // Native capacity rejection must try the next bag, not recurse to the actor.
                auto bulky = create(352, first->thisID(), 0, 1);
                const auto oldBulk = vm->_objectProtos[352]->bulk;
                vm->_objectProtos[352]->bulk = 4;
                ground();
                receive(source);
                at(source, second);
                vm->_objectProtos[352]->bulk = oldBulk;
                bulky->move(Location(0, 0, 1, ImportantLimbo));
                // Script decisions run before routing; a changed source stops all further trials.
                ground();
                for (int result : {kActionResultFailure, kActionResultSuccess}) {
                    ReceiveScriptFixture patch(actor, actor, Method_Actor_onReceive, result);
                    const auto before = source->_data;
                    const bool accepted = source->dropOn(actor->thisID(), actor->thisID());
                    requireWide(accepted == bool(result) && source->IDParent() == before.parentID &&
                                    source->getLocation() == before.location,
                                "automatic storage bypassed the actor receive script");
                    ++scripts;
                }
                {
                    ReceiveScriptFixture patch(actor, first, Method_GameObject_onAcceptInsertion,
                                               kActionResultFailure);
                    receive(source);
                    at(source, second);
                    ++scripts;
                }
                ground();
                const auto oldHealth = source->_data.hitPoints;
                {
                    ReceiveScriptFixture patch(actor, first, Method_GameObject_onAcceptInsertion,
                                               kActionResultFailure, true);
                    receive(source);
                    requireWide(source->IDParent() == home._context &&
                                    source->_data.hitPoints == 42,
                                "automatic storage retried a script-modified source");
                    ++scripts;
                }
                source->_data.hitPoints = oldHealth;
                // Exercise both portrait panels through the original two-click UI dispatch.
                auto node =
                    new ContainerNode(*vm->_cnm, first->thisID(), ContainerNode::kPhysicalType);
                first->setFlags(kObjectOpen, kObjectOpen);
                node->show();
                node->getWindow()->setExtent(Rect16(30, 60, node->getWindow()->getExtent().width,
                                                    node->getWindow()->getExtent().height));
                for (int group = 0; group < 2; ++group) {
                    if (group)
                        setControlPanelsToTrioMode();
                    else
                        setControlPanelsToIndividualMode(brother);
                    ground();
                    reDrawScreen();
                    vm->_mouseInfo->copyObject(source, GrabInfo::kIntDrop);
                    auto button = group ? portBtns[brother] : indivPortBtn;
                    const auto box = button->getExtent();
                    for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN,
                                      SDL_EVENT_MOUSE_BUTTON_UP})
                        mouse(box.x + 8, box.y + 8, type);
                    drain();
                    requireWide(actor->_moveTask && !vm->_mouseInfo->getObject(),
                                "portrait did not queue original transfer");
                    actor->_currentFacing =
                        (actor->getLocation() - actor->getLocation()).quickDir();
                    for (int frame = 0; frame < 64 && source->IDParent() == home._context; ++frame)
                        MotionTask::updatePositions();
                    at(source, first);
                    actor->setInterruptablity(true);
                    actor->stopMoving();
                    vm->_mTaskList->collectRetired();
                    updateContainerWindows();
                    updateIndicators();
                    const auto window = node->getWindow()->getExtent();
                    const auto grid = node->getView()->getExtent();
                    const int gx = window.x + grid.x, gy = window.y + grid.y;
                    const int width = std::min(int(grid.width), int(window.width) - grid.x);
                    const int height = std::min(int(grid.height), int(window.height) - grid.y);
                    const auto& view = *node->getView();
                    const auto location = source->getLocation();
                    const int ix =
                        gx + view._iconOrigin.x + location.v * (32 + view._iconSpacing.x);
                    const int iy =
                        gy + view._iconOrigin.y + location.u * (32 + view._iconSpacing.y);
                    const auto icon = region(ix, iy, 32, 32);
                    // Real motion updates the world too; render its normal frame before comparing
                    // gaps.
                    updateMainDisplay();
                    const auto before = region(gx, gy, width, height);
                    reDrawScreen();
                    requireWide(icon == region(ix, iy, 32, 32) &&
                                    before == region(gx, gy, width, height),
                                "automatic storage grid redraw differs from full redraw");
                    ++ui;
                    ++redraws;
                }
                node->hide();
                ground();
                reDrawScreen();
                Point16 projected;
                TileToScreenCoords(actor->getLocation(), projected);
                Point16 hit(-1, -1);
                StaticTilePoint pickedPosition;
                for (int py = projected.y - 70; py <= projected.y && hit.x < 0; ++py)
                    for (int px = projected.x - 24; px <= projected.x + 24; ++px)
                        if (pickObject(StaticPoint32(px, py), pickedPosition) == actor->thisID()) {
                            hit = Point16(px, py);
                            break;
                        }
                requireWide(hit.x >= 0, "cannot locate receiving brother in map sprites");
                vm->_mouseInfo->copyObject(source, GrabInfo::kIntDrop);
                // The normal map loop refreshes this cache between input events.
                pickedObject = actor->thisID();
                vm->_mouseInfo->setDoable(true);
                for (auto type : {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN,
                                  SDL_EVENT_MOUSE_BUTTON_UP})
                    mouse(hit.x + displayLayout().mapX + fineScroll.x,
                          hit.y + displayLayout().mapY + fineScroll.y, type);
                drain();
                requireWide(actor->_moveTask && !vm->_mouseInfo->getObject(),
                            "map did not queue original transfer");
                actor->_currentFacing = (actor->getLocation() - actor->getLocation()).quickDir();
                for (int frame = 0; frame < 64 && source->IDParent() == home._context; ++frame)
                    MotionTask::updatePositions();
                at(source, first);
                actor->setInterruptablity(true);
                actor->stopMoving();
                vm->_mTaskList->collectRetired();
                ++ui;
                node->show();
                updateContainerWindows();
                reDrawScreen();
                g_platform->capture(
                    output /
                    String::format("carried-storage-%d-%d-%d.ppm", wide, sidebar, brother).c_str());
                const auto sourceID = source->thisID(), firstID = first->thisID();
                requireWide(vm->saveGameState(22, "Carried storage fixture", false) == kNoError &&
                                vm->loadGameState(22) == kNoError,
                            "cannot reload carried storage");
                requireWide(GameObject::objectAddress(sourceID)->IDParent() == firstID,
                            "save/load changed automatic storage");
                ++loads;
                ++cases;
            }
    requireWide(vm->loadGameState(21) == kNoError, "cannot restore carried storage");
    for (size_t index = protoCount; index < vm->_objectProtos.size(); ++index)
        delete vm->_objectProtos[index];
    vm->_objectProtos.resize(protoCount);
    vm->_rnd->setSeed(seed);
    std::ofstream(output / "carried-storage.json")
        << "{\"status\":\"passed\",\"cases\":" << cases << ",\"placements\":" << placements
        << ",\"guard_checks\":" << guards << ",\"script_checks\":" << scripts
        << ",\"ui_transfer_checks\":" << ui << ",\"redraw_checks\":" << redraws
        << ",\"save_load_checks\":" << loads << ",\"fixture_restored\":true}\n";
}

void verifyContainerOwners(const Path& output) {
    using namespace Saga2;
    auto vm = g_vm;
    const bool originalWide = ConfMan.getBool("widescreen");
    const bool originalSidebar = ConfMan.getBool("alternate_sidebar");
    const auto actor = getCenterActorID();
    const auto position = centerActorCoords();
    const auto seed = vm->_rnd->getSeed();
    requireWide(vm->saveGameState(9, "Container owner checkpoint", false) == kNoError,
                "cannot save container owner checkpoint");
    std::array<GameObject*, 2> bags{};
    int found = 0;
    for (int id = 0; id < objectCount && found < 2; ++id) {
        auto object = GameObject::objectAddress(id);
        if (dynamic_cast<PhysicalContainerProto*>(object->proto()) && !object->isOpen() &&
            !vm->_cnm->find(id))
            bags[found++] = object;
    }
    requireWide(found == 2, "owner fixture needs two original physical containers");
    const ObjectID bagID = bags[0]->thisID(), outerID = bags[1]->thisID();
    auto proto = bags[0]->proto();
    const auto appearance = proto->appearanceType;
    auto drain = [&] {
        Event event;
        while (g_platform->pollEvent(event)) {
            if (event.type == EVENT_KEYDOWN)
                vm->_toolBase->handleKeyStroke(event);
            else if (event.type == EVENT_MOUSEMOVE || event.type == EVENT_LBUTTONDOWN ||
                     event.type == EVENT_LBUTTONUP)
                vm->_toolBase->handleMouse(event, g_platform->getMillis());
        }
    };
    int redraws = 0, tooltips = 0, transfers = 0, edges = 0, neutral = 0, closes = 0;
    auto verifyText = [&](const char* expected) {
        Point16 actualOffset, expectedOffset;
        auto image = vm->_pointer->getImage(actualOffset);
        const auto size = image->_size;
        const std::vector<byte> pixels(image->_data, image->_data + size.x * size.y);
        vm->_mouseInfo->setText(expected);
        image = vm->_pointer->getImage(expectedOffset);
        requireWide(size == image->_size && actualOffset == expectedOffset &&
                        pixels == std::vector<byte>(image->_data, image->_data + size.x * size.y),
                    String::format("owner tooltip check %d expected %s (size %d,%d -> %d,%d)",
                                   tooltips, expected ? expected : "empty", size.x, size.y,
                                   image->_size.x, image->_size.y)
                        .c_str());
        ++tooltips;
    };
    auto moveInto = [&](GameObject* item, ObjectID parent) {
        item->move(Location(TilePoint(0, 0, 0), parent));
        ++transfers;
    };
    static const char* labels[] = {"Carried by Julian", "Carried by Phillip", "Carried by Kevin"};
    ObjectID npc = Nothing;
    for (int i = 3; i < kActorCount; ++i) {
        auto candidate = GameObject::objectAddress(ActorBaseID + i);
        if (candidate->proto()) {
            npc = candidate->thisID();
            break;
        }
    }
    requireWide(npc != Nothing, "owner fixture needs a non-player actor");
    for (int wide = 0; wide < 2; ++wide) {
        for (int sidebar = 0; sidebar < 2; ++sidebar) {
            ConfMan.setBool("widescreen", bool(wide));
            ConfMan.setBool("alternate_sidebar", bool(sidebar));
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int decoration = 0; decoration < 4; ++decoration) {
                proto->appearanceType = decoration;
                moveInto(bags[0], actor);
                moveInto(bags[1], actor);
                auto node = new ContainerNode(*vm->_cnm, bagID, ContainerNode::kPhysicalType);
                bags[0]->setFlags(kObjectOpen, kObjectOpen);
                node->show();
                auto window = node->getWindow();
                const auto association = node->getOwnerIndex();
                requireWide(window && window->isOpen(), "owner fixture window did not open");
                auto box = window->getExtent();
                box.x = 30 + decoration * 8;
                box.y = 70;
                window->setExtent(box);
                reDrawScreen();
                auto hover = [&](int x, int y) {
                    mouse(box.x + x, box.y + y, SDL_EVENT_MOUSE_MOTION);
                    drain();
                };
                auto parity = [&] {
                    const auto pixels = region(box.x, box.y, box.width, box.height);
                    reDrawScreen();
                    const auto after = region(box.x, box.y, box.width, box.height);
                    if (pixels != after) {
                        for (size_t i = 0; i < pixels.size(); ++i)
                            if (pixels[i] != after[i])
                                throw std::runtime_error(
                                    String::format(
                                        "Owner redraw %d: first difference at %d,%d (%u/%u)",
                                        redraws, int(i % box.width), int(i / box.width), pixels[i],
                                        after[i])
                                        .c_str());
                    }
                    ++redraws;
                };
                auto marker = window->hitTest(Point16(131, 77));
                requireWide(marker && marker->getExtent() == Rect16(119, 66, 25, 21),
                            "owner marker has wrong bounds");
                for (int brother = 0; brother < 3; ++brother) {
                    moveInto(bags[0], ActorBaseID + brother);
                    updateContainerWindows();
                    parity();
                    for (auto point :
                         {Point16(119, 66), Point16(143, 66), Point16(119, 86), Point16(143, 86)}) {
                        requireWide(window->hitTest(point) == marker, "owner marker edge misses");
                        hover(point.x, point.y);
                        verifyText(labels[brother]);
                        ++edges;
                    }
                    hover(131, 77);
                    verifyText(labels[brother]);
                    const auto pixels = region(box.x + 119, box.y + 66, 25, 21);
                    moveInto(bags[0], outerID);
                    moveInto(bags[1], ActorBaseID + brother);
                    updateContainerWindows();
                    verifyText(labels[brother]);
                    requireWide(pixels == region(box.x + 119, box.y + 66, 25, 21),
                                "nested container has a different owner marker");
                    parity();
                    // Transfer the outer bag while the pointer stays over the inner badge.
                    moveInto(bags[1], ActorBaseID + (brother + 1) % 3);
                    updateContainerWindows();
                    verifyText(labels[(brother + 1) % 3]);
                    parity();
                    // Keep the cursor clear of the initial in the visual evidence.
                    hover(143, 77);
                    captureWithCursor(output / String::format("owner-%d-%d-%d-%d.ppm", wide,
                                                              sidebar, decoration, brother)
                                                   .c_str());
                    hover(131, 77);
                    requireWide(node->getOwnerIndex() == association && getCenterActorID() == actor,
                                "owner display changed original selection or node association");
                }
                for (auto parent : {getCenterActor()->IDParent(), npc}) {
                    moveInto(bags[0], parent);
                    if (parent != npc)
                        bags[0]->move(Location(position, parent));
                    updateContainerWindows();
                    verifyText(nullptr);
                    requireWide(window->hitTest(Point16(131, 77)) != marker,
                                "neutral container retained owner hover region");
                    parity();
                    ++neutral;
                }
                moveInto(bags[0], actor);
                updateContainerWindows();
                hover(131, 77);
                verifyText(labels[actor - ActorBaseID]);
                const auto pixels = region(box.x + 119, box.y + 66, 25, 21);
                click(window, 131, 77);
                drain();
                requireWide(pixels == region(box.x + 119, box.y + 66, 25, 21) &&
                                getCenterActorID() == actor && node->getOwnerIndex() == association,
                            "owner badge click changed state");
                for (auto point :
                     {Point16(118, 77), Point16(144, 77), Point16(131, 65), Point16(131, 87)}) {
                    requireWide(window->hitTest(point) != marker,
                                "owner marker extends outside bounds");
                    hover(point.x, point.y);
                    verifyText(nullptr);
                    hover(131, 77);
                    verifyText(labels[actor - ActorBaseID]);
                    ++edges;
                }
                if (decoration % 2) {
                    node->hide();
                    verifyText(nullptr);
                    node->show();
                    window = node->getWindow();
                    box = window->getExtent();
                    hover(131, 77);
                    verifyText(labels[actor - ActorBaseID]);
                }
                key(SDLK_ESCAPE);
                drain();
                updateContainerWindows();
                verifyText(nullptr);
                requireWide(!vm->_cnm->find(bagID), "Escape retained owner fixture window");
                ++closes;
            }
            proto->appearanceType = appearance;
            // Reopening from a save must resolve current possession, not cached ownership.
            moveInto(bags[0], actor);
            auto node = new ContainerNode(*vm->_cnm, bagID, ContainerNode::kPhysicalType);
            bags[0]->setFlags(kObjectOpen, kObjectOpen);
            node->show();
            moveInto(bags[0], ActorBaseID + (actor - ActorBaseID + 1) % 3);
            updateContainerWindows();
            requireWide(vm->saveGameState(10, "Container owner load fixture", false) == kNoError &&
                            vm->loadGameState(10) == kNoError,
                        "cannot reload owner fixture");
            updateContainerWindows();
            bags = {GameObject::objectAddress(bagID), GameObject::objectAddress(outerID)};
            proto = bags[0]->proto();
            node = vm->_cnm->find(bagID);
            requireWide(node && node->getWindow(), "saved owner window was not restored");
            auto window = node->getWindow();
            const auto box = window->getExtent();
            mouse(box.x + 131, box.y + 77, SDL_EVENT_MOUSE_MOTION);
            drain();
            verifyText(labels[(actor - ActorBaseID + 1) % 3]);
            delete node;
            bags[0]->setFlags(0, kObjectOpen);
            verifyText(nullptr);
        }
    }
    proto->appearanceType = appearance;
    verifyMentalOwners(output);
    verifyMentalScrolling(output);
    verifySpellOrdering(output);
    verifyInventoryUI(output);
    verifyPhysicalOrdering(output);
    verifyCarriedStorage(output);
    requireWide(vm->loadGameState(9) == kNoError, "cannot restore container owner fixture");
    ConfMan.setBool("widescreen", originalWide);
    ConfMan.setBool("alternate_sidebar", originalSidebar);
    requestDisplayModeChange();
    applyPendingDisplayMode();
    requireWide(getCenterActorID() == actor && centerActorCoords() == position &&
                    vm->_rnd->getSeed() == seed,
                String::format("owner fixture state: actor %u/%u position %d,%d/%d,%d RNG %u/%u",
                               getCenterActorID(), actor, centerActorCoords().u,
                               centerActorCoords().v, position.u, position.v, vm->_rnd->getSeed(),
                               seed)
                    .c_str());
    std::ofstream(output / "container-owner.json")
        << "{\"status\":\"passed\",\"layouts\":4,\"decorations\":4,\"brothers\":3,"
        << "\"redraw_checks\":" << redraws << ",\"tooltip_checks\":" << tooltips
        << ",\"edge_checks\":" << edges << ",\"transfers\":" << transfers
        << ",\"neutral_checks\":" << neutral << ",\"escape_closes\":" << closes
        << ",\"save_load_checks\":4,\"captures\":48,\"fixture_restored\":true}\n";
}

void configureWidescreenProbe(const Path& output) {
    std::filesystem::create_directories(output);
    auto state = std::make_shared<WidescreenProbe>();
    state->output = output;
    gameFrameHook = [state] { state->tick(); };
}
} // namespace Native
