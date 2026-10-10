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
 *
 * Based on the original sources
 *   Faery Tale II -- The Halls of the Dead
 *   (c) 1993-1996 The Wyrmkeep Entertainment Co.
 */

#include "platform/runtime.h"

#include "saga2/saga2.h"
#include "saga2/display.h"
#include "saga2/intrface.h"
#include "saga2/loadmsg.h"
#include "saga2/grabinfo.h"
#include "saga2/vpal.h"
#include "saga2/tile.h"
#include "saga2/tilemode.h"
#include "saga2/modal.h"
#include "saga2/calendar.h"
#include "saga2/fontlib.h"
#include "saga2/inventory_ui.h"

namespace Saga2 {

extern bool             delayReDraw;
extern BackWindow       *mainWindow;            // main window...

/* ===================================================================== *
   Globals
 * ===================================================================== */

bool                        paletteMayHaveChanged = false;


/* ===================================================================== *
   Locals
 * ===================================================================== */

static uint32 displayStatus = kDisGraphicsInit;
static bool paletteSuspendFlag = false;
static bool displayModePending = false;

void requestDisplayModeChange() {
	displayModePending = true;
}

bool applyPendingDisplayMode() {
	if (!displayModePending || ModalWindow::_current || !mainWindow ||
	    !g_vm->_tileDrawMap._data || !g_vm->_gameRunning)
		return false;
	displayModePending = false;
	const bool clockChanged = g_vm->_showGameTime != ConfMan.getBool("show_game_time");
	g_vm->_showGameTime = ConfMan.getBool("show_game_time");
	const auto mode = ConfMan.getBool("widescreen") ? Native::DisplayMode::Widescreen
	                                               : Native::DisplayMode::Classic;
	if (mode == Native::activeDisplayMode) {
		const bool sidebarChanged = applySidebarLayout();
		if (clockChanged && !sidebarChanged) {
			reDrawScreen();
			g_platform->updateScreen();
		}
		return clockChanged || sidebarChanged;
	}
	const auto layout = Native::DisplayLayout::forMode(mode);
	const int delta = layout.sidebarOffset - Native::displayLayout().sidebarOffset;
	const int mapWidth = (layout.mapWidth + kTileWidth - 1) & ~kTileDXMask;
	const int mapHeight = (layout.mapHeight + kTileWidth - 1) & ~kTileDXMask;
	std::unique_ptr<uint8[]> screen, map;
	try {
		screen = std::make_unique<uint8[]>(layout.screenWidth * layout.screenHeight);
		map = std::make_unique<uint8[]>(mapWidth * mapHeight);
	} catch (const std::bad_alloc &) {
		ConfMan.setBool("widescreen", Native::activeDisplayMode == Native::DisplayMode::Widescreen);
		ConfMan.flushToDisk();
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Display mode",
		                        "Cannot allocate the new display buffers.", nullptr);
		return false;
	}
	if (!g_platform->resizeGraphics(layout.screenWidth, layout.screenHeight)) {
		ConfMan.setBool("widescreen", Native::activeDisplayMode == Native::DisplayMode::Widescreen);
		ConfMan.flushToDisk();
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Display mode", SDL_GetError(), nullptr);
		return false;
	}
	// All allocations succeeded. No window, world or control is destroyed here.
	Native::activeDisplayMode = mode;
	for (int i = 0; i < kMaxBackBufferSources; ++i)
		g_vm->_renderer->removeSavedBackBuffer(static_cast<BackBufferSource>(i));
	delete[] g_vm->_mainPort._map->_data;
	g_vm->_mainPort._map->_data = screen.release();
	g_vm->_mainPort._map->_size = Point16(layout.screenWidth, layout.screenHeight);
	g_vm->_mainPort.setMap(g_vm->_mainPort._map);
	delete[] g_vm->_tileDrawMap._data;
	g_vm->_tileDrawMap._data = map.release();
	g_vm->_tileDrawMap._size = Point16(mapWidth, mapHeight);
	g_vm->_backPort.setMap(&g_vm->_tileDrawMap);
	mainWindow->setExtent(Rect16(0, 0, layout.screenWidth, layout.screenHeight));
	layoutUserControls(delta);
	layoutTileControls();
	extern gGenericControl *speakButtonPanel;
	speakButtonPanel->setControlExtent(mainWindow->getExtent());
	for (auto it = g_vm->_toolBase->bottomWindowIterator();
	     it != g_vm->_toolBase->topWindowIterator(); ++it) {
		auto *window = *it;
		if (window == mainWindow) continue;
		Rect16 r = window->getExtent();
		const int oldX = r.x, oldY = r.y;
		if (mode == Native::DisplayMode::Classic) {
			r.x = CLIP<int>(r.x, 0, MAX(0, layout.screenWidth - r.width));
			r.y = CLIP<int>(r.y, 0, MAX(0, layout.screenHeight - r.height));
		}
		window->setExtent(r); // Recalculate every window's clip, even if unmoved.
		// Floating inventory artwork is relative to its window origin.
		if (auto *decorated = dynamic_cast<DecoratedWindow *>(window);
		    decorated && !dynamic_cast<FloatingWindow *>(window)) {
			for (int i = 0; i < decorated->_numDecorations; ++i) {
				decorated->_decorations[i].extent.x += r.x - oldX;
				decorated->_decorations[i].extent.y += r.y - oldY;
			}
		}
	}
	g_vm->_toolBase->leavePanel();
	layoutMapView(delta);
	Native::Event mouse;
	mouse.type = Native::EVENT_MOUSEMOVE;
	mouse.mouse = g_platform->mousePosition();
	g_vm->_toolBase->handleMouse(mouse, g_platform->getMillis());
	reDrawScreen();
	g_platform->updateScreen();
	return true;
}


/* ===================================================================== *
   Prototypes
 * ===================================================================== */

void localCursorOn();
void localCursorOff();
void resetInputDevices();
APPFUNC(cmdWindowFunc);                      // main window event handler
static void switchOn();
static void switchOff();

// ------------------------------------------------------------------------
// end game (normally)

void endGame() {
	blackOut();
	displayDisable(kDisGameEnded);
	g_vm->_gameRunning = false;
}


/* ===================================================================== *
   Display initialization
 * ===================================================================== */
void dayNightUpdate();
void fadeUp();
void displayUpdate();
void drawMainDisplay();

void niceScreenStartup() {
	if (ConfMan.hasKey("save_slot")) {
		cleanupGameState();
		loadSavedGameState(ConfMan.getInt("save_slot"));

		if (GameMode::_newmodeFlag)
			GameMode::update();
		updateActiveRegions();
	}

	blackOut();
	disablePaletteChanges();
	mainEnable();
	closeLoadMode();
	g_vm->_pointer->move(Point16(320, 240));
	//g_vm->_pointer->hide();
	enablePaletteChanges();
	displayUpdate();
	dayNightUpdate();
	fadeUp();
	g_vm->_pointer->manditoryShow();                        //  hide mouse pointer
	reDrawScreen();
	//g_vm->_pointer->show();
	updateAllUserControls();
	reDrawScreen();
	g_vm->_mouseInfo->replaceObject();
	g_vm->_mouseInfo->clearGauge();
	g_vm->_mouseInfo->setText(nullptr);
	g_vm->_mouseInfo->setIntent(GrabInfo::kIntWalkTo);
	resetInputDevices();
}

// ------------------------------------------------------------------------
// backbuffer startup

void initBackPanel() {
	if (mainWindow)
		return;

	mainWindow = new BackWindow(
	                 Rect16(0, 0, Native::displayLayout().screenWidth, Native::displayLayout().screenHeight),
	                 0,
	                 cmdWindowFunc);
	if (mainWindow == nullptr)
		error("Error initializing the back panel");
}

/* ===================================================================== *
   Display disable flags
 * ===================================================================== */

// ------------------------------------------------------------------------
// enable / disable blitting

void displayEnable(DisplayDisabledBecause reason, bool onOff) {
	bool prev = displayEnabled();
	if (!onOff)
		displayStatus |= reason;
	else
		displayStatus &= (~reason);
	if (prev != displayEnabled()) {
		if (displayEnabled())
			switchOn();
		else
			switchOff();
	}
}

// ------------------------------------------------------------------------
// This is a check to see if blitting is enabled

bool displayEnabled(uint32 mask) {
	return true;
}

bool displayOkay() {
	return displayEnabled();
}


// ------------------------------------------------------------------------
// Main on/off swiotch for display

void mainEnable() {
	displayEnable(kDisGameNotInitialized);
}

// ------------------------------------------------------------------------
// This is a check to see if blitting is enabled

void mainDisable() {
	displayDisable(kDisGameNotInitialized);
}

// ------------------------------------------------------------------------
// On/Off hooks

static void switchOn() {
	enableUserControls();
}

static void switchOff() {
	disableUserControls();
}

/* ===================================================================== *
   Palette disable hooks
 * ===================================================================== */

void enablePaletteChanges() {
	paletteSuspendFlag = false;
	paletteMayHaveChanged = true;
}

void disablePaletteChanges() {
	paletteSuspendFlag = true;
}

bool paletteChangesEnabled() {
	return !paletteSuspendFlag;
}

void drawGameTime(gPort &port, const Point16 &offset, const Rect16 &clip, bool captureBackground) {
	if (!g_vm->_showGameTime || !g_vm->_calendar) return;
	const auto &layout = Native::displayLayout();
	const Rect16 bounds(layout.mapX + 8, layout.mapY + 8, 112, 12);
	if (!bounds.overlap(clip)) return;
	// Floating windows share the map buffer. Keep the clean world behind this HUD
	// region so partial window updates can restore it without old windows or digits.
	static byte background[112 * 12];
	static int backgroundWidth = 0;
	if (captureBackground) {
		for (int y = 0; y < 12; ++y)
			memcpy(background + y * 112,
			       port._map->_data + (bounds.y - offset.y + y) * port._map->_size.x + bounds.x - offset.x,
			       112);
		backgroundWidth = layout.screenWidth;
	}
	byte pixels[112 * 12]{};
	gPixelMap stamp;
	stamp._size = Point16(112, 12);
	stamp._data = pixels;
	gPort stampPort;
	stampPort.setMap(&stamp);
	stampPort.setFont(&ThinFix8Font);
	stampPort.setColor(34);
	stampPort.setOutlineColor(24);
	stampPort.setStyle(kTextStyleOutline);
	stampPort.moveTo(1, 1);
	const auto &calendar = *g_vm->_calendar;
	const auto text = InventoryUI::clockText(calendar._days, calendar._hour,
	                                       calendar._frameInHour, CalendarTime::kFramesPerHour);
	stampPort.drawText(text.data());
	SAVE_GPORT_STATE(port);
	port.setMode(kDrawModeMatte);
	const auto savedClip = port._clip;
	port.setClip(intersect(savedClip, Rect16(clip.x - offset.x, clip.y - offset.y,
	                                       clip.width, clip.height)));
	if (!captureBackground && backgroundWidth == layout.screenWidth) {
		gPixelMap clean;
		clean._size = stamp._size;
		clean._data = background;
		port.setMode(kDrawModeReplace);
		port.bltPixels(clean, 0, 0, bounds.x - offset.x, bounds.y - offset.y, 112, 12);
		port.setMode(kDrawModeMatte);
	}
	port.bltPixels(stamp, 0, 0, bounds.x - offset.x, bounds.y - offset.y, 112, 12);
	port.setClip(savedClip);
}


/* ===================================================================== *
   Refresh
 * ===================================================================== */

// ------------------------------------------------------------------------
// notice that screen may be dirty

void delayedDisplayEnable() {
	delayReDraw = false;
}

// ------------------------------------------------------------------------
// notice that palette may be dirty

void externalPaletteIntrusion() {
	paletteMayHaveChanged = true;
}

// ------------------------------------------------------------------------
// force a full screen redraw

void reDrawScreen() {
	//dispMM("refresh");
	Rect16 r = Rect16(0, 0, Native::displayLayout().screenWidth, Native::displayLayout().screenHeight);
	if (mainWindow && displayEnabled()) {
		//updateAllUserControls();
		drawMainDisplay();
		mainWindow->invalidate(&r);
		delayReDraw = false;
		if (paletteMayHaveChanged) {
			paletteMayHaveChanged = false;
			g_vm->_pal->assertCurrentPalette();
			paletteMayHaveChanged = false;
		}
	} else
		delayReDraw = true;
	//mainWindow->invalidate(r);
}


/* ===================================================================== *
   Clear screen
 * ===================================================================== */

void blackOut() {
	g_vm->_mainPort._drawMode = kDrawModeReplace;
	g_vm->_mainPort.setColor(0);            //  fill screen with color
	g_vm->_mainPort.fillRect(Rect16(0, 0, Native::displayLayout().screenWidth, Native::displayLayout().screenHeight));
	g_vm->_pal->lightsOut();
}


/* ===================================================================== *
   Loading screen
 * ===================================================================== */

// ------------------------------------------------------------------------
// enable / disable blitting

void showLoadMessage() {
	uint32 saved = displayStatus;
	displayStatus = 0;
	loadingScreen();
	displayStatus = saved;
}


/* ===================================================================== *
   Video mode save and restore for videos
 * ===================================================================== */

void pushVidState() {
}

void popVidState() {
}

} // end of namespace Saga2
