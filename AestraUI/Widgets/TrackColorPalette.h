#pragma once

#include "../Core/NUITypes.h"

#include <algorithm>
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

/**
 * @brief Pull a raw palette hue back to the timeline's restrained tone.
 *
 * TRACK_PALETTE stores identity hues at full strength. Nothing should paint
 * them raw: the timeline deliberately tones lane and clip colour down so the
 * musical content stays the brightest thing on screen. Any surface that shows
 * the *same* lane identity has to apply the same restraint, or one view reads
 * as a different colour system from the next.
 *
 * @param brightnessScale Multiplies final luma (below 1 darkens).
 * @param saturationScale Pulls channels toward luma (below 1 desaturates).
 * @param alpha Output alpha; negative keeps the input's.
 */
inline NUIColor restrainDawColor(const NUIColor& color, float brightnessScale, float saturationScale, float alpha) {
    const float luma = (0.2126f * color.r) + (0.7152f * color.g) + (0.0722f * color.b);
    const float tonedR = ((color.r - luma) * saturationScale + luma) * brightnessScale;
    const float tonedG = ((color.g - luma) * saturationScale + luma) * brightnessScale;
    const float tonedB = ((color.b - luma) * saturationScale + luma) * brightnessScale;
    return NUIColor(std::clamp(tonedR, 0.0f, 1.0f), std::clamp(tonedG, 0.0f, 1.0f), std::clamp(tonedB, 0.0f, 1.0f),
                    alpha >= 0.0f ? alpha : color.a);
}

/**
 * @brief Restraint applied to a lane-identity stripe.
 *
 * Shared by the timeline's lane strips and the minimap's per-lane lines so a
 * lane reads as one colour in both.
 */
inline NUIColor restrainLaneIdentityColor(const NUIColor& color, float alpha) {
    return restrainDawColor(color, 0.84f, 0.62f, alpha);
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
