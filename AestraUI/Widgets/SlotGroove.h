// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cmath>

namespace AestraUI {
namespace SlotGroove {

/**
 * @brief The fader slot's grooved look (Design Spec, Mixer, board p-mixer2 locked, variant F).
 *
 * A 6px slot with a 1px radius. Its face is meterBackground in repeating 4px bands: 3px of the plain colour, then
 * a 1px line lighter by 4% toward white, from the top down; an inset shadow (1 1 2 neuLo) sits on top. Pure
 * functions: the fader walks the numbers they give.
 */
inline constexpr float kSlotW = 6.0f;
inline constexpr float kSlotRadius = 1.0f;
inline constexpr float kBandH = 4.0f;       // 3px plain + 1px lighter
inline constexpr float kPlainH = 3.0f;
inline constexpr float kLineLift = 0.04f;   // the lighter line: meterBackground mixed 4% toward white
inline constexpr float kShadowOffset = 1.0f; // inset 1px 1px 2px neuLo
inline constexpr float kShadowBlur = 2.0f;

/// How many whole lighter lines fit a slot @p slotHeightPx tall (the board's gradient is clipped by the slot, so a
/// line cut off by the bottom edge is not drawn).
inline int lineCount(float slotHeightPx) {
    if (!(slotHeightPx >= kBandH))
        return 0;
    return static_cast<int>(std::floor(slotHeightPx / kBandH));
}

/// Top edge of lighter line @p index, measured from the slot's top: 3, 7, 11, ...
inline float lineTop(int index) { return kPlainH + kBandH * static_cast<float>(index); }

/// One colour channel mixed @p lift of the way toward white.
inline float liftChannel(float c, float lift = kLineLift) { return c + (1.0f - c) * lift; }

} // namespace SlotGroove
} // namespace AestraUI
