// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Internal helpers shared by the PianoRoll* widget translation units (split
// out of the former monolithic NUIPianoRollWidgets.cpp). Not part of the
// public widget API — include only from PianoRoll*.cpp files.

#include <algorithm>
#include <cmath>
#include <string>

namespace AestraUI {

/** @brief Check whether a MIDI pitch is a black key. */
inline bool isBlackKey(int midiPitch) {
    int m = midiPitch % 12;
    return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10);
}

/** @brief Format a MIDI pitch as a note name with octave (C3 = 60). */
inline std::string getNoteLabel(int midiPitch) {
    static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (midiPitch / 12) - 2; // C3=60 standard
    return std::string(noteNames[midiPitch % 12]) + std::to_string(octave);
}

/** @brief Clamp a scroll offset to [0, upper], treating non-finite input as 0. */
inline float safeClampScroll(float value, float upper) {
    if (!std::isfinite(value) || !std::isfinite(upper) || upper <= 0.0f) {
        return 0.0f;
    }
    if (value <= 0.0f) {
        return 0.0f;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

/** @brief Clamp to [lower, upper] with non-finite inputs collapsing to a safe lower bound. */
inline float safeClampRange(float value, float lower, float upper) {
    if (!std::isfinite(value) || !std::isfinite(lower) || !std::isfinite(upper)) {
        return std::max(0.0f, lower);
    }
    if (upper < lower) {
        upper = lower;
    }
    if (value <= lower) {
        return lower;
    }
    if (value >= upper) {
        return upper;
    }
    return value;
}

/** @brief Convert a beat position to a screen X coordinate. */
inline float beatToScreenX(double beat, float pixelsPerBeat, float scrollX, float originX) {
    return originX + static_cast<float>((beat * static_cast<double>(pixelsPerBeat)) - static_cast<double>(scrollX));
}

/** @brief Snap an X coordinate to the pixel center for crisp 1px vertical lines. */
inline float snapVerticalLineX(float x) {
    return std::floor(x) + 0.5f;
}

/** @brief Snap a rect edge X coordinate to the nearest whole pixel. */
inline float snapRectX(float x) {
    return std::round(x);
}

} // namespace AestraUI
