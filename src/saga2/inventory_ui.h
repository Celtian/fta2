// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Saga2::InventoryUI {
// Presentation helpers: these never advance the calendar or modify inventory objects.
inline int scrollRows(int position, int visible, int total, float delta, float& remainder) {
    if (!std::isfinite(delta))
        return position;
    const double accumulated = double(remainder) + delta;
    const double whole = std::trunc(accumulated);
    remainder = float(accumulated - whole);
    const int limit = std::max(0, total - visible);
    return int(std::clamp(double(position) - whole, 0.0, double(limit)));
}

constexpr int slotNumber(int row, int column, int columns) { return row * columns + column + 1; }

inline std::array<char, 32> clockText(unsigned days, unsigned hour, unsigned frame,
                                      unsigned framesPerHour) {
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "Day %u %02u:%02u", days + 1, hour,
                  frame * 60 / framesPerHour);
    return text;
}
} // namespace Saga2::InventoryUI
