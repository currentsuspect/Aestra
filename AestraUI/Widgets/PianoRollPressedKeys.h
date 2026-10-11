// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Which piano-roll keys show pressed (SPEC 3 §5.1), kept apart from the note layer so the
// rules are one small, testable unit: a single-pitch press releases the other keys, a chord
// press adds to them, and every change is reported once through `changed(pitch, down)`.

#include <algorithm>
#include <functional>
#include <vector>

namespace AestraUI {

inline void pressPianoRollKey(std::vector<int>& pressed, int pitch, bool addToPress,
                              const std::function<void(int, bool)>& changed) {
    if (!addToPress) {
        // One pitch at a time (a drag onto a new row): the previous key comes back up.
        for (const int held : pressed) {
            if (held != pitch && changed) changed(held, false);
        }
        pressed.erase(std::remove_if(pressed.begin(), pressed.end(), [pitch](int held) { return held != pitch; }),
                      pressed.end());
    }
    if (std::find(pressed.begin(), pressed.end(), pitch) == pressed.end()) {
        pressed.push_back(pitch);
        if (changed) changed(pitch, true);
    }
}

inline void releasePianoRollKeys(std::vector<int>& pressed, const std::function<void(int, bool)>& changed) {
    for (const int held : pressed) {
        if (changed) changed(held, false);
    }
    pressed.clear();
}

} // namespace AestraUI
