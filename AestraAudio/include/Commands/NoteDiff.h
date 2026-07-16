// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PatternSource.h"

#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Result of diffing two note states.
 *
 * Intent is expressed as discrete operations that can be mapped 1:1 to
 * ICommand subclasses. Each operation carries the full before/after note
 * state so callers can emit commands with correct disambiguation.
 */
struct NoteDiffResult {
    /** @brief Notes that were added (not present in before). */
    std::vector<MidiNote> added;
    /** @brief Notes that were removed (not present in after). */
    std::vector<MidiNote> removed;
    /** @brief Pairs of (before, after) for notes that changed position. */
    std::vector<std::pair<MidiNote, MidiNote>> moved;
    /** @brief Pairs of (before, after) for notes that changed duration. */
    std::vector<std::pair<MidiNote, MidiNote>> resized;
    /** @brief Pairs of (before, after) for in-place expression changes (velocity/pan). */
    std::vector<std::pair<MidiNote, MidiNote>> modified;

    bool empty() const {
        return added.empty() && removed.empty() && moved.empty() && resized.empty() &&
               modified.empty();
    }
};

/**
 * @brief Diff two note collections to produce a structured edit intent.
 *
 * The function uses a two-pass approach to disambiguate notes that share
 * position but differ in duration (the "same-position, different-duration"
 * ambiguity):
 *
 * Pass 1 — Full-field matching
 *   Notes that match on ALL stable fields (pitch, startBeat, durationBeats,
 *   velocity, unitId) are considered exact matches. These never cause
 *   ambiguity because they are uniquely identified by their full state.
 *
 * Pass 2 — Position-only matching for move/resize inference
 *   Remaining unmatched notes are grouped by (pitch, startBeat, unitId).
 *   Each group can have at most one before note and one after note.
 *   If a group has exactly one before and one after note:
 *     - Position changed → move
 *     - Duration changed → resize
 *   If a group has more than one before or more than one after note,
 *     the ambiguity is unresolved and both notes fall to remove+add.
 *
 * @param before  Notes before the edit (captured state).
 * @param after  Notes after the edit (current UI state).
 * @return NoteDiffResult describing all detected operations.
 */
NoteDiffResult diffNotes(const std::vector<MidiNote>& before,
                        const std::vector<MidiNote>& after);

} // namespace Audio
} // namespace Aestra
