// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

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

struct ObservedNoteOn {
    uint64_t frame;
    uint8_t pitch;
};

std::vector<ObservedNoteOn> collectAllNoteOns(PatternPlaybackEngine& playback, int sampleRate, double beats,
                                              UnitID unitId) {
    PatternPlaybackEngine::UnitMidiRoute route{unitId, nullptr};
    std::vector<ObservedNoteOn> hits;
    constexpr uint32_t kBlock = 128;
    const uint64_t totalFrames = static_cast<uint64_t>(beats * (sampleRate * 60.0 / 120.0));
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

bool hasNoteNear(const std::vector<ObservedNoteOn>& hits, uint8_t pitch, uint64_t nearFrame, uint64_t tolerance) {
    for (const auto& hit : hits) {
        if (hit.pitch != pitch) {
            continue;
        }
        const uint64_t delta = hit.frame > nearFrame ? hit.frame - nearFrame : nearFrame - hit.frame;
        if (delta <= tolerance) {
            return true;
        }
    }
    return false;
}

bool hasNoteInsideWindow(const std::vector<ObservedNoteOn>& hits, uint8_t pitch, uint64_t centerFrame,
                         uint64_t tolerance) {
    for (const auto& hit : hits) {
        if (hit.pitch != pitch) {
            continue;
        }
        const uint64_t delta = hit.frame > centerFrame ? hit.frame - centerFrame : centerFrame - hit.frame;
        if (delta <= tolerance) {
            std::cerr << "pitch " << static_cast<int>(pitch) << " note-on at frame " << hit.frame
                      << " — inside rejected window around " << centerFrame << "\n";
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    constexpr int sampleRate = 48000;
    constexpr double kBeatFrames = 24000.0; // 120 BPM
    constexpr uint8_t pitchInFirstRegion = 60;
    constexpr uint8_t pitchInSecondRegion = 62;

    TimelineClock clock(120.0);
    TrackManager trackManager;
    auto& unitManager = trackManager.getUnitManager();
    const UnitID unitId = unitManager.createUnit("Slice Unit", UnitType::Sampler);
    if (unitId == 0) {
        std::cerr << "failed to create unit\n";
        return 1;
    }

    auto& patternManager = trackManager.getPatternManager();
    PatternID patternId = patternManager.createPattern();
    auto* pattern = patternManager.getPattern(patternId);
    if (!pattern) {
        std::cerr << "failed to create pattern\n";
        return 1;
    }
    pattern->type = PatternSource::Type::Midi;
    pattern->name = "Slice Pattern";
    pattern->lengthBeats = 4.0;
    pattern->payload = MidiPayload{};
    auto& notes = std::get<MidiPayload>(pattern->payload).notes;
    notes.push_back(MidiNote{pitchInFirstRegion, 0.5, 0.5, 1.0f, 0.0f, unitId});
    notes.push_back(MidiNote{pitchInSecondRegion, 2.5, 0.5, 1.0f, 0.0f, unitId});

    auto& playlist = trackManager.getPlaylistModel();
    PlaylistLaneID laneId = playlist.createLane("Slice Lane");
    ClipInstance clip;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    clip.sourceOffset = 0.0;
    ClipInstanceID clipId = playlist.addClip(laneId, clip);
    if (!clipId.isValid()) {
        std::cerr << "failed to add pattern clip\n";
        return 1;
    }

    // Split at beat 2: the second half must start at timeline beat 2 and play
    // the pattern region [2, 4) — not restart the original pattern region.
    ClipInstanceID secondId = playlist.splitClip(clipId, 2.0);
    if (!secondId.isValid()) {
        std::cerr << "failed to split pattern clip\n";
        return 1;
    }

    auto& playback = trackManager.getPatternPlaybackEngine();
    playback.clearScheduledInstances();
    trackManager.play();
    playback.refillWindow(0, sampleRate, static_cast<uint64_t>(8.0 * kBeatFrames));

    const uint64_t tolerance = 128;
    const auto hits = collectAllNoteOns(playback, sampleRate, 8.0, unitId);
    // First half: note at pattern beat 0.5 fires at timeline beat 0.5.
    if (!hasNoteNear(hits, pitchInFirstRegion, static_cast<uint64_t>(0.5 * kBeatFrames), tolerance)) {
        std::cerr << "FAIL: first half note not placed at beat 0.5\n";
        return 1;
    }
    // Second half: note at pattern beat 2.5 must fire at timeline beat
    // 2 + 2.5 = 4.5 (correct), never at 2.5 (bug: second slice was anchored at
    // the original clip start instead of the split point).
    if (!hasNoteNear(hits, pitchInSecondRegion, static_cast<uint64_t>(4.5 * kBeatFrames), tolerance)) {
        std::cerr << "FAIL: second half note not placed at beat 4.5\n";
        return 1;
    }
    if (hasNoteInsideWindow(hits, pitchInSecondRegion, static_cast<uint64_t>(2.5 * kBeatFrames), tolerance)) {
        std::cerr << "FAIL: second half note fired at the unsliced pattern position (beat 2.5)\n";
        return 1;
    }

    std::cout << "pattern slice scheduling passed\n";
    return 0;
}
