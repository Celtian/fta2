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

#ifndef SAGA2_LOCALIZE_H
#define SAGA2_LOCALIZE_H

#include "platform/localization.h"

namespace Saga2 {

#define ALLOCATION_ERROR    "Required allocation failed"
#define RESHANDLE_ERROR     "Memory heap exhausted"

#define WOUNDED_STATUS      "%s is badly wounded!"
#define HURT_STATUS         "%s is hurt."
#define CALENDAR_STATUS     "It is %s, day %d, week %d."

#define HOUR00_TIME         "midnight"
#define HOUR02_TIME         "morning"
#define HOUR06_TIME         "mid-morning"
#define HOUR12_TIME         "noon"
#define HOUR18_TIME         "mid-afternoon"
#define HOUR22_TIME         "evening"


#define IDEAS_MENTAL        Native::ui("Ideas", true)
#define SPELL_MENTAL        Native::ui("Spells", true)
#define SKILL_MENTAL        Native::ui("Skills", true)

#define CLOSE_MOUSE         Native::ui("Close", true)
#define SCROLL_MOUSE        Native::ui("Scroll Inventory", true)

#define ASLEEP_STATE        Native::ui("Asleep", true)
#define PARALY_STATE        Native::ui("Paralysed", true)
#define BLIND_STATE         Native::ui("Blind", true)
#define AFRAID_STATE        Native::ui("Afraid", true)
#define ANGRY_STATE         Native::ui("Angry", true)
#define BADWND_STATE        Native::ui("Badly Wounded", true)
#define HURT_STATE          Native::ui("Hurt", true)
#define POISON_STATE        Native::ui("Poisoned", true)
#define DISEAS_STATE        Native::ui("Diseased", true)
#define NORMAL_STATE        Native::ui("Normal", true)
#define DEAD_STATE          Native::ui("Dead", true)

#define JULIAN_BROSTATE     Native::ui("Julian is: ", true)
#define PHILLIP_BROSTATE    Native::ui("Phillip is: ", true)
#define KEVIN_BROSTATE      Native::ui("Kevin is: ", true)

// FIXME: Spelling incorrect. Agression should be Aggression. Fixing changes game data and thus may cause issues
#define ON_AGRESS           Native::ui("Agression: On", true)
// FIXME: Spelling incorrect. Agression should be Aggression. Fixing changes game data and thus may cause issues
#define OFF_AGRESS          Native::ui("Agression: Off", true)

#define NO_ARMOR            Native::ui("No Armor Worn", true)
#define DESC_ARMOR          Native::ui("Absorb:%d, Reduce:/%d, Defense:+%d", true)

#define ON_CENTER           Native::ui("Center: On", true)
#define OFF_CENTER          Native::ui("Center: Off", true)

#define ON_BANDED           Native::ui("Banding: On", true)
#define OFF_BANDED          Native::ui("Banding: Off", true)

#define OPTIONS_PANEL       Native::ui("Options Panel", true)

#define HEALTH_HINT         Native::ui("Health:", true)
#define WEIGHT_HINT         Native::ui("Weight:", true)
#define GOLD_HINT           Native::ui("Gold:", true)
#define BULK_HINT           Native::ui("Bulk:", true)
#define UNK_WEIGHT_HINT     Native::ui("N/A", true)
#define UNK_BULK_HINT       Native::ui("N/A", true)

#define RED_MANA            Native::ui("Red Mana:", true)
#define ORANGE_MANA         Native::ui("Orange Mana:", true)
#define YELLOW_MANA         Native::ui("Yellow Mana:", true)
#define GREEN_MANA          Native::ui("Green Mana:", true)
#define BLUE_MANA           Native::ui("Blue Mana:", true)
#define VIOLET_MANA         Native::ui("Purple Mana:", true)


#define DOS_CMDLINE         "Syntax : FTA2 \n"
#define WIN_CMDLINE         "Syntax : FTA2 \n"

#define SINGLE_CHARGE       Native::ui("%s, %d Charge", true)
#define MULTI_CHARGE        Native::ui("%s, %d Charges", true)

#define PLURAL_DESC         Native::ui("%d %ss", true)

#define IDEAS_INVENT        Native::ui("Mental Inventory: Ideas", true)
#define SPELL_INVENT        Native::ui("Mental Inventory: Spells", true)
#define SKILL_INVENT        Native::ui("Mental Inventory: Skills", true)

#define ERROR_HEADING       Native::ui("Error", true)
#define ERROR_OK_BUTTON     Native::ui("OK", true)
#define ERROR_RE_BUTTON     Native::ui("_Retry", true)
#define ERROR_CA_BUTTON     Native::ui("_Cancel", true)
#define ERROR_YE_BUTTON     Native::ui("_Yes", true)
#define ERROR_NO_BUTTON     Native::ui("_No", true)

#define ARCHERY_SKILL       Native::ui("archery skill", true)
#define SWORD_SKILL         Native::ui("swordcraft skill", true)
#define SHIELD_SKILL        Native::ui("shieldcraft skill", true)
#define BLUDGEON_SKILL      Native::ui("bludgeon skill", true)
#define DEAD_SKILL          Native::ui("", true)
#define SPELL_SKILL         Native::ui("spellcraft", true)
#define DEAD2_SKILL         Native::ui("", true)
#define AGILITY_SKILL       Native::ui("agility", true)
#define BRAWN_SKILL         Native::ui("brawn", true)

#define SKILL_STATUS         "%s's %s has increased."
#define VITALITY_STATUS      "%s's vitality has increased."

#define ATTACK_STATUS        "%s is under attack!"
#define DEATH_STATUS         "%s has died!"

} // end of namespace Saga2

#endif
