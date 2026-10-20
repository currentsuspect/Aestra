// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-PR1 (FD-52): duplicate in the piano roll the way FL Studio does it. Founder, 2026-10-18: "Highlighting the piano
// roll and commanding a duplication should act like FL, move the highlight next bar and duplicate the pattern within
// it to that next bar", and "that highlight from the ruler too ... that doesn't select notes or do what i wanted".
//
//  * Selection: Duplicate copies the selected notes one step on and moves the selection onto the copies, so pressing it
//    again keeps extending the phrase. The step is the selection's length rounded UP to whole bars (one bar for a
//    selection inside a bar), so the copy lands in the next bar-aligned block, in the same place within it.
//  * Ruler range: the highlight dragged on the ruler selects the notes inside it. Duplicate then copies that range
//    into the next block of the SAME length (not rounded: a 6-beat highlight copies into the next 6 beats) and moves
//    the highlight there. The highlight is still the loop zone, as in FL.
//
// Pure functions on the note list so the rules are pinned by a test without a window.

#include "../Common/MusicHelpers.h" // MidiNote (the editor's own note, with selected / isDeleted)

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace AestraUI {
namespace PianoRollDuplicate {

using Range = std::pair<double, double>; ///< [start, end) in beats

/// The selection's length rounded up to whole bars, never less than one bar.
inline double selectionStep(double selectionStart, double selectionEnd, double beatsPerBar) {
    const double bar = std::max(1.0, beatsPerBar);
    const double bars = std::ceil((std::max(0.0, selectionEnd - selectionStart)) / bar - 1e-9);
    return std::max(1.0, bars) * bar;
}

/// A note belongs to a ruler range when it STARTS inside it (a long note that began earlier is not cut or copied).
inline bool startsIn(const MidiNote& n, const Range& r) {
    return n.startBeat >= r.first - 1e-9 && n.startBeat < r.second - 1e-9;
}

struct Plan {
    bool rangeMode{false};
    double shift{0.0};            ///< how far every copy moves, in beats
    std::vector<size_t> sources;  ///< indices into the note list to copy
    Range from{0.0, 0.0};         ///< the block being copied (the range, or the selection's span)
    Range to{0.0, 0.0};           ///< the block the copies land in (where the highlight moves, in range mode)
};

/// What a Duplicate press should do, or nothing when there is nothing to copy.
///
/// While a ruler highlight exists, Duplicate duplicates the highlighted block, so long as no selected note lies outside
/// it (dragging the highlight leaves exactly its notes selected; the copies the previous press made are selected inside
/// the new highlight; and a note clicked inside the highlight is still "in the highlight"). Select something outside
/// it and the selection is what you mean, so that is copied instead. Esc clears the highlight to copy one note.
inline std::optional<Plan> plan(const std::vector<MidiNote>& notes, const std::optional<Range>& ruler,
                                double beatsPerBar) {
    std::vector<size_t> selected;
    for (size_t i = 0; i < notes.size(); ++i)
        if (!notes[i].isDeleted && notes[i].selected) selected.push_back(i);

    if (ruler && ruler->second > ruler->first + 1e-9) {
        std::vector<size_t> inRange;
        for (size_t i = 0; i < notes.size(); ++i)
            if (!notes[i].isDeleted && startsIn(notes[i], *ruler)) inRange.push_back(i);
        const bool selectionInside = std::all_of(selected.begin(), selected.end(), [&](size_t i) { return startsIn(notes[i], *ruler); });
        if (selectionInside) {
            Plan p;
            p.rangeMode = true;
            p.shift = ruler->second - ruler->first;
            p.sources = std::move(inRange);
            p.from = *ruler;
            p.to = {ruler->second, ruler->second + p.shift};
            return p;
        }
    }

    if (selected.empty()) return std::nullopt;
    double start = notes[selected.front()].startBeat, end = 0.0;
    for (const size_t i : selected) {
        start = std::min(start, notes[i].startBeat);
        end = std::max(end, notes[i].startBeat + notes[i].durationBeats);
    }
    Plan p;
    p.shift = selectionStep(start, end, beatsPerBar);
    p.sources = std::move(selected);
    p.from = {start, end};
    p.to = {start + p.shift, end + p.shift};
    return p;
}

/// The copies, moved by the plan's shift and selected (the originals are deselected by the caller).
inline std::vector<MidiNote> copies(const std::vector<MidiNote>& notes, const Plan& p) {
    std::vector<MidiNote> out;
    out.reserve(p.sources.size());
    for (const size_t i : p.sources) {
        MidiNote c = notes[i];
        c.startBeat += p.shift;
        c.selected = true;
        c.isDeleted = false;
        out.push_back(c);
    }
    return out;
}

/// Where the last copy ends, so the pattern can be grown to hold it.
inline double endOf(const std::vector<MidiNote>& made) {
    double end = 0.0;
    for (const auto& n : made) end = std::max(end, n.startBeat + n.durationBeats);
    return end;
}

/// Whole bars the pattern must grow by so @p endBeat fits (0 when it already does).
inline int barsToGrow(double endBeat, double patternLengthBeats, double beatsPerBar) {
    const double bar = std::max(1.0, beatsPerBar);
    return std::max(0, static_cast<int>(std::ceil((endBeat - patternLengthBeats) / bar - 1e-9)));
}

} // namespace PianoRollDuplicate
} // namespace AestraUI
