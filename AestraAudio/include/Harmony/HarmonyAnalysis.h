// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-HS1 (FD-63): the harmony service. Detects the key and the chords of the music from MIDI notes.
//
// Pure data in, pure data out: no project, no UI, no audio-thread code. It is called on demand (a Muse verb, a
// future ruler refresh) on a non-real-time thread and produces a precomputed HarmonyTimeline. Reading the current
// chord for the transport bar is HarmonyTimeline::chordAt — a binary search that allocates nothing.
#pragma once

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Harmony {

/** One note as the analyser sees it, already placed at its arranged position. */
struct HarmonyNote {
    double startBeat = 0.0;
    double durationBeats = 0.0;
    int pitch = 60;         // MIDI note number
    float velocity = 0.8f;  // 0..1
};

enum class KeyMode { Major, Minor };

struct Key {
    int tonic = -1; // pitch class 0..11 (C = 0); -1 = no key (no notes)
    KeyMode mode = KeyMode::Major;
    double confidence = 0.0; // 0..1

    bool valid() const { return tonic >= 0; }
    std::string name() const; // "D minor", "" when not valid
};

enum class ChordQuality {
    Major, Minor, Diminished, Augmented, Sus2, Sus4,
    Dominant7, Major7, Minor7, HalfDiminished7, Diminished7,
    Major6, Minor6, Add9,
    Count
};

struct ChordSpan {
    double startBeat = 0.0;
    double endBeat = 0.0;
    int root = -1;  // pitch class; -1 = N.C. (no chord)
    ChordQuality quality = ChordQuality::Major;
    int bass = -1;  // pitch class of the bass when it is a chord tone but not the root (slash chord); else -1
    std::string name;  // "Dm7", "F/A", "N.C."
    std::string roman; // "i7", "III6"; empty without a key or for N.C.
    float confidence = 0.0f; // 0..1
    bool userSet = false;    // true when a user override produced this span

    bool isNoChord() const { return root < 0; }
};

/** A chord the user named on the ruler. It wins over detection on its span. */
struct ChordOverride {
    double startBeat = 0.0;
    double endBeat = 0.0;
    std::string chordName; // "Dm7", "F/A", "Bbmaj7"
};

struct HarmonyTimeline {
    Key key;
    std::vector<ChordSpan> chords; // sorted, non-overlapping

    /**
     * The chord sounding at a beat. O(log n), allocation-free, safe to call from a UI or transport tick.
     * Outside every span (before the first chord, after the last, or in a gap) it returns the N.C. span.
     */
    const ChordSpan& chordAt(double beat) const noexcept;
};

/** Nothing to analyse yet, or an unparseable name: the shared N.C. span. */
const ChordSpan& noChordSpan() noexcept;

/**
 * Detect the key and the chords.
 * @param notes        every note, placed on one beat axis (arrangement beats, or pattern beats)
 * @param beatsPerBar  bar length in beats (4 for 4/4); chord windows are half a bar
 * @param overrides    user-named chords; they replace detection over their span and set userSet
 */
HarmonyTimeline analyzeHarmony(const std::vector<HarmonyNote>& notes, double beatsPerBar,
                               const std::vector<ChordOverride>& overrides = {});

/** Key only: Krumhansl-Schmuckler profiles over the duration-weighted pitch-class histogram. */
Key detectKey(const std::vector<HarmonyNote>& notes);

/** Parse a chord name such as "Dm7", "F/A", "Bbmaj7". False when it is not a chord. */
bool parseChordName(const std::string& text, int& root, ChordQuality& quality, int& bass);

/** Name a chord; flats are used for the pitch-class spelling when useFlats. */
std::string chordName(int root, ChordQuality quality, int bass, bool useFlats);

/** Roman numeral of a chord in a key ("i7", "VI", "bVII"); empty when the key is not valid or root < 0. */
std::string romanNumeral(const Key& key, int root, ChordQuality quality);

/** Whether the key is conventionally spelled with flats (F, Bb, Dm, Gm ...). */
bool keyUsesFlats(const Key& key);

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
