// © 2026 Aestra Studios — All Rights Reserved.
// PatternRepeatPlaybackTest
//
// V82-T3 (FD-50 board 8): a timeline clip longer than its pattern plays the pattern again every pattern length.
// Before, the scheduler played each note once and the clip went silent past the pattern's end, while the board
// draws a dashed line at each repeat. This pins:
//   A. a repeating clip plays every repeat; the same clip scheduled without repeats plays the pattern once;
//   B. a clip no longer than its pattern sounds exactly as it did before (repeat or not);
//   C. a source offset shifts which part of the repeating material the clip reads;
//   D. every note-on gets its note-off, a tail is cut at the clip's end, not at the pattern's;
//   E. playback starting deep into a long clip lands that repeat's notes on their exact frames;
//   F. the shared rules: repeat ranges, the right-edge snap, the piano-roll playhead wrap.

#include "Playback/PatternPlaybackEngine.h"
#include "Playback/PatternRepeat.h"
#include "Models/PatternManager.h"
#include "Models/PatternSource.h"
#include "Models/PlaylistModel.h"
#include "Models/UnitManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr int kBlockSize = 512;
constexpr int kLookahead = kSampleRate / 4;

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

bool near(double a, double b) { return std::abs(a - b) < 1.0e-9; }

struct Events {
    std::vector<uint64_t> ons;
    std::vector<uint64_t> offs;
};

class Harness {
public:
    // A 4-beat pattern: hits at beats 0 and 2, half a beat long, plus whatever @p extra adds.
    explicit Harness(std::vector<MidiNote> extra = {}) : m_engine(&m_clock, &m_patternMgr, &m_unitMgr) {
        m_unitId = m_unitMgr.createUnit("pluck", UnitType::Sampler);
        MidiPayload payload;
        for (double start : {0.0, 2.0}) {
            MidiNote note;
            note.pitch = 60;
            note.startBeat = start;
            note.durationBeats = 0.5;
            note.velocity = 0.8f;
            payload.notes.push_back(note);
        }
        for (const auto& n : extra) payload.notes.push_back(n);
        for (auto& n : payload.notes) n.unitId = m_unitId;
        m_patternId = m_patternMgr.createMidiPattern("pluck", 4.0, payload);
    }

    void schedule(double clipStart, double sourceOffset, double duration, bool repeat) {
        m_engine.schedulePatternInstance(m_patternId, clipStart - sourceOffset, 2, sourceOffset, duration, repeat);
    }

    // Timeline playback (no pattern loop) over [from, to) beats, a refill before each block.
    Events run(double fromBeat, double toBeat) {
        Events out;
        MidiBuffer buffer;
        std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{m_unitId, &buffer}};
        const auto to = static_cast<uint64_t>(toBeat * kBeat);
        for (auto f = static_cast<uint64_t>(fromBeat * kBeat); f < to; f += kBlockSize) {
            m_engine.refillWindow(f, kSampleRate, kLookahead, 0);
            buffer.clear();
            m_engine.processAudio(f, kBlockSize, routes.data(), routes.size());
            for (size_t i = 0; i < buffer.getEventCount(); ++i) {
                const auto& ev = buffer.getEvent(i);
                const uint8_t status = ev.data[0] & 0xF0;
                if (status == 0x90 && ev.data[2] != 0) out.ons.push_back(f + ev.sampleOffset);
                if (status == 0x80 || (status == 0x90 && ev.data[2] == 0)) out.offs.push_back(f + ev.sampleOffset);
            }
        }
        return out;
    }

private:
    TimelineClock m_clock{120.0};
    PatternManager m_patternMgr;
    UnitManager m_unitMgr;
    PatternPlaybackEngine m_engine;
    UnitID m_unitId{};
    PatternID m_patternId{};
};

std::vector<uint64_t> beats(std::initializer_list<double> list) {
    std::vector<uint64_t> frames;
    for (double b : list) frames.push_back(static_cast<uint64_t>(b * kBeat));
    return frames;
}

} // namespace

int main() {
    // --- A: a 12-beat clip of the 4-beat pattern plays it three times; without repeats, once.
    {
        Harness fx;
        fx.schedule(0.0, 0.0, 12.0, true);
        check(fx.run(0.0, 16.0).ons == beats({0, 2, 4, 6, 8, 10}), "A: a repeating clip plays every repeat");
    }
    {
        Harness fx;
        fx.schedule(0.0, 0.0, 12.0, false);
        check(fx.run(0.0, 16.0).ons == beats({0, 2}), "A: without repeats the pattern plays once");
    }

    // --- B: a clip no longer than its pattern is unchanged by repeats.
    for (bool repeat : {false, true}) {
        Harness fx;
        fx.schedule(8.0, 0.0, 3.0, repeat);
        check(fx.run(0.0, 16.0).ons == beats({8, 10}), "B: a 3-beat clip of a 4-beat pattern plays as before");
    }

    // --- C: clip at beat 8 reading source [1, 7): pattern beats 2, 4+0, 4+2 land at 9, 11, 13.
    {
        Harness fx;
        fx.schedule(8.0, 1.0, 6.0, true);
        check(fx.run(0.0, 20.0).ons == beats({9, 11, 13}), "C: a source offset reads into the repeating material");
    }

    // --- D: a note at 3.5 lasting a beat rings past the pattern's end into the next repeat (the clip goes on),
    // and the last repeat's copy is cut at the clip's end. Every on has its off.
    {
        MidiNote tail;
        tail.pitch = 64;
        tail.startBeat = 3.5;
        tail.durationBeats = 1.0;
        tail.velocity = 0.8f;
        Harness fx({tail});
        fx.schedule(0.0, 0.0, 8.0, true);
        const Events ev = fx.run(0.0, 12.0);
        check(ev.ons.size() == 6 && ev.offs.size() == 6, "D: six notes on, six off");
        check(std::find(ev.offs.begin(), ev.offs.end(), static_cast<uint64_t>(4.5 * kBeat)) != ev.offs.end(),
              "D: the first repeat's tail rings to 4.5, past the pattern's end");
        check(std::find(ev.offs.begin(), ev.offs.end(), static_cast<uint64_t>(8.0 * kBeat)) != ev.offs.end(),
              "D: the last repeat's tail stops at the clip's end");
    }

    // --- E: start 400 beats into a 1000-beat clip: repeat 100 is on time.
    {
        Harness fx;
        fx.schedule(0.0, 0.0, 1000.0, true);
        check(fx.run(400.0, 403.5).ons == beats({400, 402}), "E: a repeat deep into a long clip lands on its frame");
    }

    // --- F: the shared rules.
    {
        const PatternRepeat::Range one = PatternRepeat::inSource(4.0, 0.0, 4.0);
        check(one.first == 0 && one.last == 0, "F: a clip exactly one pattern long is repeat 0 only");
        const PatternRepeat::Range some = PatternRepeat::inSource(4.0, 1.0, 9.5);
        check(some.first == 0 && some.last == 2, "F: source [1, 9.5) touches repeats 0..2");
        const PatternRepeat::Range endless =
            PatternRepeat::inSource(4.0, 0.0, std::numeric_limits<double>::infinity());
        check(endless.first == 0 && endless.last == 0, "F: an endless instance (Arsenal) plays once per pass");

        // Clip at 0, pattern 4: inside the first pass the drag is a plain trim; past it, whole repeats.
        check(near(PatternRepeat::snapEndToRepeat(4.0, 0.0, 0.0, 2.5), 2.5), "F: a trim inside the first pass");
        check(near(PatternRepeat::snapEndToRepeat(4.0, 0.0, 0.0, 4.0), 4.0), "F: the first boundary");
        check(near(PatternRepeat::snapEndToRepeat(4.0, 0.0, 0.0, 9.9), 8.0), "F: nearest boundary, down");
        check(near(PatternRepeat::snapEndToRepeat(4.0, 0.0, 0.0, 10.1), 12.0), "F: nearest boundary, up");
        // The starter kit: a 16-beat clip of a 32-beat pattern stays free until it passes 32.
        check(near(PatternRepeat::snapEndToRepeat(32.0, 0.0, 0.0, 20.0), 20.0), "F: half a pattern stays free");
        check(near(PatternRepeat::snapEndToRepeat(32.0, 0.0, 0.0, 40.0), 32.0), "F: then snaps to 32");
        // Boundaries are measured from the pattern's start, not the clip's.
        check(near(PatternRepeat::snapEndToRepeat(4.0, 7.0, 8.0, 15.2), 15.0), "F: boundaries follow the offset");

        MidiClipPlaybackInstance clip;
        clip.patternId = PatternID{42};
        clip.startBeat = 8.0;
        clip.sourceOffsetBeats = 1.0;
        clip.durationBeats = 12.0;
        clip.patternLengthBeats = 4.0;
        const std::vector<MidiClipPlaybackInstance> clips{clip};
        const auto local = patternLocalBeatAt(clips, clip.patternId, 14.5);
        check(local && near(*local, 3.5), "F: the piano-roll playhead wraps with the repeats");
    }

    if (g_failures == 0) std::cout << "[PASS] PatternRepeatPlaybackTest\n";
    return g_failures == 0 ? 0 : 1;
}
