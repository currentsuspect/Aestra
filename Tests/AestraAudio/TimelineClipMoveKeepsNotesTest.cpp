// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Moving one timeline clip during playback must not disturb any other clip's sounding notes.
//
// Owner report (2026-09-23, live): dragging a pattern clip during playback "plays the
// midi as if I did a cut". refreshTimelinePatternInstances() used to clear the whole
// scheduler and reschedule from the UI's cached playhead. The clear dropped every
// sounding note's tracked gate and queued note-off. The reschedule then woke the
// refill's entry catch-up, which re-fired notes already sounding, late.
//
// The refresh is now incremental: clips keep their scheduler slots, slots keep their
// frontiers, and the content-edit refill re-queues from the real playhead.
//
// These run the REAL scheduler at MIDI level, block by block, refilling the way the
// engine's maintenance does, and count note-ons and note-offs per pitch.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr double kBeatFrames = 24000.0; // 120 BPM
constexpr uint32_t kBlock = 128;
constexpr uint64_t kTol = 2 * kBlock; // events land on block edges when due mid-refill
constexpr uint8_t kHeld = 60;         // clip A: one long note, sounding across every edit
constexpr uint8_t kShort = 72;        // clip B: one short note

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (!cond) {
        std::cout << "[FAIL] " << what << '\n';
        ++g_failures;
    }
}

struct MidiEvent {
    uint64_t frame;
    bool on;
    uint8_t pitch;
};

uint64_t beatFrame(double beat) { return static_cast<uint64_t>(beat * kBeatFrames); }

struct Fixture {
    TrackManager tm;
    UnitID unit{0};
    PlaylistLaneID laneA, laneB;
    ClipInstanceID clipA, clipB;
    std::vector<MidiEvent> events;
    uint64_t frame = 0;

    PatternID makePattern(const char* name, uint8_t pitch, double start, double length) {
        auto& patterns = tm.getPatternManager();
        const PatternID id = patterns.createPattern();
        auto* p = patterns.getPattern(id);
        p->type = PatternSource::Type::Midi;
        p->name = name;
        p->lengthBeats = 8.0;
        p->payload = MidiPayload{};
        std::get<MidiPayload>(p->payload).notes.push_back(MidiNote{pitch, start, length, 1.0f, 0.0f, unit});
        return id;
    }

    ClipInstanceID place(PlaylistLaneID lane, PatternID pattern, double start, double length) {
        ClipInstance clip;
        clip.patternId = pattern;
        clip.sourceId = pattern.value;
        clip.startBeat = start;
        clip.durationBeats = length;
        clip.sourceOffset = 0.0;
        return tm.getPlaylistModel().addClip(lane, clip);
    }

    bool build() {
        unit = tm.getUnitManager().createUnit("Move Unit", UnitType::Sampler);
        if (unit == 0) {
            return false;
        }
        auto& playlist = tm.getPlaylistModel();
        laneA = playlist.createLane("Held");
        laneB = playlist.createLane("Short");
        clipA = place(laneA, makePattern("Held", kHeld, 0.0, 6.0), 0.0, 8.0);
        clipB = place(laneB, makePattern("Short", kShort, 0.0, 1.0), 4.0, 4.0);
        tm.play();
        return clipA.isValid() && clipB.isValid() && tm.isPlaying();
    }

    // Maintenance refill + audio callback, the way the live engine interleaves them.
    void renderTo(double beat) {
        auto& playback = tm.getPatternPlaybackEngine();
        PatternPlaybackEngine::UnitMidiRoute route{unit, nullptr};
        const uint64_t end = beatFrame(beat);
        while (frame < end) {
            playback.refillWindow(frame, kSampleRate, 4096);
            MidiBuffer buffer;
            route.midiBuffer = &buffer;
            playback.processAudio(frame, kBlock, &route, 1);
            for (size_t i = 0; i < buffer.getEventCount(); ++i) {
                const auto& e = buffer.getEvent(i);
                const uint8_t status = e.data[0] & 0xF0;
                if (status == 0x90 && e.data[2] > 0) {
                    events.push_back({frame, true, e.data[1]});
                } else if (status == 0x80 || (status == 0x90 && e.data[2] == 0)) {
                    events.push_back({frame, false, e.data[1]});
                }
            }
            frame += kBlock;
        }
    }

    std::vector<MidiEvent> of(uint8_t pitch, bool on) const {
        std::vector<MidiEvent> out;
        for (const auto& e : events) {
            if (e.pitch == pitch && e.on == on) {
                out.push_back(e);
            }
        }
        return out;
    }
};

// Not "near": <windef.h> defines near/far as empty macros, which broke the MSVC build.
bool landsNear(uint64_t frame, double beat) {
    const uint64_t want = beatFrame(beat);
    return (frame > want ? frame - want : want - frame) <= kTol;
}

// THE report: move clip B while clip A's note sounds. A must be untouched.
void testMovingAnotherClipLeavesASoundingNoteAlone() {
    Fixture f;
    if (!f.build()) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    check(f.of(kHeld, true).size() == 1, "precondition: the held note started once");

    f.tm.getPlaylistModel().moveClip(f.clipB, 5.0, f.laneB);
    f.tm.refreshTimelinePatternInstances();
    f.renderTo(8.0);

    const auto heldOns = f.of(kHeld, true);
    const auto heldOffs = f.of(kHeld, false);
    check(heldOns.size() == 1, "the held note is not re-fired by another clip's move (got " +
                                   std::to_string(heldOns.size()) + " note-ons)");
    check(heldOffs.size() == 1 && landsNear(heldOffs.front().frame, 6.0),
          "the held note is not cut: it ends once, at beat 6");
    const auto shortOns = f.of(kShort, true);
    check(shortOns.size() == 1 && landsNear(shortOns.front().frame, 5.0),
          "the moved clip plays at its new position (beat 5), not its old one (beat 4)");
}

// Moving the clip that is sounding: its voice must be released, never left hanging.
void testMovingASoundingClipAwayReleasesIt() {
    Fixture f;
    if (!f.build()) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    f.tm.getPlaylistModel().moveClip(f.clipA, 12.0, f.laneA);
    f.tm.refreshTimelinePatternInstances();
    f.renderTo(3.0);

    const auto offs = f.of(kHeld, false);
    check(!offs.empty() && landsNear(offs.front().frame, 2.0),
          "moving a sounding clip past the playhead releases its note at the move");
    check(f.of(kHeld, true).size() == 1, "and does not re-fire it");
}

// Deleting the clip that is sounding: the old clear dropped its queued off with nothing to
// re-emit it. A hung note.
void testDeletingASoundingClipReleasesIt() {
    Fixture f;
    if (!f.build()) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    f.tm.getPlaylistModel().removeClip(f.clipA);
    f.tm.refreshTimelinePatternInstances();
    f.renderTo(8.0);

    const auto offs = f.of(kHeld, false);
    check(!offs.empty() && landsNear(offs.front().frame, 2.0), "deleting a sounding clip releases its note at once");
    check(f.tm.getPatternPlaybackEngine().getActiveInstanceCount() == 1, "the deleted clip's slot is gone");
}

// Moved so the held note has already ended at the new placement: started before the playhead
// but not spanning it. The old "still exists" rule kept it, with no off left to come.
void testMovingSoTheNoteAlreadyEndedReleasesIt() {
    Fixture f;
    if (!f.build()) {
        check(false, "fixture");
        return;
    }
    f.renderTo(2.0);
    // At beat -5 the note would run -5..1, over before the playhead at 2. Clip starts clamp at 0,
    // so trim the note instead: a 6-beat note shortened to 1 beat has also ended by beat 2.
    auto& patterns = f.tm.getPatternManager();
    const auto* a = f.tm.getPlaylistModel().getClip(f.clipA);
    patterns.applyPatch(a->patternId, [](PatternSource& p) {
        std::get<MidiPayload>(p.payload).notes.front().durationBeats = 1.0;
    });
    f.tm.refreshTimelinePatternInstances();
    f.renderTo(4.0);
    const auto offs = f.of(kHeld, false);
    check(!offs.empty() && landsNear(offs.front().frame, 2.0), "a sounding note whose placement has already ended is "
                                                          "released at once, not left hanging");
}

} // namespace

int main() {
    testMovingAnotherClipLeavesASoundingNoteAlone();
    testMovingASoundingClipAwayReleasesIt();
    testDeletingASoundingClipReleasesIt();
    testMovingSoTheNoteAlreadyEndedReleasesIt();

    if (g_failures == 0) {
        std::cout << "Timeline clip move tests passed\n";
        return 0;
    }
    std::cout << g_failures << " timeline clip move test(s) failed\n";
    return 1;
}
