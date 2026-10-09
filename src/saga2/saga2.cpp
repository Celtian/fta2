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

#include "platform/runtime.h"
#include "platform/saves.h"

#include "saga2/fta.h"
#include "saga2/saga2.h"

#include "saga2/actor.h"
#include "saga2/audio.h"
#include "saga2/band.h"
#include "saga2/beegee.h"
#include "saga2/calendar.h"
#include "saga2/console.h"
#include "saga2/contain.h"
#include "saga2/detection.h"
#include "saga2/dispnode.h"
#include "saga2/gdraw.h"
#include "saga2/imagcach.h"
#include "saga2/motion.h"
#include "saga2/mouseimg.h"
#include "saga2/music.h"
#include "saga2/panel.h"
#include "saga2/player.h"
#include "saga2/spelshow.h"
#include "saga2/tilemode.h"
#include "saga2/vpal.h"

namespace Saga2 {

void main_saga2();

Saga2Engine* g_vm;

Saga2Engine::Saga2Engine() {
    // Don't forget to register your random source
    _rnd = new Native::RandomSource("saga2");

    g_vm = this;

    _console = nullptr;
    _renderer = nullptr;
    _audio = nullptr;
    _pal = nullptr;
    _act = nullptr;
    _calendar = nullptr;
    _tmm = nullptr;
    _cnm = nullptr;

    _bandList = nullptr;
    _mouseInfo = nullptr;

    _videoX = _videoY = 0;
    _loadedWeapons = 0;

    _gameRunning = true;
    _autoAggression = true;
    _autoWeapon = true;
    _showNight = true;
    _speechText = true;
    _speechVoice = true;

    _showPosition = false;
    _showStats = false;
    _showStatusMsg = false;
    _teleportOnClick = false;
    _teleportOnMap = false;

    _indivControlsFlag = false;
    _userControlsSetup = false;
    _fadeDepth = 1;
    _currentMapNum = 0;

    _loadedWeapons = 0;

    _imageCache = new CImageCache;
    _mTaskList = new MotionTaskList;
    _bandList = new BandList();
    _mainDisplayList = new DisplayNodeList;
    _activeSpells = new SpellDisplayList(kMaxActiveSpells);
    _pointer = new gMousePointer(_mainPort);
    _activeRegionList = new ActiveRegion[kPlayerActors];
    _toolBase = new gToolBase;
    _properties = new Properties;
    _aTaskList = new TileActivityTaskList;
    _grandMasterFTA = new Deejay;

    _edpList = nullptr;
    _sdpList = nullptr;
    _tileImageBanks = nullptr;
    _stackList = nullptr;
    _taskList = nullptr;
    _frate = nullptr;
    _lrate = nullptr;
}

Saga2Engine::~Saga2Engine() {
    debug("Saga2Engine::~Saga2Engine");

    freeExeResources();

    // Dispose your resources here
    delete _console;
    delete _rnd;
    for (auto player : _playerList)
        delete player;
    _playerList.clear();
    delete _renderer;
    delete _pal;
    delete _act;
    delete _calendar;
    delete _tmm;
    delete _cnm;

    delete _imageCache;
    delete _mTaskList;
    delete _bandList;
    delete _mainDisplayList;
    delete _activeSpells;
    delete _pointer;
    delete[] _activeRegionList;
    delete _toolBase;
    delete _properties;
    delete _aTaskList;
    delete _grandMasterFTA;
    if (_mainPort._map) {
        delete[] _mainPort._map->_data;
        delete _mainPort._map;
        _mainPort._map = nullptr;
    }
}

Native::Error Saga2Engine::run() {
    // Initialize graphics using following:
    g_platform->initGraphics(640, 480);

    _console = new Console(this);

    _renderer = new Renderer();

    _pal = new PaletteManager;
    _act = new ActorManager;
    _calendar = new CalendarTime;
    _tmm = new TileModeManager;
    _cnm = new ContainerManager;

    readConfig();

    if (getGameId() == GID_FTA2)
        loadExeResources();
    // TODO: Load EXE resources for Dino

    main_saga2();

    return Native::kNoError;
}

int Saga2Engine::getGameId() const { return GID_FTA2; }
const Native::GameFileDescription* Saga2Engine::getFilesDescriptions() const {
    static const Native::GameFileDescription files[] = {
        {"ftadata.hrs", GAME_RESOURCEFILE}, {"scripts.hrs", GAME_SCRIPTFILE},
        {"ftasound.hrs", GAME_SOUNDFILE},   {"ftaimage.hrs", GAME_IMAGEFILE},
        {"fta.hrs", GAME_OBJRESOURCEFILE},  {nullptr, 0}};
    return files;
}
Native::String Saga2Engine::getSavegameFile(int slot) {
    if (slot < 0 || slot > 999)
        error("Save slot must be in range 0..999");
    return Native::String::format("fta2.%03d", slot);
}
Native::Error Saga2Engine::saveGameState(int slot, const Native::String& desc, bool) {
    pauseTimer();
    try {
        std::unique_ptr<Native::OutSaveFile> out(
            getSaveFileManager()->openForSaving(getSavegameFile(slot), false));
        if (!out) {
            resumeTimer();
            return Native::kCreatingFileFailed;
        }
        saveGame(out.get(), desc);
        out->write("NRNG", 4);
        out->writeUint32LE(4);
        out->writeUint32LE(_rnd->getSeed());
        out->finalize();
    } catch (...) {
        resumeTimer();
        throw;
    }
    resumeTimer();
    return Native::kNoError;
}

Native::Error Saga2Engine::loadGameState(int slot) {
    std::unique_ptr<Native::InSaveFile> input(
        getSaveFileManager()->openForLoading(getSavegameFile(slot)));
    if (!input)
        error("Cannot open save slot %d", slot);
    Native::validateSave(*input);
    loadGame(slot);

    return Native::kNoError;
}

void Saga2Engine::syncSoundSettings() {
    g_platform->getMixer()->syncSoundSettings();

    _speechText = !ConfMan.hasKey("subtitles") || ConfMan.getBool("subtitles");
    // Unvoiced dialogue uses the original text fallback, regardless of this preference.
    _speechVoice = false;

    if (_audio && _audio->_music)
        _audio->_music->syncSoundSettings();
}

int32 clamp(int32 a, int32 val, int32 c) {
    if (val < a)
        return a;
    if (val > c)
        return c;
    return val;
}

gFont Onyx10Font;
gFont Plate18Font;
gFont Helv11Font;
gFont Amber13Font;
gFont ThinFix8Font;
gFont Script10Font;

uint32 loadingWindowWidth = 640;
uint32 loadingWindowHeight = 480;

uint8* loadingWindowPalette;
uint8* loadingWindowData;

uint8* ColorMapRanges;

uint8* closeBx1ImageData;
uint8* closeBx2ImageData;
uint8* usePtrImageData;
uint8* xPointerImageData;
uint8* arrowImageData;
uint8* grabPtrImageData;
uint8* attakPtrImageData;
uint8* centerActorIndicatorImageData;
uint8* pgUpImageData;
uint8* pgDownImageData;
uint8* pgLeftImageData;
uint8* pgRightImageData;
uint8* autoWalkImageData;
uint8* gaugeImageData;

static void loadFont(Native::File& file, gFont* font, uint32 offset) {
    file.seek(offset);

    font->height = file.readUint16LE();
    font->baseLine = file.readUint16LE();
    font->rowMod = file.readUint16LE();

    for (int i = 0; i < 256; i++)
        font->charXOffset[i] = file.readUint16LE();

    file.read(font->charWidth, 256);
    file.read(font->charKern, 256);
    file.read(font->charSpace, 256);

    uint size = font->height * font->rowMod;

    font->fontdata = (byte*)malloc(size);
    file.read(font->fontdata, size);
}

struct dataChunks {
    uint8** ptr;
    bool font;
} chunks[] = {{(uint8**)&Onyx10Font, true},
              {(uint8**)&Plate18Font, true},
              {(uint8**)&Helv11Font, true},
              {(uint8**)&Amber13Font, true},
              {(uint8**)&ThinFix8Font, true},
              {(uint8**)&Script10Font, true},
              {&loadingWindowPalette, false},
              {&loadingWindowData, false},
              {&ColorMapRanges, false},
              {&closeBx1ImageData, false},
              {&closeBx2ImageData, false},
              {&usePtrImageData, false},
              {&xPointerImageData, false},
              {&arrowImageData, false},
              {&grabPtrImageData, false},
              {&attakPtrImageData, false},
              {&centerActorIndicatorImageData, false},
              {&pgUpImageData, false},
              {&pgDownImageData, false},
              {&pgLeftImageData, false},
              {&pgRightImageData, false},
              {&autoWalkImageData, false},
              {&gaugeImageData, false},
              {nullptr, false}};

void Saga2Engine::loadExeResources() {
    Native::File exe;
    if (exe.open("fta2-dos-resources.bin")) {
        if (exe.readUint32BE() != MKTAG('F', '2', 'R', '1') || exe.readUint32LE() != 23)
            error("Invalid DOS resource pack");
        const char* names[] = {"Onyx10Font",
                               "Plate18Font",
                               "Helv11Font",
                               "Amber13Font",
                               "ThinFix8Font",
                               "Script10Font",
                               "loadingWindowPalette",
                               "loadingWindowData",
                               "ColorMapRanges",
                               "closeBx1ImageData",
                               "closeBx2ImageData",
                               "usePtrImageData",
                               "xPointerImageData",
                               "arrowImageData",
                               "grabPtrImageData",
                               "attakPtrImageData",
                               "centerActorIndicatorImageData",
                               "pgUpImageData",
                               "pgDownImageData",
                               "pgLeftImageData",
                               "pgRightImageData",
                               "autoWalkImageData",
                               "gaugeImageData"};
        for (uint i = 0; i < ARRAYSIZE(names); ++i) {
            exe.seek(8 + i * 40);
            char name[33] = {};
            exe.read(name, 32);
            uint32 start = exe.readUint32LE();
            uint32 size = exe.readUint32LE();
            if (strcmp(name, names[i]) || start > (uint32)exe.size() ||
                size > (uint32)exe.size() - start)
                error("Invalid DOS resource entry %u", i);
            if (i < 6) {
                loadFont(exe, (gFont*)chunks[i].ptr, start);
            } else {
                *chunks[i].ptr = (uint8*)malloc(size);
                exe.seek(start);
                if (exe.read(*chunks[i].ptr, size) != size)
                    error("Truncated DOS resource entry %u", i);
            }
        }
        initCursors();
        return;
    }

    error("Verified DOS resource pack is missing");
}

void Saga2Engine::freeExeResources() {
    for (int i = 0; chunks[i].ptr; i++)
        if (chunks[i].font) // Font
            free(((gFont*)chunks[i].ptr)->fontdata);
        else
            free(*chunks[i].ptr);

    freeCursors();
}

void Saga2Engine::readConfig() {
    _autoWeapon = true;

    if (ConfMan.hasKey("auto_weapon"))
        _autoWeapon = ConfMan.getBool("auto_weapon");

    _autoAggression = true;

    if (ConfMan.hasKey("auto_aggression"))
        _autoAggression = ConfMan.getBool("auto_aggression");

    _showNight = true;

    if (ConfMan.hasKey("show_night"))
        _showNight = ConfMan.getBool("show_night");

    syncSoundSettings();
}

void Saga2Engine::saveConfig() { ConfMan.flushToDisk(); }

} // End of namespace Saga2
