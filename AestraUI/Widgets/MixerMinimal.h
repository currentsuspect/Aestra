// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The mixer's colour rule (owner, 22 Nov: "as minimal as possible without losing meaning"): colour only where it
// names a track or carries state. Tints are low and desaturated, send markers are neutral (the name is beside
// them), the group band is neutral with one thin line in the bus colour. State colours (mute/solo/arm, clip, meter
// zones, selection, warnings) are never touched by these.

#include "NUIRenderer.h"

namespace AestraUI {
namespace MixerMinimal {

inline constexpr float kTintDesaturate = 0.5f;   // how much of a track colour is taken out of tints and plates
inline constexpr float kBandFillAlpha = 0.07f;   // the group band's neutral fill
inline constexpr float kBandLineAlpha = 0.7f;    // its 1px top line, in the bus colour
inline constexpr float kBandMasterLineAlpha = 0.35f;

/// @p c with @p amount (0..1) of its colour taken out toward its own grey.
inline NUIColor desaturate(const NUIColor& c, float amount) {
    const float grey = c.r * 0.299f + c.g * 0.587f + c.b * 0.114f;
    return NUIColor(c.r + (grey - c.r) * amount, c.g + (grey - c.g) * amount, c.b + (grey - c.b) * amount, c.a);
}

} // namespace MixerMinimal
} // namespace AestraUI
