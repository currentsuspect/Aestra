// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PianoRollInteraction.h"

namespace AestraUI {

int findNoteAtLocal(const std::vector<MidiNote>& notes,
                    float localX, float localY,
                    float pixelsPerBeat, float keyHeight) {
    // Search in reverse order so topmost note is returned
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
        const auto& n = notes[i];
        if (n.isDeleted) continue;

        float nx = static_cast<float>(n.startBeat * pixelsPerBeat);
        float ny = (127 - n.pitch) * keyHeight;
        float nw = static_cast<float>(n.durationBeats * pixelsPerBeat);
        float nh = keyHeight;

        if (localX >= nx && localX < nx + nw &&
            localY >= ny && localY < ny + nh) {
            return i;
        }
    }
    return -1;
}

bool isNoteInSelectionBox(const MidiNote& note,
                          float boxX, float boxY, float boxW, float boxH,
                          float pixelsPerBeat, float keyHeight,
                          float scrollX, float scrollY) {
    float nx = static_cast<float>(note.startBeat * pixelsPerBeat) - scrollX;
    float ny = (127 - note.pitch) * keyHeight - scrollY;
    float nw = static_cast<float>(note.durationBeats * pixelsPerBeat);
    float nh = keyHeight;

    // Normalize box to handle negative dimensions
    float normX = boxW < 0 ? boxX + boxW : boxX;
    float normY = boxH < 0 ? boxY + boxH : boxY;
    float normW = boxW < 0 ? -boxW : boxW;
    float normH = boxH < 0 ? -boxH : boxH;

    return !(nx + nw <= normX || nx >= normX + normW ||
             ny + nh <= normY || ny >= normY + normH);
}

} // namespace AestraUI