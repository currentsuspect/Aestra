// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-KEY2 (FD-76): the project's key and its global transpose.
//
// The notes in a project stay as they were written. Global transpose is an offset applied when they are played,
// and the project key is a label on the written notes. The key the user sees and hears is the written key moved
// by the transpose, so "set the key to A" on a project written in C is a transpose of three semitones down.
// Pure data and pure functions: no project, no UI, no audio-thread code.
#pragma once

#include <algorithm>

namespace Aestra {
namespace Audio {

struct ProjectHarmony {
    static constexpr int kMinTranspose = -24;
    static constexpr int kMaxTranspose = 24;

    /** Pitch class (C = 0) of the key the notes were written in; -1 while the project has no key. */
    int writtenTonic = -1;
    bool minor = false;
    /** Semitones every key-following part is moved by on playback. The notes themselves are not edited. */
    int transposeSemitones = 0;

    bool hasKey() const noexcept { return writtenTonic >= 0; }
    /** The key as heard: the written key moved by the transpose. -1 while the project has no key. */
    int soundingTonic() const noexcept { return hasKey() ? ((writtenTonic + transposeSemitones) % 12 + 12) % 12 : -1; }
    bool operator==(const ProjectHarmony& o) const noexcept {
        return writtenTonic == o.writtenTonic && minor == o.minor && transposeSemitones == o.transposeSemitones;
    }
    bool operator!=(const ProjectHarmony& o) const noexcept { return !(*this == o); }
};

/**
 * The shortest way from one tonic to another, in semitones: -5..+6. C to A is 3 down, not 9 up, so a key change
 * never throws the music an octave away from where it was written. The tritone, the one tie, goes up.
 */
constexpr int shortestKeyRoute(int fromTonic, int toTonic) noexcept {
    const int up = ((toTonic - fromTonic) % 12 + 12) % 12; // 0..11
    return up > 6 ? up - 12 : up;
}

/** Any transpose brought into the allowed range (whole octaves are dropped first, so the pitch class is kept). */
constexpr int clampTranspose(int semitones) noexcept {
    while (semitones > ProjectHarmony::kMaxTranspose)
        semitones -= 12;
    while (semitones < ProjectHarmony::kMinTranspose)
        semitones += 12;
    return semitones;
}

/**
 * Turn the project to a key.
 *  - A project with no key yet takes the label: its notes are said to sound in that key now, nothing moves.
 *  - A project with a key moves by the shortest route from its WRITTEN key, so the result is never more than a
 *    tritone from the notes as written, however many times the key is changed.
 * Major or minor is only a label here; rewriting the notes for a new mode is a separate, user-confirmed edit.
 */
inline ProjectHarmony withSoundingKey(ProjectHarmony h, int tonic, bool minor) noexcept {
    tonic = ((tonic % 12) + 12) % 12;
    if (!h.hasKey()) {
        h.writtenTonic = ((tonic - h.transposeSemitones) % 12 + 12) % 12;
    } else {
        h.transposeSemitones = shortestKeyRoute(h.writtenTonic, tonic);
    }
    h.minor = minor;
    return h;
}

/** A MIDI note moved by a transpose. A result off the keyboard is folded back by octaves, keeping its pitch class. */
constexpr int transposedMidiNote(int note, int semitones) noexcept {
    int moved = note + semitones;
    while (moved > 127)
        moved -= 12;
    while (moved < 0)
        moved += 12;
    return moved;
}

/**
 * A written note rewritten for the other mode of the same tonic (C major <-> C minor): the third, sixth and
 * seventh of the scale move a semitone. Notes outside the scale are left alone.
 */
constexpr int noteInOtherMode(int note, int tonic, bool toMinor) noexcept {
    const int degree = ((note - tonic) % 12 + 12) % 12;
    if (toMinor) {
        if (degree == 4 || degree == 9 || degree == 11)
            return std::max(0, note - 1);
    } else {
        if (degree == 3 || degree == 8 || degree == 10)
            return std::min(127, note + 1);
    }
    return note;
}

} // namespace Audio
} // namespace Aestra
