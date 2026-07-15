// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MusicHelpers.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace AestraUI {

/**
 * Snap a beat position to the nearest @p snapDur grid line, clamped to >= 0.
 * A non-positive grid returns the position unchanged (nothing to quantize to).
 */
inline double quantizeBeatToGrid(double beat, double snapDur) {
    if (snapDur <= 0.0001) return beat;
    return std::max(0.0, std::round(beat / snapDur) * snapDur);
}

/**
 * Compute the elongated end for a note being "connected" (legato) to the next
 * note in time. The note's end extends forward to the nearest start greater than
 * its own; when nothing follows, it extends to the next @p snapDur boundary. The
 * result never precedes the current end — the operation only ever lengthens.
 *
 * @param start      Note start in beats.
 * @param end        Note end in beats (start + duration).
 * @param otherStarts Start beats of the other candidate notes.
 * @param snapDur    Grid used for the no-follower fallback (<=0 falls back to 1 beat).
 * @return The new end beat (>= end).
 */
inline double computeConnectedNoteEnd(double start, double end,
                                      const std::vector<double>& otherStarts,
                                      double snapDur) {
    double nextStart = std::numeric_limits<double>::max();
    for (double s : otherStarts) {
        if (s > start + 0.0001 && s < nextStart) nextStart = s;
    }
    if (nextStart < std::numeric_limits<double>::max()) {
        return nextStart > end ? nextStart : end; // fill the gap, but never shorten
    }
    if (snapDur <= 0.0001) snapDur = 1.0;
    return std::ceil((end + 0.0001) / snapDur) * snapDur; // out to the next grid line
}

/**
 * Find the topmost non-deleted note at a given local coordinate.
 *
 * @param notes The note list to search (searched in reverse order for topmost)
 * @param localX X coordinate in note-space (not screen-space)
 * @param localY Y coordinate in note-space (not screen-space)
 * @param pixelsPerBeat Horizontal scale for coordinate conversion
 * @param keyHeight Vertical scale for coordinate conversion
 * @return Index of the found note, or -1 if none found
 */
int findNoteAtLocal(const std::vector<MidiNote>& notes,
                    float localX, float localY,
                    float pixelsPerBeat, float keyHeight);

/**
 * Check if a note is inside a marquee selection rectangle.
 *
 * @param note The note to check
 * @param boxX Box left edge in local coordinates
 * @param boxY Box top edge in local coordinates
 * @param boxW Box width (may be negative)
 * @param boxH Box height (may be negative)
 * @param pixelsPerBeat Horizontal scale
 * @param keyHeight Vertical scale
 * @param scrollX Horizontal scroll offset
 * @param scrollY Vertical scroll offset
 * @return true if note intersects the box
 */
bool isNoteInSelectionBox(const MidiNote& note,
                          float boxX, float boxY, float boxW, float boxH,
                          float pixelsPerBeat, float keyHeight,
                          float scrollX = 0.0f, float scrollY = 0.0f);

/**
 * Convert a pointer Y coordinate in the velocity lane to the normalized
 * velocity representation used by MidiNote (0.0f to 1.0f).
 */
float velocityFromPanelPosition(float pointerY, float bottomY, float availableHeight);

/**
 * Convert a normalized MidiNote velocity to a rendered lane height.
 */
float velocityToPanelHeight(float velocity, float availableHeight);

/**
 * Check if a note should be considered for interaction (not deleted).
 */
inline bool isNoteActive(const MidiNote& note) {
    return !note.isDeleted;
}

} // namespace AestraUI
