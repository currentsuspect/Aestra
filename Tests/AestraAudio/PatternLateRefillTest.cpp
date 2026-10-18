// © 2026 Aestra Studios — All Rights Reserved.
// PatternLateRefillTest
//
// The Arsenal's hits are queued by refillWindow() on the main loop. When the UI thread
// stalls past the lookahead, the playhead overtakes the last queued frame and the hits in
// between were silently skipped: the beat stuttered for up to a second and a half with no
// xrun anywhere (tutorial takes, 2026-10-18). This pins three things:
//   A. a stall shorter than the live lookahead (a quarter second) loses no hit (150 ms here: the
//      main loop resumes on a block edge, so a stall effectively rounds up by up to a block);
//   B. a stall longer than the lookahead is counted: getSchedulingStats() reports the late
//      refill, every hit that never sounded, and how late the refill was;
//   C. with the longer window, an edit nobody announced (no patternContentEdited(): the piano
//      roll, Muse note verbs, undo) is still heard at its exact frame, and a deleted hit that
//      was already queued does not sound.

#include "Playback/PatternPlaybackEngine.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"
#include "Models/UnitManager.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr uint64_t kLoopLength = kBeat * 4;
constexpr uint64_t kStep = kBeat / 4; // a 16th
constexpr int kBlockSize = 2048;
constexpr int kLiveLookahead = kSampleRate / 4; // AudioEngine::performNonRealtimeMaintenance
constexpr int kOldLookahead = 4096;

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

class Harness {
public:
    Harness() : m_engine(&m_clock, &m_patternMgr, &m_unitMgr) {
        m_unitId = m_unitMgr.createUnit("hats", UnitType::Sampler);
        MidiPayload payload;
        for (int s = 0; s < 16; ++s) { // a hat on every 16th, short like the real one
            MidiNote note;
            note.pitch = 60;
            note.startBeat = s * 0.25;
            note.durationBeats = 0.1;
            note.velocity = 0.8f;
            note.unitId = m_unitId;
            payload.notes.push_back(note);
        }
        m_patternId = m_patternMgr.createMidiPattern("hats", 4.0, payload);
        m_engine.schedulePatternInstance(m_patternId, 0.0, 1);
    }

    // Process [from, to) block by block. A refill runs before each block unless the block
    // starts inside [stallFrom, stallTo): the main loop is stuck there, the audio is not.
    std::vector<uint64_t> run(uint64_t from, uint64_t to, int lookahead, uint64_t stallFrom = 0, uint64_t stallTo = 0) {
        std::vector<uint64_t> ons;
        MidiBuffer buffer;
        std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{m_unitId, &buffer}};
        for (uint64_t f = from; f < to; f += kBlockSize) {
            const int size = static_cast<int>(std::min<uint64_t>(kBlockSize, to - f));
            if (f < stallFrom || f >= stallTo) {
                m_engine.refillWindow(f, kSampleRate, lookahead, kLoopLength);
            }
            buffer.clear();
            m_engine.processAudio(f, size, routes.data(), routes.size());
            for (size_t i = 0; i < buffer.getEventCount(); ++i) {
                const auto& ev = buffer.getEvent(i);
                if ((ev.data[0] & 0xF0) == 0x90 && ev.data[2] != 0) ons.push_back(f + ev.sampleOffset);
            }
        }
        return ons;
    }

    PatternPlaybackEngine& engine() { return m_engine; }
    PatternManager& patterns() { return m_patternMgr; }
    PatternID patternId() const { return m_patternId; }

private:
    TimelineClock m_clock{120.0};
    PatternManager m_patternMgr;
    UnitManager m_unitMgr;
    PatternPlaybackEngine m_engine;
    UnitID m_unitId{};
    PatternID m_patternId{};
};

bool firesAt(const std::vector<uint64_t>& ons, uint64_t frame) {
    return std::find(ons.begin(), ons.end(), frame) != ons.end();
}

size_t hitsBetween(uint64_t from, uint64_t to) { // hats on the 16th grid in [from, to)
    return static_cast<size_t>((to + kStep - 1) / kStep - (from + kStep - 1) / kStep);
}

} // namespace

int main() {
    // --- A: a 150 ms stall inside the quarter-second lookahead loses nothing.
    {
        Harness fx;
        const uint64_t stallFrom = kLoopLength;                 // second bar, mid-loop playback
        const uint64_t stallTo = stallFrom + 3 * kSampleRate / 20; // 150 ms
        const auto ons = fx.run(0, kLoopLength * 3, kLiveLookahead, stallFrom, stallTo);
        check(ons.size() == hitsBetween(0, kLoopLength * 3), "A: every hat sounds through a 150 ms stall");
        bool onGrid = true;
        for (uint64_t f = 0; f < kLoopLength * 3; f += kStep) onGrid = onGrid && firesAt(ons, f);
        check(onGrid, "A: and each lands on its exact frame");
        const auto stats = fx.engine().getSchedulingStats();
        check(stats.lateRefills == 0 && stats.missedNoteOns == 0, "A: nothing is reported missed");
    }

    // --- B: the old 4096-sample window under a 300 ms stall: hits are lost, and now counted.
    {
        Harness fx;
        const uint64_t stallFrom = kLoopLength;
        const uint64_t stallTo = stallFrom + 3 * kSampleRate / 10; // 300 ms
        const auto ons = fx.run(0, kLoopLength * 3, kOldLookahead, stallFrom, stallTo);
        const size_t expected = hitsBetween(0, kLoopLength * 3);
        const auto stats = fx.engine().getSchedulingStats();
        check(ons.size() < expected, "B: a stall past the window still skips hits (the scheduler cannot invent time)");
        check(stats.lateRefills >= 1, "B: the late refill is counted");
        check(stats.missedNoteOns == expected - ons.size(), "B: every hit that never sounded is counted, no more");
        check(stats.maxLateFrames > 0 && stats.maxLateFrames <= 3 * kSampleRate / 10,
              "B: lateness is reported and bounded by the stall");
    }

    // --- C: an unannounced edit is heard at its exact frame; a queued, deleted hit is not.
    {
        Harness fx;
        (void)fx.run(0, kBeat, kLiveLookahead); // one beat in; the next quarter second is queued
        const uint64_t playhead = kBeat;
        // Add a hit 100 ms ahead (off the 16th grid, so it is unmistakable) and delete the
        // queued hat at beat 1.25 — WITHOUT patternContentEdited().
        const double addedBeat = 1.0 + 0.2;  // 100 ms past the playhead at 120 BPM
        const double deletedBeat = 1.25; // already queued: the window reached beat ~1.44
        fx.patterns().applyPatch(fx.patternId(), [&](PatternSource& p) {
            auto& notes = std::get<MidiPayload>(p.payload).notes;
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [&](const MidiNote& n) { return n.startBeat == deletedBeat; }),
                        notes.end());
            MidiNote added = notes.front();
            added.startBeat = addedBeat;
            added.pitch = 61;
            notes.push_back(added);
        });
        const auto ons = fx.run(playhead, kLoopLength, kLiveLookahead);
        check(firesAt(ons, static_cast<uint64_t>(addedBeat * kBeat)), "C: the added hit sounds at its exact frame");
        check(!firesAt(ons, static_cast<uint64_t>(deletedBeat * kBeat)), "C: the deleted hit does not sound");
        check(firesAt(ons, static_cast<uint64_t>(1.75 * kBeat)), "C: untouched hats keep playing");
    }

    if (g_failures == 0) {
        std::cout << "PatternLateRefillTest: all checks passed\n";
        return 0;
    }
    std::cerr << "PatternLateRefillTest: " << g_failures << " check(s) failed\n";
    return 1;
}
