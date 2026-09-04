// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// T-4 / T-5 (v0.7.1 trust sprint): the pattern scheduler builds its instance
// set once — at play() time, from collectMidiClipInstances() — snapshotting
// clip bounds AND lane/clip mute+solo. The graph-rebuild flag that mute
// toggles, splits, and deletes raise does NOT rebuild that set; only an
// explicit refreshTimelinePatternInstances() does (previously called only by
// duplicate + paint). Without it, unmuting mid-playback stays silent and
// split/deleted clips keep sounding with pre-edit bounds until loop wrap.
//
// These cases pin the reschedule contract the UI call sites now rely on:
// after refresh, the scheduled set reflects the CURRENT model — split halves
// schedule as two instances, muted lanes schedule nothing, unmuted lanes
// schedule again, and refresh while stopped is a safe no-op.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr int kSampleRate = 48000;
constexpr double kBeatFrames = 24000.0; // 120 BPM
constexpr uint8_t kPitchA = 60;
constexpr uint8_t kPitchB = 62;

struct ObservedNoteOn {
    uint64_t frame;
    uint8_t pitch;
};

std::vector<ObservedNoteOn> renderAllNoteOns(PatternPlaybackEngine& playback, double beats, UnitID unitId) {
    PatternPlaybackEngine::UnitMidiRoute route{unitId, nullptr};
    std::vector<ObservedNoteOn> hits;
    constexpr uint32_t kBlock = 128;
    const uint64_t totalFrames = static_cast<uint64_t>(beats * kBeatFrames);
    uint64_t frame = 0;
    while (frame < totalFrames) {
        MidiBuffer buffer;
        route.midiBuffer = &buffer;
        playback.processAudio(frame, kBlock, &route, 1);
        for (size_t i = 0; i < buffer.getEventCount(); ++i) {
            const auto& event = buffer.getEvent(i);
            if ((event.data[0] & 0xF0) == 0x90 && event.data[1] > 0 && event.data[2] > 0) {
                hits.push_back({frame, event.data[1]});
            }
        }
        frame += kBlock;
    }
    return hits;
}

bool soundedAt(const std::vector<ObservedNoteOn>& hits, uint8_t pitch, double beat) {
    const uint64_t want = static_cast<uint64_t>(beat * kBeatFrames);
    constexpr uint64_t kTol = 256;
    for (const auto& hit : hits) {
        if (hit.pitch != pitch) {
            continue;
        }
        const uint64_t delta = hit.frame > want ? hit.frame - want : want - hit.frame;
        if (delta <= kTol) {
            return true;
        }
    }
    return false;
}

void refillFromTop(PatternPlaybackEngine& playback) {
    playback.refillWindow(0, kSampleRate, static_cast<uint64_t>(8.0 * kBeatFrames));
}

int failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "PASS: " << what << "\n";
    } else {
        std::cout << "FAIL: " << what << "\n";
        ++failures;
    }
}

struct Fixture {
    TimelineClock clock{120.0};
    TrackManager trackManager;
    UnitID unitId{0};
    PatternID patternId;
    PlaylistLaneID laneId;
    ClipInstanceID clipId;

    bool build() {
        unitId = trackManager.getUnitManager().createUnit("Reschedule Unit", UnitType::Sampler);
        if (unitId == 0) {
            return false;
        }
        auto& patternManager = trackManager.getPatternManager();
        patternId = patternManager.createPattern();
        auto* pattern = patternManager.getPattern(patternId);
        if (!pattern) {
            return false;
        }
        pattern->type = PatternSource::Type::Midi;
        pattern->name = "Reschedule Pattern";
        pattern->lengthBeats = 4.0;
        pattern->payload = MidiPayload{};
        auto& notes = std::get<MidiPayload>(pattern->payload).notes;
        notes.push_back(MidiNote{kPitchA, 0.5, 0.5, 1.0f, 0.0f, unitId});
        notes.push_back(MidiNote{kPitchB, 2.5, 0.5, 1.0f, 0.0f, unitId});

        auto& playlist = trackManager.getPlaylistModel();
        laneId = playlist.createLane("Reschedule Lane");
        ClipInstance clip;
        clip.patternId = patternId;
        clip.sourceId = patternId.value;
        clip.startBeat = 0.0;
        clip.durationBeats = 4.0;
        clip.sourceOffset = 0.0;
        clipId = playlist.addClip(laneId, clip);
        return clipId.isValid();
    }
};

} // namespace

int main() {
    {
        Fixture fx;
        if (!fx.build()) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        auto& playback = fx.trackManager.getPatternPlaybackEngine();
        auto& playlist = fx.trackManager.getPlaylistModel();

        fx.trackManager.play();
        check(playback.getActiveInstanceCount() == 1, "T-5: one clip schedules one instance at play()");

        // The pre-fix UI path mutated the model with no reschedule: the set
        // keeps stale bounds (here the whole clip) until something rebuilds it.
        playlist.splitClip(fx.clipId, 2.0);
        check(playback.getActiveInstanceCount() == 1,
              "T-5: split without reschedule leaves the stale single instance (documents the seam)");

        fx.trackManager.refreshTimelinePatternInstances();
        check(playback.getActiveInstanceCount() == 2,
              "T-5: reschedule after split picks up both halves");

        refillFromTop(playback);
        const auto hits = renderAllNoteOns(playback, 8.0, fx.unitId);
        check(soundedAt(hits, kPitchA, 0.5), "T-5: first-half note still sounds at beat 0.5 after reschedule");
        check(soundedAt(hits, kPitchB, 2.5), "T-5: second-half note still sounds at beat 2.5 after reschedule");
        fx.trackManager.stop();
    }

    {
        Fixture fx;
        if (!fx.build()) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        auto& playback = fx.trackManager.getPatternPlaybackEngine();
        auto& playlist = fx.trackManager.getPlaylistModel();

        fx.trackManager.play();
        playlist.removeClip(fx.clipId);
        fx.trackManager.refreshTimelinePatternInstances();
        check(playback.getActiveInstanceCount() == 0, "T-5: reschedule after delete drops the instance");

        refillFromTop(playback);
        const auto hits = renderAllNoteOns(playback, 8.0, fx.unitId);
        check(hits.empty(), "T-5: deleted clip renders silence after reschedule");
        fx.trackManager.stop();
    }

    {
        Fixture fx;
        if (!fx.build()) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        auto& playback = fx.trackManager.getPatternPlaybackEngine();
        auto& playlist = fx.trackManager.getPlaylistModel();

        // Muted at schedule time: nothing scheduled, transport silent.
        playlist.getLane(fx.laneId)->muted = true;
        fx.trackManager.play();
        refillFromTop(playback);
        check(renderAllNoteOns(playback, 8.0, fx.unitId).empty(), "T-4: lane muted at play() renders silence");

        // Unmute mid-playback: without a reschedule the lane stays absent
        // from the set (the reported bug); with it, notes return.
        playlist.getLane(fx.laneId)->muted = false;
        check(playback.getActiveInstanceCount() == 0,
              "T-4: unmute without reschedule leaves the stale empty set (documents the seam)");
        fx.trackManager.refreshTimelinePatternInstances();
        check(playback.getActiveInstanceCount() == 1, "T-4: reschedule after unmute re-admits the lane");

        refillFromTop(playback);
        const auto hits = renderAllNoteOns(playback, 8.0, fx.unitId);
        check(soundedAt(hits, kPitchA, 0.5), "T-4: unmuted lane sounds again after reschedule");

        // Muting mid-playback: reschedule drops the lane going forward.
        playlist.getLane(fx.laneId)->muted = true;
        fx.trackManager.refreshTimelinePatternInstances();
        refillFromTop(playback);
        check(renderAllNoteOns(playback, 8.0, fx.unitId).empty(), "T-4: muted lane renders silence after reschedule");
        fx.trackManager.stop();
    }

    {
        Fixture fx;
        if (!fx.build()) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        auto& playback = fx.trackManager.getPatternPlaybackEngine();
        auto& playlist = fx.trackManager.getPlaylistModel();

        // Trim the 4-beat clip to [0, 2): the beat-2.5 note falls outside.
        // Without a reschedule the stale [0, 4) instance keeps sounding it.
        fx.trackManager.play();
        playlist.setClipDuration(fx.clipId, 2.0);
        fx.trackManager.refreshTimelinePatternInstances();
        refillFromTop(playback);
        const auto hits = renderAllNoteOns(playback, 8.0, fx.unitId);
        check(soundedAt(hits, kPitchA, 0.5), "T-5: kept region still sounds after trim reschedule");
        check(!soundedAt(hits, kPitchB, 2.5), "T-5: trimmed-away note is silent after reschedule");
        fx.trackManager.stop();
    }

    {
        Fixture fx;
        if (!fx.build()) {
            std::cerr << "FAIL: fixture setup\n";
            return 1;
        }
        // Stopped transport: refresh is a safe no-op, so wiring it into
        // every edit path cannot disturb the stopped state.
        fx.trackManager.refreshTimelinePatternInstances();
        check(fx.trackManager.getPatternPlaybackEngine().getActiveInstanceCount() == 0,
              "refresh while stopped schedules nothing");
    }

    if (failures == 0) {
        std::cout << "timeline pattern reschedule passed\n";
    }
    return failures == 0 ? 0 : 1;
}
