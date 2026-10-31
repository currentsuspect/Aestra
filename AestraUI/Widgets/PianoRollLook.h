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

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

/// The scale degree a key plays (FD-63): "1", "2", "♭3"… for a key in the scale, empty off it or with no scale set.
/// Degrees are named against the major scale, so D minor reads 1 2 ♭3 4 5 ♭6 ♭7. The tritone is ♭5 in the scales built
/// on it (Locrian, Blues) and ♯4 elsewhere (Lydian).
inline std::string scaleDegreeLabel(int pitch, int rootKey, ScaleType scale) {
    if (scale == ScaleType::Chromatic || !MusicTheory::isNoteInScale(pitch, rootKey, scale)) return {};
    static const char* const kNames[12] = {"1",  "\xE2\x99\xAD" "2", "2", "\xE2\x99\xAD" "3", "3", "4", "",
                                           "5",  "\xE2\x99\xAD" "6", "6", "\xE2\x99\xAD" "7", "7"};
    const int interval = (((pitch - rootKey) % 12) + 12) % 12;
    if (interval == 6)
        return (scale == ScaleType::Locrian || scale == ScaleType::Blues) ? "\xE2\x99\xAD" "5" : "\xE2\x99\xAF" "4";
    return kNames[interval];
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

// ---- Note expression (FD-63, board "A + C") ----
// FD-63 supersedes FD-48's "no shading by velocity": a note is the unit's colour mixed toward black by its velocity,
// still opaque, so a soft note reads quieter at a glance. A note that may not play (chance < 100%) is striped, and a
// ratchet is drawn as ticks where its hits split the note.

/// The unit's colour at a velocity: 45% of it at velocity 0, all of it at full velocity. Always opaque.
inline NUIColor velocityFill(const NUIColor& unit, float velocity) {
    const float k = 0.45f + 0.55f * std::clamp(velocity, 0.0f, 1.0f);
    return NUIColor(unit.r * k, unit.g * k, unit.b * k, 1.0f);
}

/// The dark stripe of a chance note: the fill at 45%, as dark as the softest note can be.
inline NUIColor chanceStripe(const NUIColor& fill) { return NUIColor(fill.r * 0.45f, fill.g * 0.45f, fill.b * 0.45f, 1.0f); }

struct Segment {
    NUIPoint a;
    NUIPoint b;
};

/// The dark stripes of a chance note: lines running up to the right (x + y = c), one every @p period px along x, each
/// already cut to the note so it can be drawn with plain lines (the renderer has one clip rect, which the grid holds).
inline std::vector<Segment> chanceStripes(const NUIRect& r, float period = 7.0f) {
    std::vector<Segment> out;
    if (r.width <= 0.0f || r.height <= 0.0f || period <= 0.0f) return out;
    const float x0 = r.x, x1 = r.right(), y0 = r.y, y1 = r.bottom();
    // Stripes are anchored to whole multiples of the period so they don't crawl when the note scrolls by a pixel.
    for (float c = std::floor((x0 + y0) / period) * period; c <= x1 + y1; c += period) {
        const float xa = std::max(x0, c - y1);
        const float xb = std::min(x1, c - y0);
        if (xb - xa < 0.5f) continue;
        out.push_back({NUIPoint(xa, c - xa), NUIPoint(xb, c - xb)});
    }
    return out;
}

/// Where a ratchet's hits split the note: x of each boundary between hits (none for a single hit).
inline std::vector<float> ratchetTicks(const NUIRect& r, int ratchet) {
    std::vector<float> xs;
    const int hits = std::clamp(ratchet, 1, 4);
    for (int k = 1; k < hits; ++k) xs.push_back(r.x + r.width * static_cast<float>(k) / static_cast<float>(hits));
    return xs;
}

/// The note's tag: its chance and ratchet when they are not the default ("60%", "×3", "60% ×3"), else empty.
inline std::string expressionTag(float chance, int ratchet) {
    std::string tag;
    if (chance < 0.995f) tag = std::to_string(static_cast<int>(std::lround(std::clamp(chance, 0.0f, 1.0f) * 100.0f))) + "%";
    if (ratchet > 1) {
        if (!tag.empty()) tag += " ";
        tag += "\xC3\x97" + std::to_string(std::clamp(ratchet, 1, 4)); // ×
    }
    return tag;
}

/// Black or white text on a note, whichever reads against its fill.
inline NUIColor noteLabelColor(const NUIColor& fill) {
    return fill.luminance() > 0.55f ? NUIColor(0.06f, 0.06f, 0.08f, 0.92f) : NUIColor(1.0f, 1.0f, 1.0f, 0.92f);
}

} // namespace PianoRollLook
} // namespace AestraUI
