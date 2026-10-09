// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace Native {
enum class DisplayMode { Classic, Widescreen };

struct DisplayLayout {
    int screenWidth, screenHeight;
    int mapX, mapY, mapWidth, mapHeight;
    int sidebarOffset;

    static constexpr DisplayLayout forMode(DisplayMode mode) {
        const int extra = mode == DisplayMode::Widescreen ? 214 : 0;
        return {640 + extra, 480, 20, 20, 440 + extra, 420, extra};
    }
};

// Changed only at startup or by the game-thread display transition.
inline DisplayMode activeDisplayMode = DisplayMode::Classic;
inline constexpr DisplayLayout classicLayout = DisplayLayout::forMode(DisplayMode::Classic);
inline constexpr DisplayLayout widescreenLayout = DisplayLayout::forMode(DisplayMode::Widescreen);
inline const DisplayLayout& displayLayout() {
    return activeDisplayMode == DisplayMode::Widescreen ? widescreenLayout : classicLayout;
}
} // namespace Native
