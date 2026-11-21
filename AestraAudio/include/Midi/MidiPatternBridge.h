// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Between a parsed Standard MIDI File and Arsenal patterns. Pure functions, no engine or UI state,
// so both directions are unit-tested without an app.

#include "Midi/StandardMidiFile.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Midi {

struct ImportedPattern {
    std::string name;
    double lengthBeats{4.0};
    std::vector<MidiNote> notes;
};

/// One pattern per MIDI track that has notes (tempo-only tracks are skipped). Every note lands on
/// @p unitId. A track without a name is called after the file ("Song", "Song 2", ...). The length
/// rounds the last note's end up to a whole 4/4 bar, never under one bar.
inline std::vector<ImportedPattern> planPatternImport(const SmfData& smf, const std::string& fileStem,
                                                      uint64_t unitId) {
    std::vector<ImportedPattern> out;
    const std::string stem = fileStem.empty() ? std::string("MIDI") : fileStem;
    int unnamed = 0;
    for (const auto& t : smf.tracks) {
        if (t.notes.empty()) continue;
        ImportedPattern p;
        if (!t.name.empty()) {
            p.name = t.name;
        } else {
            ++unnamed;
            p.name = unnamed == 1 ? stem : stem + " " + std::to_string(unnamed);
        }
        double end = 0.0;
        for (const auto& n : t.notes) {
            MidiNote note = n.note;
            note.unitId = unitId;
            end = std::max(end, note.startBeat + note.durationBeats);
            p.notes.push_back(note);
        }
        p.lengthBeats = std::max(4.0, std::ceil(end / 4.0 - 1e-9) * 4.0);
        out.push_back(std::move(p));
    }
    return out;
}

/// A pattern as a MIDI file: tempo at beat 0, one track per unit (named by @p unitName, or the
/// pattern's name when only one unit plays). Notes of a unit keep their beats.
inline SmfData buildPatternExport(const std::string& patternName, const std::vector<MidiNote>& notes, double bpm,
                                  const std::function<std::string(uint64_t)>& unitName) {
    SmfData d;
    d.tempos.push_back({0.0, bpm > 0.0 ? bpm : 120.0});
    std::map<uint64_t, SmfTrack> byUnit;
    for (const auto& n : notes) {
        SmfNote s;
        s.note = n;
        byUnit[n.unitId].notes.push_back(s);
    }
    const bool single = byUnit.size() <= 1;
    for (auto& [unit, track] : byUnit) {
        track.name = single ? patternName : (unitName ? unitName(unit) : std::string());
        if (track.name.empty()) track.name = patternName;
        d.tracks.push_back(std::move(track));
    }
    if (d.tracks.empty()) {
        SmfTrack t;
        t.name = patternName;
        d.tracks.push_back(std::move(t));
    }
    return d;
}

} // namespace Midi
} // namespace Audio
} // namespace Aestra
