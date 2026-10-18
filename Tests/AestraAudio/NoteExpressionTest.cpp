// © 2026 Aestra Studios — All Rights Reserved.
// NoteExpressionTest (V81-AR1, FD-27: swing, per-step chance and ratchets).
//
// The pure rules, then the scheduler: a ratchet plays a step as several hits inside it, swing pushes odd
// 16ths late and leaves even ones alone, chance thins a pattern the same way every time it is asked, and a
// pattern with none of these schedules exactly as before.

#include "Playback/NoteExpression.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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

void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

MidiNote stepNote(int step, uint64_t unit) {
    MidiNote n;
    n.pitch = 60;
    n.startBeat = step * 0.25;
    n.durationBeats = 0.25;
    n.velocity = 0.8f;
    n.unitId = unit;
    return n;
}

struct Hit {
    uint64_t frame;
};

// Plays one loop of a pattern and returns the frames of every note-on.
std::vector<Hit> playOnce(const std::vector<MidiNote>& notes, float swing, uint64_t loops = 1) {
    TimelineClock clock{120.0};
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine engine(&clock, &patterns, &units);
    const UnitID unit = units.createUnit("u", UnitType::Sampler);
    MidiPayload payload;
    for (auto n : notes) { n.unitId = unit; payload.notes.push_back(n); }
    const PatternID pid = patterns.createMidiPattern("t", 4.0, payload);
    patterns.getPattern(pid)->swing = swing;
    engine.schedulePatternInstance(pid, 0.0, 1);
    MidiBuffer buffer;
    std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{unit, &buffer}};
    std::vector<Hit> hits;
    for (uint64_t f = 0; f < kLoopLength * loops; f += kBlockSize) {
        engine.refillWindow(f, kSampleRate, kLookahead, kLoopLength);
        buffer.clear();
        engine.processAudio(f, kBlockSize, routes.data(), routes.size());
        for (size_t i = 0; i < buffer.getEventCount(); ++i) {
            const auto& ev = buffer.getEvent(i);
            if ((ev.data[0] & 0xF0) == 0x90 && ev.data[2] != 0 && f + ev.sampleOffset < kLoopLength * loops) hits.push_back({f + ev.sampleOffset});
        }
    }
    return hits;
}
} // namespace

int main() {
    std::cout << "[1] swing delays odd 16ths only\n";
    check(swingDelayBeats(0.0, 1.0f) == 0.0, "step 0 is never delayed");
    check(swingDelayBeats(0.25, 0.0f) == 0.0, "no swing, no delay");
    check(std::abs(swingDelayBeats(0.25, 1.0f) - kMaxSwingDelayBeats) < 1e-9, "full swing delays step 1 by half a step");
    check(swingDelayBeats(0.5, 1.0f) == 0.0, "step 2 is even");
    check(swingDelayBeats(0.3, 1.0f) == 0.0, "an off-grid note is left where it was put");

    std::cout << "[2] chance is the same answer every time\n";
    check(chancePasses(1, 1, 1, 0.0, 60, 0, 1.0f), "chance 1 always plays");
    check(!chancePasses(1, 1, 1, 0.0, 60, 0, 0.0f), "chance 0 never plays");
    bool stable = true;
    for (uint64_t pass = 0; pass < 50; ++pass)
        stable = stable && chancePasses(7, 2, 3, 1.5, 62, pass, 0.5f) == chancePasses(7, 2, 3, 1.5, 62, pass, 0.5f);
    check(stable, "the same note on the same pass decides the same way");
    int played = 0;
    for (uint64_t pass = 0; pass < 2000; ++pass) played += chancePasses(7, 2, 3, 1.5, 62, pass, 0.5f) ? 1 : 0;
    check(played > 850 && played < 1150, "chance 0.5 plays about half the passes (" + std::to_string(played) + "/2000)");

    {
        int differing = 0;
        for (uint64_t pass = 1; pass < 64; ++pass)
            differing += chancePasses(1, 1, 1, 0.0, 60, pass, 0.5f) != chancePasses(1, 1, 1, 0.0, 60, 0, 0.5f) ? 1 : 0;
        check(differing > 10, "consecutive passes do not all draw alike (" + std::to_string(differing) + "/63 differ from pass 0)");
    }

    std::cout << "[3] expansion\n";
    std::vector<MidiNote> plain{stepNote(0, 1), stepNote(1, 1)};
    check(!needsExpression(plain, 0.0f), "plain notes need no expansion");
    check(needsExpression(plain, 0.3f), "swing needs expansion");
    plain[1].ratchet = 3;
    const auto expanded = expandForPlayback(plain, 0.0f, 1, 1, 0);
    check(expanded.size() == 4, "a 3-ratchet makes three hits");
    check(std::abs(expanded[2].startBeat - (0.25 + 0.25 / 3.0)) < 1e-9, "hits are evenly spaced inside the step");
    check(expanded[1].durationBeats < 0.25 && std::abs(expanded[1].gate - 1.0f / 3.0f) < 1e-6, "the gate shrinks with the hit");

    std::cout << "[4] the scheduler plays them\n";
    const auto straight = playOnce({stepNote(0, 1), stepNote(1, 1), stepNote(2, 1), stepNote(3, 1)}, 0.0f);
    check(straight.size() == 4, "four steps, four hits");
    const uint64_t step = kBeat / 4;
    check(straight[1].frame == step, "step 1 lands on the grid");

    const auto swung = playOnce({stepNote(0, 1), stepNote(1, 1), stepNote(2, 1), stepNote(3, 1)}, 1.0f);
    check(swung.size() == 4 && swung[0].frame == 0 && swung[2].frame == 2 * step, "even steps stay on the grid");
    check(swung[1].frame > straight[1].frame && swung[1].frame - straight[1].frame <= step / 2 + 1, "odd steps are late by up to half a step");

    auto ratchet = stepNote(1, 1);
    ratchet.ratchet = 4;
    const auto rat = playOnce({ratchet}, 0.0f);
    check(rat.size() == 4, "a 4-ratchet is four note-ons");
    check(rat.size() == 4 && rat[0].frame == step && rat[3].frame < 2 * step, "all inside the step");

    auto never = stepNote(0, 1);
    never.chance = 0.0f;
    check(playOnce({never, stepNote(1, 1)}, 0.0f).size() == 1, "chance 0 drops the step, the rest plays");
    auto half = stepNote(0, 1);
    half.chance = 0.5f;
    const auto a = playOnce({half}, 0.0f, 8), b = playOnce({half}, 0.0f, 8);
    check(a.size() == b.size() && a.size() < 8 && a.size() > 0, "chance thins across passes and replays identically (" + std::to_string(a.size()) + "/8)");

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
