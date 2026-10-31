// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The piano roll in the v0.8.1 look (FD-48, board 6): flat and opaque like the timeline's clips, so the notes, the keys and
// the grid stop reading as noise. The decisions live here as small pure functions so they can be pinned by a test:
//
//   notes   one flat colour, the unit's; lighter when selected, hovered or sounding. No alpha, no shadow, no gradient.
//   keys    ivory naturals and near-black accidentals, every C named
//   rows    in-scale rows lifted and out-of-scale rows recessed (a chromatic roll recesses the accidentals instead);
//           a stronger line on every C
//
// Colours come from the unit (Arsenal colour); the rest are fixed here because they describe a physical keyboard.

#include "../Common/MusicHelpers.h"
#include "../Core/NUITypes.h"

namespace AestraUI {
namespace PianoRollLook {

// ---- Keys ----
inline NUIColor ivoryKey() { return NUIColor(0.91f, 0.89f, 0.84f, 1.0f); }
inline NUIColor ivoryKeyHover() { return NUIColor(0.97f, 0.95f, 0.90f, 1.0f); }
inline NUIColor blackKey() { return NUIColor(0.075f, 0.075f, 0.09f, 1.0f); }
inline NUIColor blackKeyHover() { return NUIColor(0.20f, 0.20f, 0.24f, 1.0f); }
inline NUIColor keyEdge() { return NUIColor(0.0f, 0.0f, 0.0f, 0.35f); }
/// The label on an ivory key: dark, so every C reads.
inline NUIColor ivoryKeyLabel() { return NUIColor(0.20f, 0.19f, 0.18f, 1.0f); }

// ---- Rows ----
enum class RowShade {
    Plain,       ///< nothing added: the bed shows through
    Lifted,      ///< a row of the scale, lifted off the bed
    Recessed,    ///< a row outside the scale, or an accidental on a chromatic roll
};

inline bool isAccidental(int pitch) {
    switch (((pitch % 12) + 12) % 12) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

/// How a row of the grid is shaded. With a scale set, membership decides (in-scale lifted, out-of-scale recessed);
/// chromatic means no scale, so the accidentals recess the way a keyboard's black keys do.
inline RowShade rowShade(int pitch, int rootKey, ScaleType scale) {
    if (scale == ScaleType::Chromatic) return isAccidental(pitch) ? RowShade::Recessed : RowShade::Plain;
    return MusicTheory::isNoteInScale(pitch, rootKey, scale) ? RowShade::Lifted : RowShade::Recessed;
}

inline NUIColor rowShadeFill(RowShade shade) {
    switch (shade) {
    case RowShade::Lifted: return NUIColor(1.0f, 1.0f, 1.0f, 0.032f);
    case RowShade::Recessed: return NUIColor(0.0f, 0.0f, 0.0f, 0.24f);
    case RowShade::Plain: break;
    }
    return NUIColor(0.0f, 0.0f, 0.0f, 0.0f);
}

/// The line under a row: every C is stronger than the rest, so the octaves can be counted at a glance.
inline float rowLineAlpha(int pitch) { return pitch % 12 == 0 ? 0.13f : 0.035f; }

// ---- Notes ----
struct NoteState {
    bool selected = false;
    bool hovered = false;
    bool sounding = false;
};

/// The note's one flat colour: the unit's, lifted by state. Always fully opaque.
inline NUIColor noteFill(const NUIColor& unit, const NoteState& state) {
    NUIColor c = unit;
    c.a = 1.0f;
    if (state.sounding) return c.lightened(0.34f);
    if (state.selected) return c.lightened(0.20f);
    if (state.hovered) return c.lightened(0.09f);
    return c;
}

/// Black or white text on a note, whichever reads against its fill.
inline NUIColor noteLabelColor(const NUIColor& fill) {
    return fill.luminance() > 0.55f ? NUIColor(0.06f, 0.06f, 0.08f, 0.92f) : NUIColor(1.0f, 1.0f, 1.0f, 0.92f);
}

} // namespace PianoRollLook
} // namespace AestraUI
