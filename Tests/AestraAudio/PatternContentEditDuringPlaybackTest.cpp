// © 2026 Aestra Studios — All Rights Reserved.
// PatternContentEditDuringPlaybackTest
//
// Regression for #823: editing Arsenal steps while playing must neither sound
// a placed step before the playhead reaches it nor leave a deleted step
// ringing, and an edit must never re-fire notes that are currently sounding
// (the flam). Transport-entry catch-up (resuming a note already in progress
// when playback starts / seeks) must keep working.

#include "Playback/PatternPlaybackEngine.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"
#include "Models/UnitManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr uint64_t kLoopLength = kBeat * 4;
constexpr int kLookahead = static_cast<int>(kBeat * 2);
constexpr int kBlockSize = 512;

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

struct NoteOn {
    uint64_t frame;
    uint8_t pitch;
};

class Harness {
public:
    Harness() : m_engine(&m_clock, &m_patternMgr, &m_unitMgr) {
        m_unitId = m_unitMgr.createUnit("kick", UnitType::Sampler);
        MidiPayload payload;
        // One long gate per beat so a mid-edit "currently sounding" state exists.
        for (double beat : {0.0, 1.0, 2.0, 3.0}) {
            MidiNote note;
            note.pitch = 60 + static_cast<int>(beat);
            note.startBeat = beat;
            note.durationBeats = 1.0;
            note.velocity = 100.0f / 127.0f;
            note.unitId = m_unitId;
            payload.notes.push_back(note);
        }
        m_patternId = m_patternMgr.createMidiPattern("t", 4.0, payload);
    }

    void schedule() { m_engine.schedulePatternInstance(m_patternId, 0.0, 1); }

    // What TrackManager::patternContentEdited() forwards to when the Arsenal
    // grid or Piano Roll mutates pattern notes.
    void contentEdited() { m_engine.patternContentEdited(); }

    // Refill + process [from, to) one block at a time; returns note-ons observed.
    std::vector<NoteOn> run(uint64_t from, uint64_t to) {
        std::vector<NoteOn> ons;
        MidiBuffer buffer;
        std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{m_unitId, &buffer}};
        for (uint64_t f = from; f < to; f += kBlockSize) {
            const int size = static_cast<int>(std::min<uint64_t>(kBlockSize, to - f));
            m_engine.refillWindow(f, kSampleRate, kLookahead, kLoopLength);
            buffer.clear();
            m_engine.processAudio(f, size, routes.data(), routes.size());
            for (size_t i = 0; i < buffer.getEventCount(); ++i) {
                const auto& ev = buffer.getEvent(i);
                if ((ev.data[0] & 0xF0) == 0x90 && ev.data[2] != 0) {
                    ons.push_back({f + ev.sampleOffset, ev.data[1]});
                }
            }
        }
        return ons;
    }

    PatternManager& patterns() { return m_patternMgr; }
    PatternID patternId() const { return m_patternId; }
    uint64_t unitValue() const { return m_unitId; }

private:
    TimelineClock m_clock{120.0};
    PatternManager m_patternMgr;
    UnitManager m_unitMgr;
    PatternPlaybackEngine m_engine;
    UnitID m_unitId{};
    PatternID m_patternId{};
};

std::vector<NoteOn> onsFor(const std::vector<NoteOn>& ons, uint8_t pitch) {
    std::vector<NoteOn> out;
    std::copy_if(ons.begin(), ons.end(), std::back_inserter(out),
                 [pitch](const NoteOn& n) { return n.pitch == pitch; });
    return out;
}

bool firesNear(const std::vector<NoteOn>& ons, uint8_t pitch, uint64_t frame) {
    return std::any_of(ons.begin(), ons.end(), [pitch, frame](const NoteOn& n) {
        return n.pitch == pitch && std::llabs(static_cast<long long>(n.frame) - static_cast<long long>(frame)) < kBlockSize;
    });
}

} // namespace

int main() {
    // --- Scenario A: deleting a queued step silences it; sounding notes don't re-fire.
    {
        Harness fx;
        fx.schedule();
        (void)fx.run(0, 20000); // into the middle of beat 0's one-beat gate

        auto& patterns = fx.patterns();
        patterns.applyPatch(fx.patternId(), [](PatternSource& p) {
            if (!p.isMidi())
                return;
            auto& notes = std::get<MidiPayload>(p.payload).notes;
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [](const MidiNote& n) {
                                           return std::fabs(n.startBeat - 1.0) < 0.001;
                                       }),
                        notes.end());
        });
        fx.contentEdited();

        auto later = fx.run(20000, 72000);
        check(onsFor(later, 61).empty(), "deleted step must not fire after the edit");
        check(onsFor(later, 60).empty(), "the note sounding at edit time must not be re-fired (no flam)");
    }

    // --- Scenario B: placement behind the playhead waits for the next pass;
    //     placement ahead enters at its exact frame.
    {
        Harness fx;
        fx.schedule();
        (void)fx.run(0, 20000);

        const uint64_t unit = fx.unitValue();
        fx.patterns().applyPatch(fx.patternId(), [unit](PatternSource& p) {
            if (!p.isMidi())
                return;
            auto& notes = std::get<MidiPayload>(p.payload).notes;
            MidiNote behind;
            behind.pitch = 64;
            behind.startBeat = 0.5; // frame 12000 — already behind the playhead
            behind.durationBeats = 1.0;
            behind.velocity = 100.0f / 127.0f;
            behind.unitId = unit;
            MidiNote ahead = behind;
            ahead.pitch = 65;
            ahead.startBeat = 1.5; // frame 36000 — ahead of the playhead
            notes.push_back(behind);
            notes.push_back(ahead);
        });
        fx.contentEdited();

        auto restOfPass1 = fx.run(20000, 96000);
        check(onsFor(restOfPass1, 64).empty(), "step placed behind the playhead must stay silent this pass");
        check(onsFor(restOfPass1, 65).size() == 1 && firesNear(restOfPass1, 65, 36000),
              "step placed ahead of the playhead fires once at its exact frame");

        auto pass2 = fx.run(96000, 168000);
        check(onsFor(pass2, 64).size() == 1 && firesNear(pass2, 64, 96000 + 12000),
              "behind-playhead placement fires exactly once on the next pass");
    }

    // --- Scenario C: transport-entry catch-up still resumes an in-progress note once.
    {
        Harness fx;
        fx.schedule();
        // Enter playback mid-gate of beat 2 (gate spans frames 48000..72000).
        auto entered = fx.run(60000, 64000);
        check(onsFor(entered, 62).size() == 1, "entering playback mid-gate resumes the note exactly once");
    }

    if (g_failures == 0) {
        std::cout << "PatternContentEditDuringPlaybackTest: all checks passed\n";
        return 0;
    }
    std::cerr << "PatternContentEditDuringPlaybackTest: " << g_failures << " failure(s)\n";
    return 1;
}
