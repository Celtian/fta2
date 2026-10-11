// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/screenshots.h"
#include <iostream>

using namespace Native;
static void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    const Path root = std::filesystem::temp_directory_path() /
                      ("fta2-screenshots-" + std::to_string(SDL_GetPerformanceCounter()));
    try {
        std::filesystem::create_directories(root / "data");
        check(screenshotRoot(root / "package/bin/") == root / "package", "Package root");
        check(screenshotRoot(root / "build/") == root / "build", "Development root");
        const byte pixels[] = {10, 20, 30, 255, 40, 50, 60, 255};
        const Path application = root / std::filesystem::path(u8"game ž č");
        auto first = saveScreenshot(pixels, 2, 1, application, root / "user", root / "data", "same");
        auto second = saveScreenshot(pixels, 2, 1, application, root / "user", root / "data", "same");
        check(!first.path.empty() && !second.path.empty() && first.path != second.path &&
                  first.path.parent_path() == application / "screenshots", "Unique Unicode paths");
        auto* image = SDL_LoadPNG(first.path.toString().c_str());
        check(image && image->w == 2 && image->h == 1, "PNG dimensions");
        auto* rgba = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
        check(rgba && !memcmp(rgba->pixels, pixels, sizeof(pixels)), "Lossless PNG pixels");
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(image);
        std::ofstream(root / "blocked").put('x');
        auto fallback = saveScreenshot(pixels, 2, 1, root / "blocked", root / "user", root / "data");
        check(!fallback.path.empty() && fallback.usedUserDirectory, "Write fallback");
        auto protectedData = saveScreenshot(pixels, 2, 1, root / "data", root / "user", root / "data");
        check(!protectedData.path.empty() && protectedData.usedUserDirectory &&
                  !std::filesystem::exists(root / "data/screenshots"), "Read-only input protection");
        auto failure = saveScreenshot(pixels, 2, 1, root / "blocked", root / "blocked", root / "data");
        check(failure.path.empty() && !failure.error.empty(), "Both paths fail safely");
        {
            Platform platform;
            platform.screenshotBaseDirectory = application;
            platform.saves.directory = root / "user";
            dataDirectory = root / "data";
            platform.initGraphics(640, 480);
            const byte palette[] = {12, 34, 56, 78, 90, 123};
            platform.getPaletteManager()->setPalette(palette, 0, 2);
            platform.fillScreen(Rect(0, 0, 640, 480), 0);
            CursorMan.visible = true;
            CursorMan.w = 2;
            CursorMan.h = 1;
            CursorMan.hotX = CursorMan.hotY = 0;
            CursorMan.key = 255;
            CursorMan.pixels = {1, 255};
            SDL_Event motion{};
            motion.type = SDL_EVENT_MOUSE_MOTION;
            motion.motion.x = motion.motion.y = 100;
            check(SDL_PushEvent(&motion), "Inject cursor position");
            Event event;
            while (platform.pollEvent(event)) {}
            const Point mouse = platform.mousePosition();
            check(mouse.x >= 0 && mouse.x + 1 < 640 && mouse.y >= 0 && mouse.y < 480,
                  "Cursor test position is outside the image");
            platform.updateScreen();
            // Mutating the framebuffer without presenting must not change the screenshot.
            platform.fillScreen(Rect(0, 0, 640, 480), 1);
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = SDLK_PRINTSCREEN;
            key.key.down = true;
            check(SDL_PushEvent(&key), "Inject Print Screen");
            bool saved = false;
            while (platform.pollEvent(event)) {
                check(event.type != EVENT_KEYDOWN, "Screenshot leaked into game controls");
                saved |= event.type == EVENT_SCREENSHOT && event.screenshotSaved;
            }
            check(saved, "Screenshot hotkey result");
            size_t count = 0;
            Path captured;
            for (const auto& entry : std::filesystem::directory_iterator(application / "screenshots")) {
                ++count;
                if (entry.path() != first.path && entry.path() != second.path)
                    captured = entry.path();
            }
            check(count == 3, "One screenshot per press");
            image = SDL_LoadPNG(captured.toString().c_str());
            check(image && image->w == 640 && image->h == 480, "Classic native resolution");
            rgba = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
            check(rgba, "Convert screenshot pixels");
            for (int y = 0; y < 480; ++y)
                for (int x = 0; x < 640; ++x) {
                    const auto* pixel = static_cast<byte*>(rgba->pixels) + y * rgba->pitch + x * 4;
                    const auto* expected = palette + (x == mouse.x && y == mouse.y ? 3 : 0);
                    check(!memcmp(pixel, expected, 3) && pixel[3] == 255,
                          "Last presented frame, palette or cursor transparency differs");
                }
            SDL_DestroySurface(rgba);
            SDL_DestroySurface(image);
            key.key.repeat = true;
            check(SDL_PushEvent(&key), "Inject repeat");
            while (platform.pollEvent(event))
                check(event.type != EVENT_SCREENSHOT && event.type != EVENT_KEYDOWN, "Repeat was not consumed");
            check(std::distance(std::filesystem::directory_iterator(application / "screenshots"),
                                std::filesystem::directory_iterator()) == 3, "Repeat created screenshot");
            check(platform.resizeGraphics(854, 480), "Wide framebuffer");
            platform.updateScreen();
            key.key.repeat = false;
            check(SDL_PushEvent(&key), "Inject wide screenshot");
            while (platform.pollEvent(event)) {}
            for (const auto& entry : std::filesystem::directory_iterator(application / "screenshots")) {
                if (entry.path() == first.path || entry.path() == second.path || entry.path() == captured)
                    continue;
                image = SDL_LoadPNG(Path(entry.path()).toString().c_str());
                check(image && image->w == 854 && image->h == 480, "Wide native resolution");
                SDL_DestroySurface(image);
            }
        }
        std::filesystem::remove_all(root);
        std::cout << "Screenshot checks passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::filesystem::remove_all(root);
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
