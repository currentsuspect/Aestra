// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The Arsenal's colour decisions in one headless place (Ink theme, variant B, 21 Nov 2026). UnitRow.cpp is pinned at
// its size baseline, so what a row, a pad and a scene tile are painted with lives here: theme + state in, opaque
// colours out. Nothing here draws, so UnitRowLookTest checks every label and part against the contrast floors for
// every theme.
//
// The rule: violet (the theme's primary) means "selected" and "the playhead", nothing else. A selected row is a solid
// lit primary with textOnPrimary on top of it. Pads are neutral: white when active, the playhead fills one with the
// primary. The unit's own colour stays in its strip and its route dot, and the unit palette avoids the violet hues.

#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>

namespace AestraUI::UnitRowLook {

struct RowState {
    bool selected = false;
    bool hovered = false;
    bool dropTarget = false;
    bool enabled = true;
};

/// What a unit row is painted with. Every colour is opaque except where it is documented as quiet.
struct Row {
    bool lit = false;                ///< selected: the whole card is the lit primary
    NUIColor fill;                   ///< the card
    NUIColor edge;                   ///< outline; alpha 0 means none
    float edgeWidth = 1.0f;
    NUIColor name;                   ///< unit name
    NUIColor kind;                   ///< the type line ("Sampler") and a role the user chose
    NUIColor kindGuess;              ///< a role read from the name: a step dimmer, still 4.5:1
    NUIColor grip;                   ///< the drag handle. Quiet at rest by design (it wakes on hover or selection)
    NUIColor dot;                    ///< the dot beside the name
    NUIColor keyFill;                ///< an M or S key that is off
    NUIColor keyLabel;               ///< its letter
    NUIColor stripRing;              ///< a ring around the colour strip, only on a lit row
    float stripRingWidth = 0.0f;
};

inline Row row(const NUIThemeProperties& t, const RowState& s, const NUIColor& identity) {
    Row r;
    r.lit = s.selected && !s.dropTarget;
    r.fill = t.backgroundSecondary;
    r.edge = NUIColor(0.0f, 0.0f, 0.0f, 0.0f);
    r.name = t.textPrimary;
    r.kind = t.textSecondary;
    r.kindGuess = (s.hovered || s.dropTarget) ? t.textSecondary : t.textMuted;
    r.grip = t.textSecondary.withAlpha(s.hovered ? 0.70f : 0.28f);
    r.dot = s.enabled ? identity : t.textDisabled.withAlpha(0.45f);
    r.keyFill = t.surfaceTertiary;
    r.keyLabel = t.textSecondary;
    if (s.dropTarget) {
        r.fill = t.selectionTint;
        r.edge = t.selectionEdge;
        r.edgeWidth = 2.0f;
    } else if (s.selected) {
        r.fill = t.primary;
        r.edge = t.primary;
        r.name = r.kind = r.kindGuess = r.grip = t.textOnPrimary;
        r.dot = s.enabled ? t.textOnPrimary : t.textOnPrimary.withAlpha(0.45f); // a disabled unit still reads off on the lit card
        r.keyFill = t.backgroundPrimary; // an off key is a dark chip on the lit card
        r.stripRing = t.textOnPrimary;
        r.stripRingWidth = 2.0f;
    } else if (s.hovered) {
        r.fill = t.surfaceRaised;
        r.edge = t.borderStrong;
    }
    return r;
}

/// The step pads. Neutral: the unit's colour is not spent here, so violet is the only thing that moves.
struct Pads {
    NUIColor restEven;     ///< beats 1 and 3
    NUIColor restOdd;      ///< beats 2 and 4
    NUIColor active;       ///< a note
    NUIColor playhead;     ///< the playhead's 1.5 px stroke
    NUIColor playheadFill; ///< an active pad under the playhead
};

namespace detail {
inline float lin(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float lum(const NUIColor& c) { return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b); }
inline float ratio(const NUIColor& a, const NUIColor& b) {
    const float x = lum(a), y = lum(b);
    return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}
} // namespace detail

inline Pads pads(const NUIThemeProperties& t) {
    // The playhead stroke is the hover accent; a theme whose hover accent is too close to its rest pads falls back to
    // the primary, then to the text colour, so the stroke always reads at 3:1 on both shades.
    NUIColor stroke = t.primaryHover;
    for (const NUIColor& c : {t.primaryHover, t.primary, t.textPrimary}) {
        stroke = c;
        if (detail::ratio(c, t.surfaceTertiary) >= 3.0f && detail::ratio(c, t.surfaceRaised) >= 3.0f) break;
    }
    // The fill under the playhead must read 2.5:1 on both rest pads. A deep-violet primary (Ink, option C) is 2.4:1 on
    // the control grey, so it falls back to the text colour rather than the floor being lowered.
    NUIColor fill = t.primary;
    for (const NUIColor& c : {t.primary, t.textPrimary}) {
        fill = c;
        if (detail::ratio(c, t.surfaceTertiary) >= 2.5f && detail::ratio(c, t.surfaceRaised) >= 2.5f) break;
    }
    return {t.surfaceTertiary, t.surfaceRaised, t.textPrimary, stroke, fill};
}

/// The part of an active pad above its velocity bar: opaque, so it never depends on what is behind the pad.
inline NUIColor velocityBase(const Pads& p, const NUIColor& rest) { return NUIColor::lerp(rest, p.active, 0.40f); }

/// A scene card in the strip above the rack.
struct SceneTile {
    NUIColor fill;
    NUIColor edge;
    NUIColor label;
    NUIColor thumbOn;  ///< a busy sixteenth in the thumbnail
    NUIColor thumbOff; ///< an empty one (decoration, not a part)
};

inline SceneTile sceneTile(const NUIThemeProperties& t, bool selected, bool hovered) {
    if (selected)
        return {t.primary, t.primary, t.textOnPrimary, t.textOnPrimary, NUIColor::lerp(t.primary, t.textOnPrimary, 0.35f)};
    return {hovered ? t.surfaceRaised : t.backgroundSecondary, hovered ? t.border : t.borderSubtle, t.textPrimary,
            t.textSecondary, t.textDisabled};
}

} // namespace AestraUI::UnitRowLook
