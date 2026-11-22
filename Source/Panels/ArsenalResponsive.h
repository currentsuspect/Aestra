// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// How the Arsenal's header gives way as the window narrows (owner, 21 Nov 2026: at about 660 px the Swing label sat on the pattern "+"
// button, the bars / steps pills overlapped the ruler numbers, and the unit name truncated to "Sa..."). Pure functions of widths, so
// ArsenalResponsiveTest can walk every width from 600 to 1400 and assert no two controls overlap and every control stays 24 px or more.
//
// Priority, first to go to last: the "Swing" label, the divider, the Place button's long label (then "Place", then an arrow alone), the
// swing slider, and the Fit | Scroll toggle shrinks to its minimum hit sizes. The bars and steps pills drop their unit words and
// tighten their gaps; the unit row keeps a minimum name width and gives the lane the rest.

#include <algorithm>

namespace Aestra::ArsenalResponsive {

constexpr float kMinHit = 24.0f;       ///< every control keeps at least this much hit area
constexpr float kGap = 8.0f;
constexpr float kLabelW = 40.0f, kLabelGap = 6.0f, kTrackW = 77.0f, kDividerW = 1.0f;
constexpr float kPlaceFull = 156.0f, kPlaceShort = 84.0f, kPlaceIcon = 34.0f;
constexpr float kMinScenes = 96.0f;    ///< the scene cards keep at least this much room (they scroll)

enum class PlaceLabel { Full, Short, Icon };

/// The right end of the scene strip for a given strip width.
struct Right {
    float width = 0.0f;
    bool label = true, divider = true, swing = true;
    PlaceLabel place = PlaceLabel::Full;
    float placeWidth() const { return place == PlaceLabel::Full ? kPlaceFull : place == PlaceLabel::Short ? kPlaceShort : kPlaceIcon; }
};

/// @p reserved is what the strip keeps for the "+" slot and its gap (the caller's kAddW + kGap).
inline Right rightFor(float stripWidth, float reserved) {
    const float budget = stripWidth - reserved - kMinScenes;
    const auto tier = [](bool label, bool divider, bool swing, PlaceLabel place) {
        Right r;
        r.label = label; r.divider = divider; r.swing = swing; r.place = place;
        float w = r.placeWidth();
        if (swing) w += kTrackW + kGap + (label ? kLabelW + kLabelGap : 0.0f) + (divider ? kDividerW + kGap : 0.0f);
        r.width = w;
        return r;
    };
    const Right tiers[] = {tier(true, true, true, PlaceLabel::Full),   tier(false, true, true, PlaceLabel::Full),
                           tier(false, false, true, PlaceLabel::Full), tier(false, false, true, PlaceLabel::Short),
                           tier(false, false, false, PlaceLabel::Short), tier(false, false, false, PlaceLabel::Icon)};
    for (const Right& t : tiers)
        if (t.width <= budget) return t;
    return tiers[5];
}

/// Fit | Scroll: 108 wide with room; under 560 px of panel it keeps only the two minimum hit areas (30 and 46: the word "Scroll").
struct Fit { float width, fitHalf; };
inline Fit fitFor(float panelWidth) { return panelWidth >= 560.0f ? Fit{108.0f, 46.0f} : Fit{76.0f, 30.0f}; }

/// The scene strip's width in a header @p headerWidth wide: 16 px in, the strip, a 12 px gap, the Fit | Scroll toggle, 16 px out.
inline float sceneStripWidth(float headerWidth, float panelWidth) { return std::max(0.0f, headerWidth - 16.0f - 12.0f - fitFor(panelWidth).width - 16.0f); }

/// The bars and steps pills. Wide: "2 bars" / "32 steps" with the unit words. Narrow (the head under 262 px): bare numbers, tight gaps.
struct Steppers {
    bool words = true;
    float barsValueW = 56.0f, stepsValueW = 66.0f, gapInner = 4.0f, gapBetween = 8.0f;
};
inline Steppers steppersFor(float controlWidth) {
    Steppers s;
    if (controlWidth < 262.0f) {
        s.words = false;
        s.barsValueW = 28.0f;
        s.stepsValueW = 30.0f;
        s.gapInner = 2.0f;
        s.gapBetween = 6.0f;
    }
    return s;
}

/// The unit row's head (stripe, name, M S, open-lanes key): never narrower than this, so the name keeps 80 px (it showed "Sa..." at 54).
constexpr float kMinUnitName = 80.0f;

} // namespace Aestra::ArsenalResponsive
