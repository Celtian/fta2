// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"
namespace Native {
extern std::function<void()> gameFrameHook;
void configureSmokeProbe(const Path& output, int frames = 260);
void configureVoiceProbe(const Path& output);
void configureDeathProbe(const Path& output, uint16 actor);
void configureWidescreenProbe(const Path& output);
void configureLocalizationProbe(const Path& output);
void verifyContainerOwners(const Path& output);
} // namespace Native
