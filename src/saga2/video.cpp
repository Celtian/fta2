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

#include "platform/runtime.h"

#include "saga2/saga2.h"

namespace Saga2 {

#define VIDEO_EXT ".SMK"

// This DOS data profile contains no movies. All callers may continue immediately.
void Saga2Engine::startVideo(const char *, int, int) {}
bool Saga2Engine::checkVideo() { return false; }
void Saga2Engine::abortVideo() {}
void Saga2Engine::endVideo() {}
} // namespace Saga2
