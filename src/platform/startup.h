// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"
namespace Native {
Path discoverGameDirectory(const Path& remembered, const Path& working, const Path& executable);
#ifdef _WIN32
Path chooseGameDirectory();
#endif
} // namespace Native
