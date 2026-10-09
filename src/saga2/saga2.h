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

#ifndef SAGA2_H
#define SAGA2_H

#include "platform/runtime.h"

#include "saga2/gfx.h"
#include "saga2/idtypes.h"
#include "saga2/vdraw.h"
#include "saga2/weapons.h"

namespace Video {
class SmackerDecoder;
}

namespace Native {
class MemoryWriteStreamDynamic;
}

namespace Saga2 {

class ContainerManager;
class Console;
class Timer;
class TimerList;
class BandList;
class ActorAppearance;
class PathRequest;
class MotionTask;
class MotionTaskList;
class CMassWeightIndicator;
class GrabInfo;
class CImageCache;
class SensorList;
class Sensor;
class EffectDisplayPrototypeList;
class SpellDisplayPrototypeList;
class DisplayNodeList;
class SpellDisplayList;
class gMousePointer;
class ActiveRegion;
class gToolBase;
class Properties;
class PlayerActor;
class ProtoObj;
class ActorProto;
class HandleArray;
class TileActivityTaskList;
class TaskStackList;
class TaskList;
class Deejay;
class frameSmoother;
class frameCounter;
class CMapFeature;
class AudioInterface;
class PaletteManager;
class ActorManager;
class CalendarTime;
class TileModeManager;
struct SAGA2GameDescription;

enum {
    kDebugResources = 1,
    kDebugActors,
    kDebugScripts,
    kDebugEventLoop,
    kDebugInit,
    kDebugTiles,
    kDebugPalettes,
    kDebugLoading,
    kDebugTimers,
    kDebugPath,
    kDebugTasks,
    kDebugSound,
    kDebugSaveload,
    kDebugSensors,
};

#define TICKSPERSECOND (728L / 10L)

class Saga2Engine {
  public:
    Saga2Engine();
    ~Saga2Engine();

    Native::Error run();

    int getGameId() const;

    Native::Error saveGameState(int slot, const Native::String& desc, bool isAutosave);
    Native::Error loadGameState(int slot);
    void syncSoundSettings();

    Native::String getSavegameFile(int num);
    const Native::GameFileDescription* getFilesDescriptions() const;
    Native::Platform* getEventManager() { return g_platform; }
    Native::TimerManager* getTimerManager() { return &g_platform->timers; }
    Native::SaveFileManager* getSaveFileManager() { return &g_platform->saves; }
    bool shouldQuit() { return g_platform->quitting; }

    void loadExeResources();
    void freeExeResources();

    void startVideo(const char* fileName, int x, int y);
    bool checkVideo();
    void endVideo();
    void abortVideo();

    void readConfig();
    void saveConfig();

  public:
    // We need random numbers
    Native::RandomSource* _rnd;
    Console* _console;
    Renderer* _renderer;
    AudioInterface* _audio;
    PaletteManager* _pal;
    ActorManager* _act;
    CalendarTime* _calendar;
    TileModeManager* _tmm;
    ContainerManager* _cnm;

    WeaponStuff _weaponRack[kMaxWeapons];
    weaponID _loadedWeapons;
    Native::Array<char*> _nameList;
    Native::Array<PlayerActor*> _playerList;
    Native::Array<ProtoObj*> _objectProtos;
    Native::Array<ActorProto*> _actorProtos;
    Native::Array<CMapFeature*> _mapFeatures;
    Native::List<TimerList*> _timerLists;
    Native::List<Timer*> _timers;
    Native::List<ActorAppearance*> _appearanceLRU;
    Native::List<PathRequest*> _pathQueue;
    Native::List<SensorList*> _sensorListList;
    Native::List<Sensor*> _sensorList;
    Native::List<CMassWeightIndicator*> _indList;
    Native::List<int> _platformLRU;
    BandList* _bandList;
    MotionTaskList* _mTaskList;
    CImageCache* _imageCache;
    GrabInfo* _mouseInfo;
    EffectDisplayPrototypeList* _edpList;
    SpellDisplayPrototypeList* _sdpList;
    DisplayNodeList* _mainDisplayList;
    SpellDisplayList* _activeSpells;
    gMousePointer* _pointer;
    ActiveRegion* _activeRegionList;
    gToolBase* _toolBase;
    Properties* _properties;
    HandleArray* _tileImageBanks;
    TileActivityTaskList* _aTaskList;
    TaskStackList* _stackList;
    TaskList* _taskList;
    Deejay* _grandMasterFTA;
    frameSmoother* _frate;
    frameCounter* _lrate;

    gDisplayPort _mainPort;
    gPort _backPort;
    gPixelMap _tileDrawMap;

    bool _gameRunning;
    bool _autoAggression;
    bool _autoWeapon;
    bool _showNight;
    bool _speechText;
    bool _speechVoice;

    bool _teleportOnClick;
    bool _teleportOnMap;
    bool _showPosition;
    bool _showStats;
    bool _showStatusMsg;

    bool _indivControlsFlag;
    bool _userControlsSetup;
    int _fadeDepth;
    int _currentMapNum;

  private:
    int _videoX, _videoY;
};

extern Saga2Engine* g_vm;

} // End of namespace Saga2

#endif
