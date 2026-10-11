// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/screenshots.h"
#include "platform/resources.h"
#include <chrono>
#include <ctime>

namespace Native {
Path screenshotRoot(Path executableDirectory) {
    if (executableDirectory.filename().empty())
        executableDirectory = executableDirectory.parent_path();
    if (executableDirectory.filename() == "bin")
        executableDirectory = executableDirectory.parent_path();
    return executableDirectory;
}

ScreenshotResult saveScreenshot(const void* rgba, int width, int height, const Path& application,
                                const Path& user, const Path& data, String timestamp) {
    ScreenshotResult result;
    if (!rgba || width <= 0 || height <= 0) {
        result.error = "No presented frame is available";
        return result;
    }
    if (timestamp.empty()) {
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::system_clock::to_time_t(now);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &seconds);
#else
        localtime_r(&seconds, &local);
#endif
        char date[32];
        std::strftime(date, sizeof(date), "%Y-%m-%d_%H-%M-%S", &local);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now.time_since_epoch()).count() % 1000;
        timestamp = String::format("%s-%03d", date, int(milliseconds));
    }
    auto* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                          const_cast<void*>(rgba), width * 4);
    if (!surface) {
        result.error = SDL_GetError();
        return result;
    }
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> owned(surface, SDL_DestroySurface);
    for (const auto& root : {application, user}) {
        result.usedUserDirectory = root == user;
        Path path;
        try {
            if (root.empty())
                throw std::runtime_error("Screenshot directory is unavailable");
            const Path directory = root / "screenshots";
            requireSeparateOutput(data, directory);
            std::filesystem::create_directories(directory);
            for (unsigned suffix = 0; suffix < 10000; ++suffix) {
                path = directory / ("fta2-" + timestamp +
                       (suffix ? String::format("-%u", suffix) : String()) + ".png");
                // SDL 3.4.18 maps x to exclusive creation on Windows as well as POSIX.
                auto* stream = SDL_IOFromFile(path.toString().c_str(), "wbx");
                if (!stream) {
                    if (std::filesystem::exists(path))
                        continue;
                    throw std::runtime_error(SDL_GetError());
                }
                const bool saved = SDL_SavePNG_IO(surface, stream, false);
                const String saveError = saved ? String() : String(SDL_GetError());
                const bool closed = SDL_CloseIO(stream);
                if (!saved || !closed) {
                    const String failure = saved ? String(SDL_GetError()) : saveError;
                    std::error_code ignored;
                    std::filesystem::remove(path, ignored);
                    throw std::runtime_error(failure);
                }
                result.path = path;
                result.error.clear();
                return result;
            }
            throw std::runtime_error("Screenshot filename collision limit reached");
        } catch (const std::exception& exception) {
            if (!result.error.empty())
                result.error += "; ";
            result.error += root.toString() + ": " + exception.what();
        }
    }
    return result;
}
} // namespace Native
