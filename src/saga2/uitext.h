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
 * aint32 with this program; if not, write to the Free Software
 *
 *
 * Based on the original sources
 *   Faery Tale II -- The Halls of the Dead
 *   (c) 1993-1996 The Wyrmkeep Entertainment Co.
 */

#ifndef SAGA2_UITEXT_H
#define SAGA2_UITEXT_H

#include "platform/localization.h"

namespace Saga2 {

#define FILE_DIALOG_NONAME  Native::ui("Empty Slot", true)

#define SAVE_DIALOG_NAME    Native::ui("Save Game", true)
#define SAVE_DIALOG_BUTTON1 Native::ui("Cancel", true)
#define SAVE_DIALOG_BUTTON2 Native::ui("Save", true)

#define LOAD_DIALOG_NAME    Native::ui("Load Game", true)
#define LOAD_DIALOG_BUTTON1 Native::ui("Cancel", true)
#define LOAD_DIALOG_BUTTON2 Native::ui("Load", true)

#define OPTN_DIALOG_NAME    Native::ui("Options", true)
#define OPTN_DIALOG_SLIDE1  Native::ui("Sound Effects Volume", true)
#define OPTN_DIALOG_SLIDE2  Native::ui("Speech Volume", true)
#define OPTN_DIALOG_SLIDE3  Native::ui("Music Volume", true)
#define OPTN_DIALOG_CHECK1  Native::ui("Auto-Aggression", true)
#define OPTN_DIALOG_CHECK2  Native::ui("Auto-Wield Weapon", true)
#define OPTN_DIALOG_CHECK3  Native::ui("Speech Text", true)
#define OPTN_DIALOG_CHECK4  Native::ui("Night", true)
#define OPTN_DIALOG_BUTTON1 Native::ui("Resume Play", true)
#define OPTN_DIALOG_BUTTON2 Native::ui("Save", true)
#define OPTN_DIALOG_BUTTON3 Native::ui("Load", true)
#define OPTN_DIALOG_BUTTON4 Native::ui("Quit", true)
#define OPTN_DIALOG_BUTTON5 Native::ui("Credits", true)
#define OPTN_DIALOG_BUTTON6 Native::ui("New Game", true)
#define OPTN_DIALOG_HOT_KEYS Native::ui("Hot Keys", true)
#define HOT_KEYS_CLOSE      Native::ui("Close", true)

#define VFYX_DIALOG_NAME    Native::ui("Faery Tale Adventure", true)
#define VFYX_DIALOG_CAPTION Native::ui("Do you really want to quit?", true)
#define VFYX_DIALOG_BUTTON1 Native::ui("_Quit", true)
#define VFYX_DIALOG_BUTTON2 Native::ui("_Continue", true)

} // end of namespace Saga2

#endif
