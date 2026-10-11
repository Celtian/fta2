// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "platform/runtime.h"

namespace Native {
struct ScreenshotResult {
    Path path;
    String error;
    bool usedUserDirectory = false;
};
Path screenshotRoot(Path executableDirectory);
ScreenshotResult saveScreenshot(const void* rgba, int width, int height, const Path& application,
                                const Path& user, const Path& data, String timestamp = {});
} // namespace Native
