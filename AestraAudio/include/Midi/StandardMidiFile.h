// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Standard MIDI File (.mid) reader and writer. Pure data in, pure data out: no UI, no I/O, no
// engine state. Positions are quarter-note beats, so they are independent of the file's tempo.
//
// Reader: SMF format 0 and 1 (format 2 and SMPTE time division are rejected with a message).
// It never reads out of bounds; any malformed or truncated input returns ok == false.
// Writer: format 1, PPQ 480, track 0 = tempo map + time signatures. Beat positions on the
// 1/480 grid survive write -> read exactly.

#include "Models/PatternSource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Midi {

constexpr int kWriterPpq = 480;

struct SmfNote {
    MidiNote note;        // pitch, startBeat, durationBeats, velocity (0..1)
    uint8_t channel{0};   // 0..15
};

struct TempoChange {
    double beat{0.0};
    double bpm{120.0};
};

struct TimeSignatureChange {
    double beat{0.0};
    int numerator{4};
    int denominator{4};
};

struct SmfTrack {
    std::string name;
    std::vector<SmfNote> notes; // sorted by startBeat on read
};

struct SmfData {
    int format{1};
    std::vector<TempoChange> tempos;              // sorted by beat
    std::vector<TimeSignatureChange> timeSignatures;
    std::vector<SmfTrack> tracks;                 // tempo-only tracks come back with no notes
};

struct SmfReadResult {
    bool ok{false};
    std::string error; // set when !ok
    SmfData data;
};

SmfReadResult readStandardMidiFile(const uint8_t* bytes, size_t size);
inline SmfReadResult readStandardMidiFile(const std::vector<uint8_t>& b) {
    return readStandardMidiFile(b.data(), b.size());
}

// Always succeeds: out-of-range values are clamped (pitch 0..127, velocity 1..127, beats >= 0).
std::vector<uint8_t> writeStandardMidiFile(const SmfData& data);

} // namespace Midi
} // namespace Audio
} // namespace Aestra
