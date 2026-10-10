// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"
namespace Native {
extern std::function<void()> gameFrameHook;
void configureSmokeProbe(const Path& output, int frames = 260);
void configureWidescreenProbe(const Path& output);
void verifyContainerOwners(const Path& output);
} // namespace Native
