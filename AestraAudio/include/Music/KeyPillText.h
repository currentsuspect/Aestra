// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-KEY3 (FD-98): the words on the transport bar's key pill. Pure, so the wording is tested without a window.
#pragma once

#include "Music/ProjectHarmony.h"

#include <string>

namespace Aestra {
namespace Audio {

/** "C", "C#", "D"... for a pitch class (any int; wrapped). Plain ASCII: the UI font has no sharp sign. */
inline const char* tonicName(int tonic) noexcept {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[((tonic % 12) + 12) % 12];
}

/** "A min", "C maj", or "No key" while the project has none: the key as it is HEARD. */
inline std::string keyName(const ProjectHarmony& h) {
    if (!h.hasKey())
        return "No key";
    return std::string(tonicName(h.soundingTonic())) + (h.minor ? " min" : " maj");
}

/** "+3", "-2", or "" at zero: how far the song is moved from how it was written. */
inline std::string transposeText(const ProjectHarmony& h) {
    if (h.transposeSemitones == 0)
        return "";
    return (h.transposeSemitones > 0 ? "+" : "-") + std::to_string(h.transposeSemitones < 0 ? -h.transposeSemitones : h.transposeSemitones);
}

} // namespace Audio
} // namespace Aestra
