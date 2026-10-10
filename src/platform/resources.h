// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"
namespace Native {
String sha256(const byte* data, size_t size);
void validateHrs(const byte* data, size_t size);
void prepareDosResources();
extern Path voiceArchive;
void prepareVoiceArchive(const Path& requested = {});
void requireSeparateOutput(const Path& data, const Path& output);
} // namespace Native
