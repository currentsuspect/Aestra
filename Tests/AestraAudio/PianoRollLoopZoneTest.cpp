// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SPEC 3 §5.2 (owner ruling: "piano-roll ruler zones create a local playback loop for that
// pattern"). A zone [s, e) is played by scheduling the pattern with startBeat = -s and source
// range [s, e) while the engine loops [0, e - s): the path trimmed clips already use.
//
// This pins the timing at the scheduler, with real MIDI events: inside a zone, pattern beat b
// sounds at loop beat b - s; notes outside the zone never sound; a note crossing the zone end
// is cut off there, so nothing hangs across the wrap.

#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cout << "[FAIL] " << message << '\n';
        ++g_failures;
    }
}

constexpr uint32_t kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr uint32_t kBlock = 512;

MidiNote note(int pitch, double start, double length) {
    MidiNote n;
    n.pitch = pitch;
    n.startBeat = start;
    n.durationBeats = length;
    n.velocity = 0.8f;
    n.unitId = 1;
    return n;
}

struct Seen {
    std::map<int, std::vector<uint64_t>> on;
    std::map<int, std::vector<uint64_t>> off;
};

// Run the scheduler over [0, endFrame) in audio-sized blocks, as the engine does.
Seen run(PatternPlaybackEngine& scheduler, uint64_t loopSamples, uint64_t endFrame) {
    Seen seen;
    MidiBuffer buf;
    PatternPlaybackEngine::UnitMidiRoute routes[] = {PatternPlaybackEngine::UnitMidiRoute(1, &buf)};
    for (uint64_t frame = 0; frame < endFrame; frame += kBlock) {
        buf.clear();
        scheduler.refillWindow(frame, static_cast<int>(kSampleRate), 4096, loopSamples);
        scheduler.processAudio(frame, static_cast<int>(kBlock), routes, 1);
        for (size_t i = 0; i < buf.getEventCount(); ++i) {
            const auto& ev = buf.getEvent(i);
            if (ev.size < 2) continue;
            const uint8_t status = ev.data[0] & 0xF0;
            const uint64_t at = frame + ev.sampleOffset;
            if (status == 0x90 && ev.data[2] > 0) seen.on[ev.data[1]].push_back(at);
            if (status == 0x80 || (status == 0x90 && ev.data[2] == 0)) seen.off[ev.data[1]].push_back(at);
        }
    }
    return seen;
}

void testAZoneLoopsOnlyItsRange() {
    TimelineClock clock(120.0);
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine scheduler(&clock, &patterns, &units);

    MidiPayload payload;
    payload.notes = {note(60, 0.0, 1.0), note(61, 3.0, 1.0), note(62, 5.5, 1.0), note(63, 7.0, 1.0)};
    const PatternID pid = patterns.createMidiPattern("zone", 8.0, payload);

    // Zone [2, 6): pattern beat 0 lands at -2, only [2, 6) sounds, the engine loops 4 beats.
    constexpr double s = 2.0, e = 6.0;
    scheduler.schedulePatternInstance(pid, -s, 1, s, e - s);
    const uint64_t loop = static_cast<uint64_t>((e - s) * kBeat);
    Seen seen = run(scheduler, loop, loop); // exactly one pass

    check(!seen.on[61].empty(), "the note inside the zone sounds (so the checks below are not vacuous)");
    check(!seen.on[61].empty() && seen.on[61].front() == 1 * kBeat,
          "pattern beat 3 sounds at loop beat 1 (3 - zone start 2)");
    check(!seen.on[62].empty() && seen.on[62].front() == static_cast<uint64_t>(3.5 * kBeat),
          "pattern beat 5.5 sounds at loop beat 3.5");
    check(seen.on[60].empty(), "a note before the zone never sounds");
    check(seen.on[63].empty(), "a note after the zone never sounds");

    // The note crossing the zone end (5.5..6.5) is cut at the zone end: its note-off is due at
    // loop beat 4, the wrap, not at 4.5 in the next pass.
    PatternPlaybackEngine again(&clock, &patterns, &units);
    again.schedulePatternInstance(pid, -s, 1, s, e - s);
    Seen withWrap = run(again, loop, loop + kBlock);
    bool cutAtZoneEnd = false;
    for (const uint64_t at : withWrap.off[62]) {
        cutAtZoneEnd = cutAtZoneEnd || (at > static_cast<uint64_t>(3.5 * kBeat) && at <= loop);
    }
    check(cutAtZoneEnd, "the note crossing the zone end is released by the wrap, not in the next pass");
}

// Control: the same pattern without a zone plays pattern beat 3 at beat 3, and every note.
void testWithoutAZoneTheWholePatternPlays() {
    TimelineClock clock(120.0);
    PatternManager patterns;
    UnitManager units;
    PatternPlaybackEngine scheduler(&clock, &patterns, &units);

    MidiPayload payload;
    payload.notes = {note(60, 0.0, 1.0), note(61, 3.0, 1.0), note(63, 7.0, 1.0)};
    const PatternID pid = patterns.createMidiPattern("whole", 8.0, payload);
    scheduler.schedulePatternInstance(pid, 0.0, 1);
    const uint64_t loop = 8 * kBeat;
    Seen seen = run(scheduler, loop, loop);

    check(!seen.on[60].empty() && seen.on[60].front() == 0, "without a zone beat 0 sounds at 0");
    check(!seen.on[61].empty() && seen.on[61].front() == 3 * kBeat, "without a zone beat 3 sounds at 3");
    check(!seen.on[63].empty(), "without a zone the last note sounds too");
}

} // namespace

int main() {
    testAZoneLoopsOnlyItsRange();
    testWithoutAZoneTheWholePatternPlays();
    if (g_failures == 0) {
        std::cout << "Piano roll loop zone tests passed\n";
        return 0;
    }
    std::cout << g_failures << " piano roll loop zone test(s) failed\n";
    return 1;
}
