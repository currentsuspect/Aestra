#pragma once

#include <cstdint>
#include <climits>

namespace AestraUI {

static constexpr uint32_t TRACK_PALETTE[] = {
    0xFF00C9A7, // 0 — Aestra Teal
    0xFF7B6FD4, // 1 — Soft Purple
    0xFFF0A500, // 2 — Amber
    0xFFFF5757, // 3 — Coral
    0xFF4FB3FF, // 4 — Sky Blue
    0xFFA3D977, // 5 — Sage Green
    0xFFFF7AC6, // 6 — Pink
    0xFF5C7CFA, // 7 — Indigo
};
static constexpr int PALETTE_SIZE = 8;

static constexpr const char* PALETTE_NAMES[] = {
    "Teal",
    "Purple",
    "Amber",
    "Coral",
    "Sky Blue",
    "Sage",
    "Pink",
    "Indigo",
};

inline uint32_t paletteIndexToARGB(int index) {
    if (index < 0 || index >= PALETTE_SIZE) return 0xFF808080;
    return TRACK_PALETTE[index];
}

inline int nearestPaletteIndex(uint32_t argb) {
    if (argb == 0) return -1;
    int best = 0;
    int bestDist = INT_MAX;
    auto parseARGB = [](uint32_t c, int& r, int& g, int& b) {
        r = (c >> 16) & 0xFF;
        g = (c >> 8) & 0xFF;
        b = c & 0xFF;
    };
    int r0, g0, b0;
    parseARGB(argb, r0, g0, b0);
    for (int i = 0; i < PALETTE_SIZE; ++i) {
        int r, g, b;
        parseARGB(TRACK_PALETTE[i], r, g, b);
        int dr = r - r0, dg = g - g0, db = b - b0;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return (bestDist < 5000) ? best : -1;
}

} // namespace AestraUI
