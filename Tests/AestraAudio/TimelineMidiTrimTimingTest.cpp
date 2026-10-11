// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TimelineMidiTrimTimingTest — SPEC 3 section 5.2 side finding: a MIDI clip trimmed on its left
// edge must play its notes where they are drawn.
//
// The scheduler places a note at (instance.startBeat + note.startBeat), so instance.startBeat is
// the beat where PATTERN beat 0 falls: clip start minus the left trim. The timeline used to hand it
// the clip's start, which plays every note of a left-trimmed clip late by the trim. Checked at the
// scheduler with real MIDI events, through the real TrackManager play path.

#include "Models/PatternManager.h"
#include "Models/TrackManager.h"
#include "Playback/PatternPlaybackEngine.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}
constexpr uint32_t kSampleRate = 48000;
constexpr uint64_t kBeat = 24000; // 120 BPM
constexpr uint32_t kBlock = 512;
} // namespace

int main() {
    TrackManager tm;
    tm.setOutputSampleRate(kSampleRate);
    tm.getPlaylistModel().setBPM(120.0);

    MidiPayload payload;
    for (auto [pitch, start] : {std::pair{60, 1.0}, std::pair{61, 3.0}, std::pair{62, 5.0}}) {
        MidiNote n;
        n.pitch = pitch;
        n.startBeat = start;
        n.durationBeats = 0.5;
        n.velocity = 0.8f;
        n.unitId = 1;
        payload.notes.push_back(n);
    }
    const PatternID pid = tm.getPatternManager().createMidiPattern("trimmed", 8.0, payload);
    const auto lane = tm.getPlaylistModel().createLane("L");
    // Placed at beat 8, then trimmed 2 beats off its left edge: it now shows pattern beats 2..6 at beats 8..12.
    const ClipInstanceID clip = tm.getPlaylistModel().addClipFromPattern(lane, pid, 8.0, 4.0);
    tm.getPlaylistModel().getClip(clip)->sourceOffset = 2.0;

    std::cout << "[1] the timeline hands the scheduler the pattern's origin, not the clip's start\n";
    tm.play();
    const auto instances = tm.getPatternPlaybackEngine().snapshotInstances();
    check(instances.size() == 1, "one scheduled instance");
    if (instances.size() == 1) {
        check(std::fabs(instances[0].startBeat - 6.0) < 1e-9,
              "pattern beat 0 falls at beat 6 (clip start 8 minus trim 2), got " + std::to_string(instances[0].startBeat));
        check(std::fabs(instances[0].sourceStartBeat - 2.0) < 1e-9, "from pattern beat 2");
        check(std::fabs(instances[0].sourceEndBeat - 6.0) < 1e-9, "to pattern beat 6");
    }

    std::cout << "[2] the notes sound where they are drawn\n";
    MidiBuffer buf;
    PatternPlaybackEngine::UnitMidiRoute routes[] = {PatternPlaybackEngine::UnitMidiRoute(1, &buf)};
    std::map<int, uint64_t> firstOn;
    const uint64_t end = 14 * kBeat;
    for (uint64_t frame = 0; frame < end; frame += kBlock) {
        buf.clear();
        tm.getPatternPlaybackEngine().refillWindow(frame, static_cast<int>(kSampleRate), 4096, 0);
        tm.getPatternPlaybackEngine().processAudio(frame, static_cast<int>(kBlock), routes, 1);
        for (size_t i = 0; i < buf.getEventCount(); ++i) {
            const auto& ev = buf.getEvent(i);
            if (ev.size >= 3 && (ev.data[0] & 0xF0) == 0x90 && ev.data[2] > 0 && !firstOn.count(ev.data[1]))
                firstOn[ev.data[1]] = frame + ev.sampleOffset;
        }
    }
    check(!firstOn.count(60), "the note before the trim (pattern beat 1) never sounds");
    check(firstOn.count(61) && firstOn[61] == 9 * kBeat, "pattern beat 3 sounds at beat 9 (drawn 1 beat into the clip)");
    check(firstOn.count(62) && firstOn[62] == 11 * kBeat, "pattern beat 5 sounds at beat 11");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
