// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/PatternSource.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Swing, chance and ratchets (V81-AR1, FD-27: "swing, per-step chance, ratchets are new engine work").
 *
 * Pure rules the pattern scheduler applies before it turns notes into events, so they can be tested without
 * an audio device:
 *  - swing delays every odd 16th step by up to half a step (pattern-wide);
 *  - chance decides, per note and per pass, whether the step plays. The decision is a hash of the note's
 *    identity and the pass, never a random draw, so a re-queue after an edit while playing lands on the
 *    same answer and a bounce matches playback;
 *  - a ratchet splits the step into 2..4 equal hits (the gate shrinks with it; only the last may slide).
 */
inline constexpr double kExprStepBeats = 0.25;
inline constexpr double kMaxSwingDelayBeats = 0.125; ///< swing 1.0 = half a step late
inline constexpr uint8_t kMaxRatchet = 4;

/// The step index a note sits on, or -1 when it is off the 16th grid (swing only touches grid notes).
inline int gridStepOf(double startBeat) {
    const double step = std::round(startBeat / kExprStepBeats);
    return std::abs(startBeat - step * kExprStepBeats) < 0.002 ? static_cast<int>(step) : -1;
}

inline double swingDelayBeats(double startBeat, float swing) {
    const int step = gridStepOf(startBeat);
    if (step < 0 || (step & 1) == 0) return 0.0;
    return static_cast<double>(std::clamp(swing, 0.0f, 1.0f)) * kMaxSwingDelayBeats;
}

/// Same answer for the same note on the same pass, whenever it is asked.
inline bool chancePasses(uint64_t patternId, uint32_t instanceId, uint64_t unitId, double startBeat, int pitch,
                         uint64_t pass, float chance) {
    if (chance >= 1.0f) return true;
    if (chance <= 0.0f) return false;
    uint64_t h = 0x9E3779B97F4A7C15ull ^ patternId;
    auto mix = [&h](uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdull;
        h ^= h >> 33;
    };
    mix(instanceId);
    mix(unitId);
    mix(static_cast<uint64_t>(std::llround(startBeat * 960.0)));
    mix(static_cast<uint64_t>(pitch));
    mix(pass);
    // Full finalizer (splitmix64): the mixing above leaves small input differences small, and a pass
    // counter is exactly that, so without it every pass of a note gave nearly the same draw.
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebull;
    h ^= h >> 31;
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0) < static_cast<double>(chance);
}

/// True when any note, or the pattern's swing, needs the expansion; otherwise the scheduler uses the notes as they are.
inline bool needsExpression(const std::vector<MidiNote>& notes, float swing) {
    if (swing > 0.0f) return true;
    return std::any_of(notes.begin(), notes.end(), [](const MidiNote& n) { return n.chance < 1.0f || n.ratchet > 1; });
}

/// The notes as they play on this pass: swung, thinned by chance, ratchets split into hits.
inline std::vector<MidiNote> expandForPlayback(const std::vector<MidiNote>& notes, float swing, uint64_t patternId,
                                               uint32_t instanceId, uint64_t pass) {
    std::vector<MidiNote> out;
    out.reserve(notes.size());
    for (const auto& n : notes) {
        if (!chancePasses(patternId, instanceId, n.unitId, n.startBeat, n.pitch, pass, n.chance)) continue;
        const double start = n.startBeat + swingDelayBeats(n.startBeat, swing);
        const int hits = std::clamp<int>(n.ratchet, 1, kMaxRatchet);
        if (hits == 1) {
            MidiNote m = n;
            m.startBeat = start;
            out.push_back(m);
            continue;
        }
        const double span = std::max(n.durationBeats, 0.01) / hits;
        for (int i = 0; i < hits; ++i) {
            MidiNote m = n;
            m.startBeat = start + span * i;
            m.durationBeats = span;
            m.gate = n.gate / static_cast<float>(hits);
            m.slide = n.slide && i == hits - 1;
            m.ratchet = 1;
            out.push_back(m);
        }
    }
    return out;
}

} // namespace Audio
} // namespace Aestra
