// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MusicHelpers.h"
#include <vector>

namespace AestraUI {

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
 * Check if a note should be considered for interaction (not deleted).
 */
inline bool isNoteActive(const MidiNote& note) {
    return !note.isDeleted;
}

} // namespace AestraUI