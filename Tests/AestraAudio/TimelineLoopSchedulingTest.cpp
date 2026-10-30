// © 2026 Aestra Studios — All Rights Reserved.
// TimelineLoopSchedulingTest
//
// The note scheduler queues a quarter second ahead on the main loop. Timeline loops used to
// rewind the queue at every wrap and wait for the next main-loop tick to re-queue the loop
// top, so the first notes of each pass came late or never, by however long the UI took.
// They now plan in a monotonic frame domain (ScheduleLoop), like pattern loops, and the
// queue is discarded by epoch instead of by resetting the SPSC write index. This pins:
//   A. ScheduleLoop maps frames continuously across wraps and confines notes to a pass;
//   B. with refills stalled for 100 ms either side of every wrap, every note of every pass
//      lands on its exact frame, nothing sounds past the loop end, and a note held across
//      the loop start re-enters exactly at the loop top;
//   C. a rewind from the audio thread drops everything queued, and a refill that read the
//      playhead before the rewind (stale ticket) cannot revive notes from the old position.

#include "Playback/PatternPlaybackEngine.h"
#include "Playback/ScheduleLoop.h"
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
constexpr uint64_t kStep = kBeat / 4;
constexpr int kBlockSize = 512;
constexpr int kLookahead = kSampleRate / 4;
constexpr uint64_t kLoopStart = 4 * kBeat;
constexpr uint64_t kLoopEnd = 8 * kBeat;
constexpr uint64_t kLoopLength = kLoopEnd - kLoopStart;
constexpr int kHatPitch = 60;
constexpr int kPadPitch = 50;

int g_failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

struct NoteOn {
    uint64_t frame;
    int pitch;
};

class Harness {
public:
    Harness() : m_engine(&m_clock, &m_patternMgr, &m_unitMgr) {
        m_unitId = m_unitMgr.createUnit("kit", UnitType::Sampler);
        MidiPayload payload;
        for (int s = 0; s < 16 * 4; ++s) { // a short hat on every 16th for 16 beats
            MidiNote note;
            note.pitch = kHatPitch;
            note.startBeat = s * 0.25;
            note.durationBeats = 0.1;
            note.velocity = 0.8f;
            note.unitId = m_unitId;
            payload.notes.push_back(note);
        }
        MidiNote pad; // starts before the loop, still held at the loop start
        pad.pitch = kPadPitch;
        pad.startBeat = 3.5;
        pad.durationBeats = 2.0;
        pad.velocity = 0.8f;
        pad.unitId = m_unitId;
        payload.notes.push_back(pad);
        m_patternId = m_patternMgr.createMidiPattern("kit", 16.0, payload);
        m_engine.schedulePatternInstance(m_patternId, 0.0, 1);
    }

    // Monotonic frames [from, to). A refill runs before each block unless the block starts
    // within `stall` frames of a wrap: the main loop is stuck there, the audio is not.
    std::vector<NoteOn> run(uint64_t from, uint64_t to, const ScheduleLoop& loop, uint64_t stall) {
        std::vector<NoteOn> ons;
        MidiBuffer buffer;
        std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{m_unitId, &buffer}};
        for (uint64_t f = from; f < to; f += kBlockSize) {
            const int size = static_cast<int>(std::min<uint64_t>(kBlockSize, to - f));
            bool stalled = false;
            for (uint64_t wrap = loop.end(); wrap < to + loop.length; wrap += loop.length) {
                stalled = stalled || (f + stall >= wrap && f < wrap + stall);
            }
            if (!stalled) {
                m_engine.refillWindow(m_engine.refillTicket(), f, kSampleRate, kLookahead, loop);
            }
            buffer.clear();
            m_engine.processAudio(f, size, routes.data(), routes.size());
            collect(buffer, f, ons);
        }
        return ons;
    }

    static void collect(const MidiBuffer& buffer, uint64_t blockStart, std::vector<NoteOn>& ons) {
        for (size_t i = 0; i < buffer.getEventCount(); ++i) {
            const auto& ev = buffer.getEvent(i);
            if ((ev.data[0] & 0xF0) == 0x90 && ev.data[2] != 0) {
                ons.push_back({blockStart + ev.sampleOffset, ev.data[1]});
            }
        }
    }

    PatternPlaybackEngine& engine() { return m_engine; }
    UnitID unit() const { return m_unitId; }

private:
    TimelineClock m_clock{120.0};
    PatternManager m_patternMgr;
    UnitManager m_unitMgr;
    PatternPlaybackEngine m_engine;
    UnitID m_unitId{};
    PatternID m_patternId{};
};

bool has(const std::vector<NoteOn>& ons, uint64_t frame, int pitch) {
    return std::any_of(ons.begin(), ons.end(), [&](const NoteOn& n) { return n.frame == frame && n.pitch == pitch; });
}

ScheduleLoop timelineLoop() {
    ScheduleLoop loop;
    loop.start = kLoopStart;
    loop.length = kLoopLength;
    loop.timeline = true;
    return loop;
}

void testMapping() {
    const ScheduleLoop loop = timelineLoop();
    check(loop.passOf(0) == 0 && loop.passOf(kLoopEnd - 1) == 0, "A: pre-roll and first time round are pass 0");
    check(loop.passOf(kLoopEnd) == 1 && loop.passOf(kLoopEnd + kLoopLength - 1) == 1, "A: pass 1 starts at the loop end");
    check(loop.baseOf(1) + kLoopStart == kLoopEnd, "A: the domain is continuous across the wrap");

    uint64_t on = loop.baseOf(1) + kLoopEnd;
    uint64_t off = on + 10;
    check(!loop.placeInPass(1, on, off), "A: a note at the loop end sounds in no pass");
    on = loop.baseOf(1) + kLoopStart - 100;
    off = loop.baseOf(1) + kLoopStart + 100;
    check(loop.placeInPass(1, on, off) && on == loop.baseOf(1) + kLoopStart, "A: a held note chases to the loop top");
    on = loop.baseOf(0) + kLoopEnd - 10;
    off = loop.baseOf(0) + kLoopEnd + 500;
    check(loop.placeInPass(0, on, off) && off == kLoopEnd, "A: a note-off past the loop end is pulled to the wrap");

    ScheduleLoop pattern;
    pattern.length = kLoopLength;
    check(pattern.passOf(kLoopLength * 3 + 5) == 3, "A: pattern loops keep the plain pass index");
}

void testPassesLandExactlyThroughStalls() {
    Harness fx;
    const ScheduleLoop loop = timelineLoop();
    const uint64_t passes = 3;
    const uint64_t to = kLoopEnd + passes * kLoopLength;
    const uint64_t stall = kSampleRate / 10; // 100 ms either side of each wrap: 200 ms with no refill
    const auto ons = fx.run(0, to, loop, stall);

    size_t expected = 0;
    bool exact = true;
    for (uint64_t t = 0; t < kLoopEnd; t += kStep, ++expected) {
        exact = exact && has(ons, t, kHatPitch); // pass 0: pre-roll and first time round
    }
    for (uint64_t p = 1; p <= passes; ++p) {
        for (uint64_t t = kLoopStart; t < kLoopEnd; t += kStep, ++expected) {
            exact = exact && has(ons, loop.baseOf(p) + t, kHatPitch);
        }
    }
    const size_t hats = static_cast<size_t>(
        std::count_if(ons.begin(), ons.end(), [](const NoteOn& n) { return n.pitch == kHatPitch; }));
    check(exact, "B: every hat of every pass lands on its exact frame, wraps included");
    check(hats == expected, "B: and nothing else sounds (no late duplicates, nothing past the loop end)");

    bool padChases = has(ons, static_cast<uint64_t>(3.5 * kBeat), kPadPitch);
    for (uint64_t p = 1; p <= passes; ++p) {
        padChases = padChases && has(ons, loop.baseOf(p) + kLoopStart, kPadPitch);
    }
    check(padChases, "B: a note held across the loop start re-enters exactly at each loop top");

    const auto stats = fx.engine().getSchedulingStats();
    check(stats.lateRefills == 0 && stats.missedNoteOns == 0, "B: no refill was late");
}

void testRewindDropsQueuedAndStaleTicketCannotRevive() {
    Harness fx;
    ScheduleLoop linear;
    MidiBuffer buffer;
    std::vector<PatternPlaybackEngine::UnitMidiRoute> routes{{fx.unit(), &buffer}};

    fx.engine().refillWindow(fx.engine().refillTicket(), 0, kSampleRate, kLookahead, linear);
    fx.engine().rewindScheduledInstances(); // the audio thread, e.g. a stop edge
    buffer.clear();
    fx.engine().processAudio(0, kBlockSize, routes.data(), routes.size());
    check(buffer.getEventCount() == 0, "C: a rewind drops every queued event unplayed");

    // A seek to beat 10: the refill read the OLD playhead (beat 0) and took its ticket
    // before the audio thread's rewind for the seek landed.
    const uint16_t staleTicket = fx.engine().refillTicket();
    fx.engine().rewindScheduledInstances();
    fx.engine().refillWindow(staleTicket, 0, kSampleRate, kLookahead, linear);
    const uint64_t seekTo = 10 * kBeat;
    buffer.clear();
    fx.engine().processAudio(seekTo, kBlockSize, routes.data(), routes.size());
    check(buffer.getEventCount() == 0, "C: notes planned from the stale playhead do not burst out after the seek");

    // The next refill reads the new playhead under a current ticket and plays normally.
    std::vector<NoteOn> ons;
    for (uint64_t f = seekTo; f < seekTo + kBeat; f += kBlockSize) {
        fx.engine().refillWindow(fx.engine().refillTicket(), f, kSampleRate, kLookahead, linear);
        buffer.clear();
        fx.engine().processAudio(f, kBlockSize, routes.data(), routes.size());
        Harness::collect(buffer, f, ons);
    }
    check(has(ons, seekTo + kStep, kHatPitch) && has(ons, seekTo + 2 * kStep, kHatPitch),
          "C: after a fresh refill the hats land on their exact frames");
}

} // namespace

int main() {
    testMapping();
    testPassesLandExactlyThroughStalls();
    testRewindDropsQueuedAndStaleTicketCannotRevive();
    if (g_failures == 0) {
        std::cout << "TimelineLoopSchedulingTest: all checks passed\n";
        return 0;
    }
    std::cerr << "TimelineLoopSchedulingTest: " << g_failures << " check(s) failed\n";
    return 1;
}
