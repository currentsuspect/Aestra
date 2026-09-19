#pragma once

#include "../Core/NUITypes.h"

#include <algorithm>
#include <cmath>
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

// ---------------------------------------------------------------------------
// Clip colour contract
//
// A clip is a flat, opaque surface in its identity hue — the same hue the mixer
// strips paint — with its content (waveform, notes, label) drawn as a LIGHT ink
// on top. Owner direction 2026-09-19 (spec 2 §1): foreground legibility is
// separated from routing identity. Routing colour still decides the hue; it no
// longer decides whether the foreground comes out light or dark.
//
// The previous contract derived ink by deepening the hue toward black and picked
// label colour per hue by luminance. That made a routed clip's foreground flip
// between near-black and near-white depending on which channel it fed, and on
// the dark hues (purple, indigo) the near-black ink sat on a near-black body.
//
// The rule now has two halves that only work together:
//
//   1. Every body is capped to one luminance ceiling. Hue and saturation are
//      untouched (one scalar multiplies all three channels), so a clip still
//      reads as its channel's colour — it is simply never bright enough to
//      swallow a light foreground.
//   2. Every foreground is lifted toward white from the surface it sits on, so
//      label and waveform belong to one family instead of being independently
//      chosen colours.
//
// Body and ink live here together because what matters is the *gap* between them
// and its direction; separate the two derivations and a later edit can quietly
// collapse or invert it. TimelineClipContrastTest guards both.
// ---------------------------------------------------------------------------

/** @brief WCAG relative luminance — the measure both halves of the contract use. */
inline float clipRelativeLuminance(const NUIColor& color) {
    const auto channel = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return (v <= 0.03928f) ? (v / 12.92f) : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel(color.r) + 0.7152f * channel(color.g) + 0.0722f * channel(color.b);
}

/**
 * @brief The luminance every clip body is pulled down to.
 *
 * Contrast of near-white against a surface at luminance L is (1.05)/(L + 0.05),
 * which crosses 4.5:1 at L = 0.183 — and that is for *pure* white, before the
 * selection lift brightens the body. So the ceiling is not a taste value with
 * slack in it: above roughly 0.15 no foreground colour whatsoever can carry a
 * label at AA on a selected clip, and the rule silently stops working. 0.14
 * leaves the margin the lift consumes while keeping the body ~3.8:1 against the
 * timeline's black canvas, so clips still read as clips.
 *
 * TimelineClipContrastTest measures both ends rather than restating this number.
 */
inline constexpr float kClipBodyLuminanceCeiling = 0.14f;

/** @brief How much brighter a selected body is than an unselected one. */
inline constexpr float kClipSelectionLift = 1.06f;

/**
 * @brief Darken a hue until it is dark enough to carry a light foreground.
 *
 * One scalar scales all three channels, so hue angle and saturation — the parts
 * that carry routing identity — survive exactly; only brightness moves. Solved
 * by bisection because the sRGB transfer curve has no single closed-form inverse
 * across its linear and power segments.
 */
inline NUIColor capClipLuminance(const NUIColor& color, float ceiling) {
    if (clipRelativeLuminance(color) <= ceiling) {
        return color;
    }
    const auto scaled = [&color](float k) {
        return NUIColor(color.r * k, color.g * k, color.b * k, color.a);
    };
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 14; ++i) {
        const float mid = (lo + hi) * 0.5f;
        if (clipRelativeLuminance(scaled(mid)) > ceiling) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return scaled(lo);
}

/** @brief The clip body: the identity hue, capped for legibility. Selection lifts it. */
inline NUIColor clipBodyTone(const NUIColor& identity, bool selected) {
    const NUIColor capped =
        capClipLuminance(NUIColor(identity.r, identity.g, identity.b, 1.0f), kClipBodyLuminanceCeiling);
    return selected ? restrainDawColor(capped, kClipSelectionLift, 1.0f, 1.0f) : capped;
}

/** @brief The tint clip content is lifted from — the body hue itself. */
inline NUIColor waveformTintTone(const NUIColor& identity, bool selected) {
    return clipBodyTone(identity, selected);
}

/** @brief Lift a surface toward white. Sole authority for how light clip ink is. */
inline NUIColor liftClipInk(const NUIColor& surface, float amount) {
    return NUIColor::lerp(NUIColor(surface.r, surface.g, surface.b, 1.0f), NUIColor(1.0f, 1.0f, 1.0f, 1.0f),
                          std::clamp(amount, 0.0f, 1.0f));
}

/** @brief Lift used for waveform/note ink — keeps a whisper of the clip's hue. */
inline constexpr float kClipContentInkLift = 0.86f;

/** @brief Lift used for labels and glyphs — the brightest thing on a clip. */
inline constexpr float kClipLabelInkLift = 0.94f;

/** @brief The ink drawn over the body (waveform, notes). Must stay lighter than clipBodyTone(). */
inline NUIColor waveformInkTone(const NUIColor& identity, bool selected) {
    return liftClipInk(waveformTintTone(identity, selected), kClipContentInkLift);
}

/**
 * @brief The one foreground colour for clip text and glyphs.
 *
 * Derived from the surface it lands on rather than from the theme: a themed
 * textPrimary is near-black in the light UI mode, which is exactly the
 * unreadable-on-a-dark-clip case this contract exists to remove. Clip bodies do
 * not follow the UI theme, so their foreground must not either.
 */
inline NUIColor clipForegroundInk(const NUIColor& surface, float alpha) {
    return liftClipInk(surface, kClipLabelInkLift).withAlpha(alpha);
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
