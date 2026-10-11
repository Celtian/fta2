// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/localization.h"
#include "platform/probe.h"
#include "platform/resources.h"
#include "saga2/saga2.h"

#include "saga2/actor.h"
#include "saga2/audio.h"
#include "saga2/beegee.h"
#include "saga2/button.h"
#include "saga2/calendar.h"
#include "saga2/contain.h"
#include "saga2/display.h"
#include "saga2/document.h"
#include "saga2/fontlib.h"
#include "saga2/gpointer.h"
#include "saga2/grabinfo.h"
#include "saga2/grequest.h"
#include "saga2/intrface.h"
#include "saga2/modal.h"
#include "saga2/palette.h"
#include "saga2/player.h"
#include "saga2/script.h"
#include "saga2/speech.h"
#include "saga2/uidialog.h"
#include "saga2/vpal.h"
#include <cctype>
#include <fstream>
#include <set>

namespace Saga2 {
extern BackWindow* mainWindow;
void requestDisplayModeChange();
void reDrawScreen();
void buildText(uint16 textScript);
int16 FileDialog(int16 fileProcess);
extern char bookText[];
extern GfxCompImage* namePlates[kNumViews];
extern GfxOwnerSelCompButton* speechTextBtn;
extern GfxCompButton* menConBtn;
extern CDocumentAppearance bookAppearance, scrollAppearance, parchAppearance;
uint8* segmentAddress(uint16 segment, uint16 offset);
} // namespace Saga2

namespace Native {
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(String("Localization probe: ") + message);
}
void key(SDL_Keycode code) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.down = true;
    require(SDL_PushEvent(&event), "cannot inject keyboard input");
}
void clickChoice(Saga2::gPanel* choice, bool label) {
    const auto row = choice->getExtent();
    const auto windowBox = choice->getWindow()->getExtent();
    int count = 0;
    auto windows = SDL_GetWindows(&count);
    require(count == 1, "expected one SDL window");
    auto window = windows[0];
    SDL_free(windows);
    float x = 0, y = 0;
    require(SDL_RenderCoordinatesToWindow(SDL_GetRenderer(window),
                                          windowBox.x + row.x + (label ? 45 : 8),
                                          windowBox.y + row.y + 8, &x, &y),
            "cannot convert radio coordinates");
    for (auto type :
         {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(window);
            event.motion.x = x;
            event.motion.y = y;
        } else {
            event.button.windowID = SDL_GetWindowID(window);
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.x = x;
            event.button.y = y;
        }
        require(SDL_PushEvent(&event), "cannot inject radio click");
    }
}
void screenshotHotkey() {
    g_platform->updateScreen();
    key(SDLK_PRINTSCREEN);
    Event event;
    bool saved = false;
    while (g_platform->pollEvent(event)) {
        require(event.type != EVENT_KEYDOWN, "Print Screen reached game keyboard dispatch");
        saved |= event.type == EVENT_SCREENSHOT && event.screenshotSaved;
    }
    require(saved, "document/gameplay screenshot was not saved");
}
void setLanguage(Language value) {
    requestLanguage(value);
    applyPendingLanguage();
    Saga2::g_vm->syncSoundSettings();
    Saga2::reDrawScreen();
}
void portraitPlates(const Path& output) {
    using namespace Saga2;
    std::vector<byte> originals[kNumViews];
    for (auto value : {Language::English, Language::Czech, Language::Spanish, Language::French}) {
        setLanguage(value);
        for (int brother = 0; brother < kNumViews; ++brother) {
            auto* plate = namePlates[brother];
            const auto box = plate->getExtent();
            gPixelMap map;
            map._size = Point16(65, 15);
            std::vector<byte> pixels(map.bytes(), 0);
            map._data = pixels.data();
            gPort port;
            port.setMap(&map);
            plate->drawClipped(port, Point16(box.x, box.y), box);
            if (value == Language::English) {
                originals[brother] = pixels;
            } else if (brother == 2 || value != Language::Czech) {
                require(pixels == originals[brother], "Kevin's original name plate changed");
            } else {
                require(std::count(pixels.begin(), pixels.end(), byte(10)) >= 40,
                        "localized portrait name lost its bold white lettering");
                require(pixels != originals[brother], "portrait name was not translated");
                for (int y = 0; y < 15; ++y)
                    for (int x = 0; x < 65; ++x)
                        if (y < 2 || x < 10 || x >= 56)
                            require(pixels[y * 65 + x] == originals[brother][y * 65 + x],
                                    "localized portrait name damaged its frame");
            }
            std::ofstream file(
                output / String::format("portrait-plate-%d-%d.raw", brother, int(value)).c_str(),
                std::ios::binary);
            file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
        }
        g_platform->capture(output / String::format("portraits-%d.ppm", int(value)).c_str());
    }
}

unsigned manaTooltips(const Path& output) {
    using namespace Saga2;
    const char* english[] = {
        "Red Mana:", "Orange Mana:", "Yellow Mana:", "Green Mana:", "Blue Mana:", "Purple Mana:"};
    const char* czech[] = {"\310erven\341 mana:", "Oran\236ov\341 mana:", "\216lut\341 mana:",
                           "Zelen\341 mana:",     "Modr\341 mana:",       "Fialov\341 mana:"};
    const char* spanish[] = {"Man\341 rojo:",  "Man\341 naranja:", "Man\341 amarillo:",
                             "Man\341 verde:", "Man\341 azul:",    "Man\341 violeta:"};
    const char* french[] = {"Mana rouge :", "Mana orange :", "Mana jaune :",
                            "Mana vert :",  "Mana bleu :",   "Mana violet :"};
    const auto originalBrother = getCenterActorPlayerID();
    const bool individual = g_vm->_indivControlsFlag;
    unsigned cases = 0;
    for (bool wide : {false, true})
        for (bool sidebar : {false, true}) {
            ConfMan.setBool("widescreen", wide);
            ConfMan.setBool("alternate_sidebar", sidebar);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (int brother = 0; brother < kNumViews; ++brother) {
                setCenterBrother(brother);
                setControlPanelsToIndividualMode(brother);
                for (int color = 0; color < 6; ++color)
                    for (auto value : {Language::English, Language::Czech, Language::Spanish,
                                       Language::French}) {
                        g_vm->_toolBase->leavePanel();
                        setLanguage(value);
                        const auto region = ManaIndicator->getManaRegionRect(color);
                        Event hover{};
                        hover.type = EVENT_MOUSEMOVE;
                        hover.mouse.x = region.x + region.width / 2;
                        hover.mouse.y = region.y + region.height / 2;
                        g_vm->_pointer->move(Point16(hover.mouse.x, hover.mouse.y));
                        g_vm->_toolBase->handleMouse(hover, 0);
                        const auto actual = CursorMan.pixels;
                        const auto width = CursorMan.w, height = CursorMan.h;
                        auto* player = g_vm->_playerList[brother];
                        const auto expected = String::format(
                            "%s %d/%d",
                            value == Language::Czech     ? czech[color]
                            : value == Language::Spanish ? spanish[color]
                            : value == Language::French  ? french[color]
                                                         : english[color],
                            player->getEffStats()->mana(color), player->getBaseStats().mana(color));
                        g_vm->_mouseInfo->setText(expected.c_str());
                        require(actual == CursorMan.pixels && width == CursorMan.w &&
                                    height == CursorMan.h,
                                String::format("mana tooltip mismatch: brother=%d color=%d "
                                               "language=%d actual=%dx%d expected=%dx%d",
                                               brother, color, int(value), width, height,
                                               CursorMan.w, CursorMan.h)
                                    .c_str());
                        // Capture the actual cursor bitmap, which includes the outlined tooltip.
                        if (brother == 0 && color == 4) {
                            byte palette[768];
                            g_platform->getPaletteManager()->grabPalette(palette, 0, 256);
                            std::ofstream capture(output /
                                                      String::format("mana-tooltip-%d-%d-%d.ppm",
                                                                     wide, sidebar, int(value))
                                                          .c_str(),
                                                  std::ios::binary);
                            capture << "P6\n" << width << ' ' << height << "\n255\n";
                            for (byte pixel : actual)
                                capture.write(reinterpret_cast<char*>(palette + pixel * 3), 3);
                        }
                        ++cases;
                    }
            }
        }
    g_vm->_toolBase->leavePanel();
    g_vm->_mouseInfo->setText(nullptr);
    setCenterBrother(originalBrother);
    if (individual)
        setControlPanelsToIndividualMode(originalBrother);
    else
        setControlPanelsToTrioMode();
    return cases;
}
void menuCase(const Path& output, bool pregame, bool cancel, Language target, int number) {
    using namespace Saga2;
    const auto before = language();
    const std::array<int, 3> volumes = {ConfMan.getInt("sfx_volume"),
                                        ConfMan.getInt("speech_volume"),
                                        ConfMan.getInt("music_volume")};
    const std::array<bool, 3> settings = {ConfMan.getBool("widescreen"),
                                          ConfMan.getBool("alternate_sidebar"),
                                          ConfMan.getBool("show_game_time")};
    const auto otherLanguage = before != target              ? before
                               : target == Language::English ? Language::Czech
                                                             : Language::English;
    const SDL_Keycode shortcuts[] = {SDLK_E, SDLK_C, SDLK_S, SDLK_F};
    const auto targetKey = shortcuts[static_cast<size_t>(target)];
    const auto otherKey = shortcuts[static_cast<size_t>(otherLanguage)];
    bool subtitlesBeforeSameApply = false;
    int phase = 0, ticks = 0, dispatchDelay = 0;
    ModalWindow *options = nullptr, *child = nullptr;
    gPanel *selected = nullptr, *other = nullptr;
    auto checkSelection = [&](bool targetSelected) {
        unsigned count = 0;
        for (auto shortcut : shortcuts)
            count += selected->getWindow()->keyTest(std::toupper(shortcut))->isSelected();
        require(count == 1, "radio selection changed the number of selected languages");
        require(selected->isSelected() == targetSelected && other->isSelected() != targetSelected,
                String::format("radio case=%d phase=%d expected=%d selected=%d other=%d", number,
                               phase, targetSelected, selected->isSelected(), other->isSelected())
                    .c_str());
    };
    gameFrameHook = [&] {
        require(++ticks < 150, "modal navigation timed out");
        // SDL may finish the current polling batch before delivering an injected event.
        if (dispatchDelay-- > 0)
            return;
        dispatchDelay = 1;
        auto* modal = ModalWindow::_current;
        if (!modal)
            return;
        switch (phase) {
        case 0:
            options = modal;
            require(modal->keyTest('J'), "language button missing");
            g_platform->capture(output / String::format("options-%02d.ppm", number).c_str());
            key(SDLK_J);
            phase = 1;
            break;
        case 1: {
            if (modal == options)
                return;
            child = modal;
            require(child->getExtent().width == 487 && child->getExtent().height == 200,
                    "language dialog is not compact");
            unsigned selectedCount = 0;
            for (auto shortcut : shortcuts) {
                auto* choice = modal->keyTest(std::toupper(shortcut));
                require(choice, "language choice missing");
                selectedCount += choice->isSelected();
            }
            require(selectedCount == 1, "language group does not have exactly one selected radio");
            require(modal->keyTest('\r')->getExtent().y == 140 &&
                        modal->keyTest(ASCII_ESCAPE)->getExtent().y == 140,
                    "language confirmation buttons were not moved");
            selected = modal->keyTest(std::toupper(targetKey));
            other = modal->keyTest(std::toupper(otherKey));
            checkSelection(before == target);
            key(targetKey);
            phase = 2;
            break;
        }
        case 2:
            require(modal == child && language() == before, "selection applied prematurely");
            checkSelection(true);
            clickChoice(other, true);
            phase = 3;
            break;
        case 3:
            checkSelection(false);
            clickChoice(selected, false);
            phase = 4;
            break;
        case 4:
            checkSelection(true);
            clickChoice(selected, true);
            phase = 5;
            break;
        case 5:
            checkSelection(true);
            key(otherKey);
            phase = 6;
            break;
        case 6:
            checkSelection(false);
            key(targetKey);
            phase = 7;
            break;
        case 7:
            checkSelection(true);
            require(modal == child && language() == before, "radio clicks applied prematurely");
            g_platform->capture(output / String::format("language-%02d.ppm", number).c_str());
            g_platform->updateScreen();
            key(SDLK_PRINTSCREEN);
            key(cancel ? SDLK_ESCAPE : SDLK_RETURN);
            phase = 8;
            break;
        case 8:
            if (modal != options)
                return;
            require(language() == (cancel ? before : target),
                    "Apply did not change language immediately");
            require(ModalWindow::_current == options, "Apply replaced the Options window");
            require(ConfMan.getInt("sfx_volume") == volumes[0] &&
                        ConfMan.getInt("speech_volume") == volumes[1] &&
                        ConfMan.getInt("music_volume") == volumes[2] &&
                        ConfMan.getBool("widescreen") == settings[0] &&
                        ConfMan.getBool("alternate_sidebar") == settings[1] &&
                        ConfMan.getBool("show_game_time") == settings[2],
                    "Apply changed other preferences");
            require(!strcmp(static_cast<GfxCompImage*>(modal->keyTest('J'))->caption(),
                            ui("Language...")) &&
                        !strcmp(static_cast<GfxCompImage*>(modal->keyTest('C'))->caption(),
                                ui("Credits")),
                    "Open Options captions were not refreshed");
            require(speechTextBtn->isSelected() == g_vm->_speechText, "Subtitle checkbox is stale");
            if (!cancel && before != target && target != Language::English)
                require(g_vm->_speechText,
                        "Entering a translated language did not enable subtitles immediately");
            g_platform->capture(output /
                                String::format("options-applied-%02d.ppm", number).c_str());
            g_platform->updateScreen();
            key(SDLK_PRINTSCREEN);
            if (!cancel) {
                // A later user choice to hide subtitles must survive Apply of the active language.
                if (g_vm->_speechText)
                    clickChoice(speechTextBtn, false);
                subtitlesBeforeSameApply = false;
                key(SDLK_J);
                phase = 12;
                break;
            }
            key(SDLK_H);
            phase = 9;
            break;
        case 9:
            if (modal == options)
                return;
            g_platform->capture(output / String::format("hotkeys-%02d.ppm", number).c_str());
            g_platform->updateScreen();
            key(SDLK_PRINTSCREEN);
            key(SDLK_ESCAPE);
            phase = 10;
            break;
        case 10:
            if (modal != options)
                return;
            phase = 11;
            if (pregame)
                static_cast<requestInfo*>(modal->_userData)->running = false;
            else
                key(SDLK_ESCAPE);
            break;
        case 12:
            if (modal == options)
                return;
            require(language() == target && g_vm->_speechText == subtitlesBeforeSameApply,
                    "User subtitle choice was not retained");
            key(SDLK_RETURN);
            phase = 13;
            break;
        case 13:
            if (modal != options)
                return;
            require(language() == target && g_vm->_speechText == subtitlesBeforeSameApply &&
                        speechTextBtn->isSelected() == subtitlesBeforeSameApply,
                    "Applying the active language reset subtitles");
            key(SDLK_H);
            phase = 9;
            break;
        default:
            break;
        }
    };
    OptionsDialog(pregame);
    gameFrameHook = {};
    require(phase == 11 && !ModalWindow::_current, "Options did not restore modal focus");
    if (GameMode::_newmodeFlag)
        GameMode::update();
    applyPendingLanguage();
    g_vm->syncSoundSettings();
    require(language() == (cancel ? before : target),
            String::format("confirm/cancel case=%d cancel=%d before=%d target=%d actual=%d", number,
                           cancel, int(before), int(target), int(language()))
                .c_str());
    if (!cancel)
        require(!g_vm->_speechText, "Closing Options reset the later subtitle choice");
    reDrawScreen();
}

unsigned nameBounds() {
    using namespace Saga2;
    unsigned cases = 0;
    for (auto value : {Language::Czech, Language::Spanish, Language::French}) {
        setLanguage(value);
        for (uint16 index = 0; index < g_vm->_nameList.size(); ++index) {
            const char* text = localizedName(index, g_vm->_nameList[index]);
            if (!*text)
                continue;
            require(strlen(text) < 54,
                    String::format("name:%u exceeds the tooltip base-name buffer (%s)", index,
                                   languageCode(value))
                        .c_str());
            checkTextFit(text, &Helv11Font, 630, Helv11Font.height + 2, kTextStyleOutline,
                         "NAME tooltip");
            ++cases;
        }
    }
    return cases;
}

void statusMessages() {
    using namespace Saga2;
    class ProbeStatusLine : public CStatusLine {
      public:
        using CStatusLine::CStatusLine;
        String text() const { return _lineBuf; }
        gFont* font() const { return _buttonFont; }
    };
    setLanguage(Language::English);
    gPanelList controls(*mainWindow);
    textPallete pal(42, 45, 50, 43, 49, 52);
    for (auto value : {Language::Czech, Language::Spanish, Language::French}) {
        auto* line = new ProbeStatusLine(controls, Rect16(49, 445, 407, 15), "", &Script10Font, 0,
                                         pal, 5, 0, nullptr);
        auto first = LocalizedMessage::ui("%s is hurt.");
        first.formatted = true;
        first.arguments.push_back(LocalizedMessage::name(545, "Julian"));
        auto second = LocalizedMessage::ui("%s is badly wounded!");
        second.formatted = true;
        second.arguments.push_back(LocalizedMessage::name(545, "Julian"));
        line->setLocalizedLine(first, 5);
        line->setLocalizedLine(second, 5);
        const auto firstLocalized = first.render(value);
        // Destroy the caller's inputs before either message is displayed.
        first = {};
        second = {};
        line->experationCheck();
        require(line->text() == "Julian is hurt.", "initial status changed");
        setLanguage(value);
        line->refreshLanguage();
        require(line->text() == firstLocalized, "active status did not refresh");
        require(line->font() == localizedFont(&Script10Font, value), "status font did not refresh");
        const auto previousTime = gameTime;
        gameTime = previousTime + 6;
        line->experationCheck();
        gameTime = previousTime;
        auto expected = LocalizedMessage::ui("%s is badly wounded!");
        expected.formatted = true;
        expected.arguments.push_back(LocalizedMessage::name(545, "Julian"));
        require(line->text() == expected.render(value), "queued status did not refresh");
        setLanguage(Language::English);
        line->refreshLanguage();
        require(line->text() == "Julian is badly wounded!" && line->font() == &Script10Font,
                "status did not restore English");
        line->clear();
    }
}

void speechSaves(const Path& output) {
    using namespace Saga2;
    g_vm->saveGameState(8, "Localization baseline", false);
    cleanupSpeechTasks();
    setLanguage(Language::Czech);
    auto* first = speechList.newTask(getCenterActorID(), Speech::kSpNoAnimate);
    auto* second = speechList.newTask(getCenterActorID(), Speech::kSpNoAnimate);
    require(first && second, "cannot construct speech fixtures");
    char canonical[] = "Synthetic choice: @Yes@No";
    char canonical2[] = "Synthetic continued speech";
    first->append(canonical, 0, "Zkou\232ka: @Ano@Ne");
    second->append(canonical2, 0, "Pokra\350ov\341n\355 zkou\232ky");
    first->activate();
    require(g_vm->saveGameState(9, "Localized speech", false) == kNoError, "speech save failed");
    setLanguage(Language::English);
    g_vm->loadGameState(9);
    require(language() == Language::English && speechList.speechCount() == 2 &&
                speechList.currentActive()->textLanguage() == Language::Czech,
            String::format(
                "localized save language=%d count=%d speechLanguage=%d", int(language()),
                speechList.speechCount(),
                speechList.currentActive() ? int(speechList.currentActive()->textLanguage()) : -1)
                .c_str());
    updateSpeech();
    require(speechButtonCount == 2, "restored localized choices changed");
    g_platform->capture(output / "speech-restored.ppm");
    g_vm->saveGameState(10, "Localized speech restored", false);
    g_vm->loadGameState(8);
    require(language() == Language::English, "old save changed the selected language");
}

void mixedSpeechSaves(const Path& output) {
    using namespace Saga2;
    cleanupSpeechTasks();
    setLanguage(Language::Spanish);
    auto* first = speechList.newTask(getCenterActorID(), Speech::kSpNoAnimate);
    require(first, "cannot create Spanish speech fixture");
    char canonical[] = "Synthetic choice: @Yes@No";
    first->append(canonical, 0, "Prueba: @S\355@No");
    setLanguage(Language::French);
    auto* second = speechList.newTask(getCenterActorID(), Speech::kSpNoAnimate);
    require(second, "cannot create French speech fixture");
    char canonical2[] = "Synthetic continued speech";
    second->append(canonical2, 0, "R\351plique fran\347aise");
    first->activate();
    require(g_vm->saveGameState(11, "Mixed localized speech", false) == kNoError,
            "mixed speech save failed");
    setLanguage(Language::English);
    g_vm->loadGameState(11);
    auto* pending = speechList.findSpeech(getCenterActorID());
    require(language() == Language::English && speechList.speechCount() == 2 &&
                speechList.currentActive() && pending &&
                speechList.currentActive()->textLanguage() == Language::Spanish &&
                pending->textLanguage() == Language::French,
            "mixed speech languages were not restored independently");
    updateSpeech();
    require(speechButtonCount == 2, "restored Spanish choices changed");
    g_platform->capture(output / "speech-restored-es.ppm");
    require(g_vm->saveGameState(12, "Mixed speech restored", false) == kNoError,
            "mixed speech resave failed");
    g_vm->loadGameState(8);
    require(language() == Language::English, "old save changed the selected language");
}

void speechVoices(const Path& output) {
    using namespace Saga2;
    const bool previous = ConfMan.getBool("subtitles");
    for (auto value : {Language::Czech, Language::Spanish, Language::French}) {
        setLanguage(value);
        const char* display = value == Language::Czech     ? "Zkou\232ka \350esk\375ch titulk\371"
                              : value == Language::Spanish ? "Prueba de subt\355tulos"
                                                           : "Test des sous-titres fran\347ais";
        for (int variant = 0; variant < 3; ++variant) {
            ConfMan.setBool("subtitles", variant == 0);
            g_vm->syncSoundSettings();
            auto* speech = speechList.newTask(getCenterActorID(), Speech::kSpNoAnimate);
            require(speech != nullptr, "cannot create voice fixture");
            char canonical[] = "Synthetic voice fixture";
            require(speech->append(canonical, variant == 2 ? 9999 : 1, display),
                    "voice append failed");
            speech->activate();
            updateSpeech();
            require(speechList.currentActive() == speech, "voice fixture finished too early");
            const auto* pixels = speech->_speechImage._data;
            const bool glyphs = std::any_of(pixels, pixels + speech->_speechImage.bytes(),
                                            [](byte p) { return p; });
            require(glyphs == (variant != 1 || voiceArchive.empty()),
                    "Translated subtitle fallback changed");
            if (variant != 2 && !voiceArchive.empty())
                require(!g_vm->_audio->_speechQueue.empty(), "English recording was not retained");
            g_platform->capture(
                output /
                String::format("speech-voice-%s-%d.ppm", languageCode(value), variant).c_str());
            speech->abortSpeech();
            speech->dispose();
        }
    }
    ConfMan.setBool("subtitles", previous);
    g_vm->syncSoundSettings();
}

unsigned documents(const Path& output, unsigned& screenshots) {
    using namespace Saga2;
    std::set<std::pair<uint16, int>> scripts;
    scripts.emplace(resImports->reserved[0], 0);
    for (int id = 0; id < objectCount; ++id) {
        auto* object = GameObject::objectAddress(id);
        auto* prototype = object->proto();
        if (!prototype || !object->getExtra())
            continue;
        if (dynamic_cast<BookProto*>(prototype))
            scripts.emplace(object->getExtra(), prototype->appearanceType);
        else if (dynamic_cast<ScrollProto*>(prototype))
            scripts.emplace(object->getExtra(), 1);
    }
    unsigned pages = 0, redrawChecks = 0, wheelChecks = 0, multipageDocuments = 0;
    for (bool wide : {false, true})
        for (bool sidebar : {false, true}) {
            ConfMan.setBool("widescreen", wide);
            ConfMan.setBool("alternate_sidebar", sidebar);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (auto value :
                 {Language::English, Language::Czech, Language::Spanish, Language::French}) {
                setLanguage(value);
                bool captured[3]{};
                bool capturedPaging[3]{};
                for (auto [script, appearance] : scripts) {
                    require(appearance >= 0 && appearance <= 2, "unknown document appearance");
                    const auto overflowsBeforeAssembly = textOverflowCount();
                    buildText(script);
                    if (textOverflowCount() != overflowsBeforeAssembly)
                        warning("Document assembly overflow: script=%u language=%s", script,
                                languageCode(value));
                    if (script == resImports->reserved[0]) {
                        require(std::strstr(bookText,
                                            value == Language::Czech     ? "Hlavn\355 scen\341rista"
                                            : value == Language::Spanish ? "guionista"
                                            : value == Language::French  ? "Auteur principal"
                                                                         : "Lead writer"),
                                "credits role was not localized");
                        require(std::strstr(bookText, "Matthew T. Kuzma") &&
                                    std::strstr(bookText, "The Faery Tale Adventure licensed"),
                                "credits changed author or licensing text");
                    }
                    auto& app = appearance == 0   ? bookAppearance
                                : appearance == 1 ? scrollAppearance
                                                  : parchAppearance;
                    CDocument document(app, bookText, &Script10Font, 0);
                    require(document.open(), "cannot open document");
                    const auto box = document.getExtent();
                    auto pixels = [&] {
                        auto* screen = g_platform->lockScreen();
                        std::vector<byte> result;
                        for (int y = box.y; y < box.y + box.height; ++y) {
                            auto* row =
                                static_cast<byte*>(screen->pixels) + y * screen->pitch + box.x;
                            result.insert(result.end(), row, row + box.width);
                        }
                        g_platform->unlockScreen();
                        return result;
                    };
                    std::vector<std::vector<byte>> cleanPages;
                    auto wheel = [&](float delta) {
                        Event event{};
                        event.type = EVENT_MOUSEWHEEL;
                        event.mouse.x = box.x + 80;
                        event.mouse.y = box.y + 80;
                        event.wheelY = delta;
                        g_vm->_toolBase->handleMouse(event, 0);
                        ++wheelChecks;
                    };
                    if (script == resImports->reserved[0]) {
                        // Capture every credits spread, including fragmented role descriptions.
                        for (int page = 0; page < document.pageCount(); page += app.numPages) {
                            document.gotoPage(page);
                            g_platform->capture(output / String::format("credits-%d-%d-%d-%d.ppm",
                                                                        wide, sidebar, int(value),
                                                                        page)
                                                             .c_str());
                        }
                    }
                    for (int page = 0; page < document.pageCount(); page += app.numPages) {
                        document.gotoPage(page);
                        const auto incremental = pixels();
                        static_cast<gWindow&>(document).draw();
                        cleanPages.push_back(pixels());
                        if (incremental != cleanPages.back()) {
                            g_platform->capture(output / "document-redraw-clean.ppm");
                            throw std::runtime_error(
                                String::format("Document redraw differs: script=%u appearance=%d "
                                               "wide=%d sidebar=%d language=%d page=%d",
                                               script, appearance, wide, sidebar, int(value), page)
                                    .c_str());
                        }
                        ++redrawChecks;
                        if (!sidebar && page == 0 && !captured[appearance]) {
                            screenshotHotkey();
                            ++screenshots;
                            g_platform->capture(output / String::format("document-%u-%d-%d-%d.ppm",
                                                                        script, appearance, wide,
                                                                        int(value))
                                                             .c_str());
                            captured[appearance] = true;
                        }
                        ++pages;
                    }
                    if (document.pageCount() > app.numPages) {
                        ++multipageDocuments;
                    }
                    // Compare reverse navigation with independently repainted pages. Vertical
                    // documents use the same wheel dispatcher as normal gameplay, including limits.
                    for (int spread = int(cleanPages.size()) - 1; spread >= 0; --spread) {
                        if (appearance) {
                            wheel(spread == int(cleanPages.size()) - 1 ? -1 : 1);
                        } else {
                            document.gotoPage(spread * app.numPages);
                        }
                        require(document.currentPage() == spread * app.numPages,
                                "document reverse navigation selected the wrong page");
                        require(pixels() == cleanPages[spread],
                                "document reverse navigation retained another page's pixels");
                        ++redrawChecks;
                    }
                    if (appearance) {
                        for (int page = 0; page <= document.pageCount(); ++page) {
                            wheel(page == 0 ? 1 : -1);
                            const int expected = std::min(page, int(document.pageCount()) - 1);
                            require(document.currentPage() == expected &&
                                        pixels() == cleanPages[expected],
                                    "document forward wheel retained another page's pixels");
                            ++redrawChecks;
                            if (!capturedPaging[appearance] && document.pageCount() > 1 && page < 2)
                                g_platform->capture(
                                    output / String::format("paging-%d-%d-%d-%d-%d.ppm", appearance,
                                                            wide, sidebar, int(value), page)
                                                 .c_str());
                        }
                        if (document.pageCount() > 1)
                            capturedPaging[appearance] = true;
                    }
                    document.close();
                    if (GameMode::_newmodeFlag)
                        GameMode::update();
                }
            }
        }
    std::ofstream(output / "documents.json")
        << "{\"scripts\":" << scripts.size() << ",\"page_spreads\":" << pages
        << ",\"redraw_comparisons\":" << redrawChecks << ",\"wheel_checks\":" << wheelChecks
        << ",\"multipage_documents\":" << multipageDocuments << "}\n";
    return pages;
}

unsigned placards(const Path& output) {
    using namespace Saga2;
    auto* table = segmentAddress(71, 2);
    const int count = READ_LE_UINT16(table) / 2;
    unsigned cases = 0;
    for (bool wide : {false, true}) {
        for (bool sidebar : {false, true}) {
            ConfMan.setBool("widescreen", wide);
            ConfMan.setBool("alternate_sidebar", sidebar);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (auto value :
                 {Language::English, Language::Czech, Language::Spanish, Language::French}) {
                setLanguage(value);
                for (int i = 0; i < count; ++i) {
                    const char* original =
                        reinterpret_cast<const char*>(table + READ_LE_UINT16(table + 2 * i));
                    if (strchr(original, '%'))
                        continue;
                    const char* translated = localizedScript(71, 2, i, original, value);
                    const auto before = textOverflowCount();
                    textPallete pal(62, 64, 67, 11, 23, 17);
                    CPlacardWindow window(Rect16(201, 111, 238, 145), 0, nullptr,
                                          const_cast<char*>(translated), pal, &Plate18Font);
                    if (textOverflowCount() != before)
                        warning("Placard overflow: script:71:2:%d language=%s", i,
                                languageCode(value));
                    ++cases;
                }
                int ticks = 0;
                gameFrameHook = [&] {
                    require(++ticks < 10, "message dialog did not close");
                    if (!ModalWindow::_current)
                        return;
                    g_platform->capture(
                        output / String::format("message-%s.ppm", languageCode(value)).c_str());
                    key(SDLK_ESCAPE);
                };
                userDialog("Load Game", "No saved games yet.\nUse Save in Options to create one.",
                           "OK", nullptr, nullptr);
                gameFrameHook = {};
            }
        }
    }
    return cases;
}

unsigned liveOptions(const Path& output) {
    using namespace Saga2;
    const auto calendar = *g_vm->_calendar;
    const bool originalPanel = g_vm->_indivControlsFlag;
    const bool subtitles = g_vm->_speechText;
    // Isolate display changes from the existing timed ambient-sound RNG draws.
    const auto theme = g_vm->_grandMasterFTA->_currentTheme;
    g_vm->_grandMasterFTA->_currentTheme = 0;
    g_vm->_calendar->_hour = g_vm->_calendar->_frameInHour = 0;
    unsigned cases = 0;
    for (bool wide : {false, true})
        for (bool sidebar : {false, true})
            for (auto source :
                 {Language::English, Language::Czech, Language::Spanish, Language::French})
                for (bool pregame : {false, true})
                    for (bool individual : {false, true}) {
                        if (GameMode::_newmodeFlag)
                            GameMode::update();
                        setLanguage(source);
                        ConfMan.setBool("widescreen", wide);
                        ConfMan.setBool("alternate_sidebar", sidebar);
                        requestDisplayModeChange();
                        applyPendingDisplayMode();
                        if (individual)
                            setControlPanelsToIndividualMode(0);
                        else
                            setControlPanelsToTrioMode();
                        ContainerNode floating(*g_vm->_cnm, getCenterActorID(),
                                               ContainerNode::kMentalType);
                        floating.show();
                        auto* inventory = floating.getWindow();
                        inventory->setExtent(Rect16(wide ? 680 : 400, 180, 158, 215));
                        const auto actor = getCenterActorID();
                        const auto position = centerActorCoords();
                        const auto seed = g_vm->_rnd->getSeed();
                        const bool night = g_vm->_showNight, aggression = g_vm->_autoAggression;
                        const bool weapon = g_vm->_autoWeapon, clock = g_vm->_showGameTime;
                        const auto speech = g_vm->_speechText;
                        const std::array<int, 3> volumes = {ConfMan.getInt("sfx_volume"),
                                                            ConfMan.getInt("speech_volume"),
                                                            ConfMan.getInt("music_volume")};
                        int phase = 0, ticks = 0, delay = 0;
                        ModalWindow* options = nullptr;
                        auto press = [&](int x, int y) {
                            auto* control = options->hitTest(Point16(x, y));
                            require(control, "live setting control missing");
                            clickChoice(control, false);
                        };
                        gameFrameHook = [&] {
                            require(++ticks < 120, "live Options timed out");
                            // Map redraws can rediscover the terrain's ambient theme.
                            g_vm->_grandMasterFTA->_currentTheme = 0;
                            if (delay-- > 0)
                                return;
                            delay = 1;
                            auto* modal = ModalWindow::_current;
                            if (!modal)
                                return;
                            if (!options)
                                options = modal;
                            require(options == modal, "live setting replaced Options");
                            require(
                                actor == getCenterActorID() && position == centerActorCoords() &&
                                    seed == g_vm->_rnd->getSeed(),
                                String::format("live setting changed simulation: case=%u phase=%d "
                                               "actor=%u/%u rng=%u/%u position=%d,%d/%d,%d",
                                               cases, phase, actor, getCenterActorID(), seed,
                                               g_vm->_rnd->getSeed(), position.u, position.v,
                                               centerActorCoords().u, centerActorCoords().v)
                                    .c_str());
                            require(volumes == std::array<int, 3>{ConfMan.getInt("sfx_volume"),
                                                                  ConfMan.getInt("speech_volume"),
                                                                  ConfMan.getInt("music_volume")},
                                    "display setting reset sliders");
                            if (phase == 0)
                                press(223, 145);
                            else if (phase <= 6) {
                                const bool selected = phase % 2 ? !wide : wide;
                                require((activeDisplayMode == DisplayMode::Widescreen) ==
                                                selected &&
                                            g_platform->getWidth() == (selected ? 854 : 640),
                                        "Widescreen did not apply in open Options");
                                require(options->getExtent().x ==
                                            (g_platform->getWidth() - 487) / 2,
                                        "live Options is not centered");
                                require(floating.getWindow() == inventory &&
                                            inventory->getExtent().x +
                                                    inventory->getExtent().width <=
                                                g_platform->getWidth(),
                                        "floating inventory was lost or clipped");
                                if (phase < 6)
                                    press(223, 145);
                                else {
                                    g_platform->capture(
                                        output /
                                        String::format("live-options-%02u-width.ppm", cases)
                                            .c_str());
                                    press(223, 168);
                                }
                            } else if (phase == 7 || phase == 8) {
                                const bool selected = phase == 7 ? !sidebar : sidebar;
                                require(menConBtn->getExtent().width == (selected ? 28 : 44),
                                        "sidebar did not apply in open Options");
                                if (phase == 7)
                                    press(223, 168);
                                else
                                    press(223, 191);
                            } else if (phase == 9 || phase == 10) {
                                require(g_vm->_showGameTime == (phase == 9 ? !clock : clock),
                                        "game clock did not apply in open Options");
                                if (phase == 9)
                                    press(223, 191);
                                else
                                    press(23, 214);
                            } else if (phase == 11 || phase == 12) {
                                require(g_vm->_showNight == (phase == 11 ? !night : night),
                                        "Night did not toggle");
                                gPalette expected, actual;
                                g_vm->_pal->createPalette(&expected, g_vm->_pal->_midnightPalette,
                                                          g_vm->_pal->_noonPalette,
                                                          g_vm->_calendar->lightLevel(MAX_LIGHT),
                                                          MAX_LIGHT);
                                g_vm->_pal->getCurrentPalette(&actual);
                                require(!memcmp(&expected, &actual, sizeof(expected)),
                                        "Night palette did not apply in open Options");
                                if (phase == 11) {
                                    g_platform->capture(
                                        output /
                                        String::format("live-options-%02u-night.ppm", cases)
                                            .c_str());
                                    press(23, 214);
                                } else
                                    press(23, 145);
                            } else if (phase == 13 || phase == 14) {
                                require(g_vm->_autoAggression ==
                                            (phase == 13 ? !aggression : aggression),
                                        "auto-aggression did not apply immediately");
                                if (phase == 13)
                                    press(23, 145);
                                else
                                    press(23, 168);
                            } else if (phase == 15 || phase == 16) {
                                require(g_vm->_autoWeapon == (phase == 15 ? !weapon : weapon),
                                        "auto-weapon did not apply immediately");
                                if (phase == 15)
                                    press(23, 168);
                                else
                                    press(23, 191);
                            } else if (phase == 17 || phase == 18) {
                                require(g_vm->_speechText == (phase == 17 ? !speech : speech),
                                        "subtitles did not apply immediately");
                                if (phase == 17)
                                    press(23, 191);
                                else if (pregame)
                                    static_cast<requestInfo*>(options->_userData)->running = false;
                                else
                                    key(SDLK_ESCAPE);
                            }
                            ++phase;
                        };
                        OptionsDialog(pregame);
                        gameFrameHook = {};
                        require(phase == 19 && !ModalWindow::_current,
                                "live Options did not close");
                        require(ConfMan.getBool("widescreen") == wide &&
                                    ConfMan.getBool("alternate_sidebar") == sidebar &&
                                    g_vm->_showGameTime == clock,
                                "closing Options reverted a setting");
                        ++cases;
                    }
    *g_vm->_calendar = calendar;
    g_vm->_grandMasterFTA->_currentTheme = theme;
    g_vm->_speechText = subtitles;
    ConfMan.setBool("subtitles", subtitles);
    if (originalPanel)
        setControlPanelsToIndividualMode(0);
    else
        setControlPanelsToTrioMode();
    return cases;
}

unsigned fileDialogs(const Path& output) {
    using namespace Saga2;
    unsigned cases = 0;
    for (bool wide : {false, true})
        for (bool sidebar : {false, true}) {
            ConfMan.setBool("widescreen", wide);
            ConfMan.setBool("alternate_sidebar", sidebar);
            requestDisplayModeChange();
            applyPendingDisplayMode();
            for (auto value :
                 {Language::English, Language::Czech, Language::Spanish, Language::French}) {
                setLanguage(value);
                for (int mode : {0, 1}) {
                    int ticks = 0;
                    gameFrameHook = [&] {
                        require(++ticks < 10, "file dialog did not close");
                        if (!ModalWindow::_current)
                            return;
                        g_platform->capture(output / String::format("files-%d-%d-%d-%d.ppm", wide,
                                                                    sidebar, int(value), mode)
                                                         .c_str());
                        key(SDLK_ESCAPE);
                    };
                    require(FileDialog(mode) <= 0, "file dialog cancellation result changed");
                    gameFrameHook = {};
                    ++cases;
                }
            }
        }
    return cases;
}
} // namespace

void configureLocalizationProbe(const Path& output) {
    std::filesystem::create_directories(output);
    auto frame = std::make_shared<int>(0);
    gameFrameHook = [output, frame] {
        if (++*frame != 2)
            return;
        const auto startupLanguage = language();
        const bool startupSubtitles = ConfMan.getBool("subtitles");
        resetTextOverflowCount();
        screenshotHotkey();
        int number = 0;
        for (bool wide : {false, true}) {
            for (bool sidebar : {false, true}) {
                ConfMan.setBool("widescreen", wide);
                ConfMan.setBool("alternate_sidebar", sidebar);
                Saga2::requestDisplayModeChange();
                Saga2::applyPendingDisplayMode();
                for (auto source :
                     {Language::English, Language::Czech, Language::Spanish, Language::French}) {
                    for (bool pregame : {false, true}) {
                        setLanguage(source);
                        const auto target =
                            static_cast<Language>((static_cast<unsigned>(source) + 1) % 4);
                        menuCase(output, pregame, true, target, number++);
                        menuCase(output, pregame, false, target, number++);
                    }
                }
            }
        }
        const auto liveCases = liveOptions(output);
        portraitPlates(output);
        const auto manaCases = manaTooltips(output);
        const auto nameCases = nameBounds();
        statusMessages();
        speechSaves(output);
        mixedSpeechSaves(output);
        speechVoices(output);
        unsigned documentScreenshots = 0;
        const auto documentPages = documents(output, documentScreenshots);
        const auto placardCases = placards(output);
        const auto fileDialogCases = fileDialogs(output);
        require(textOverflowCount() == 0, "translated menu text overflows");
        setLanguage(Language::Czech);
        std::ofstream(output / "localization.json")
            << "{\"status\":\"passed\",\"menu_cases\":" << number
            << ",\"mana_tooltip_cases\":" << manaCases << ",\"name_tooltip_cases\":" << nameCases
            << ",\"live_options_cases\":" << liveCases
            << ",\"text_overflows\":0,\"speech_save_roundtrip\":true,\"mixed_speech_save_"
               "roundtrip\":true,\"voice_variants\":9,"
               "\"radio_clicks\":192,\"status_queue_refresh\":true,\"immediate_language_apply\":"
               "true,"
               "\"same_language_preserves_subtitles\":true,\"modal_screenshots\":192,"
               "\"gameplay_screenshots\":1,\"document_screenshots\":"
            << documentScreenshots
            << ","
               "\"document_spreads\":"
            << documentPages << ",\"placard_cases\":" << placardCases
            << ",\"file_dialog_cases\":" << fileDialogCases << ",\"startup_language\":\""
            << languageCode(startupLanguage)
            << "\",\"startup_subtitles\":" << (startupSubtitles ? "true" : "false") << "}\n";
        Saga2::g_vm->_gameRunning = false;
    };
}
} // namespace Native
