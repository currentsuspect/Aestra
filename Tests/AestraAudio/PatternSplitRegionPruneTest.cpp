// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <cmath>
#include <iostream>

using namespace Aestra::Audio;

namespace {

const MidiNote* findNote(const std::vector<MidiNote>& notes, double startBeat) {
    for (const auto& note : notes) {
        if (std::abs(note.startBeat - startBeat) < 0.001) {
            return &note;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    TrackManager trackManager;
    auto& unitManager = trackManager.getUnitManager();
    const UnitID unitKick = unitManager.createUnit("Kick", UnitType::Sampler);
    const UnitID unitHat = unitManager.createUnit("Hat", UnitType::Sampler);
    const UnitID unitBass = unitManager.createUnit("Bass", UnitType::Sampler);
    if (unitKick == 0 || unitHat == 0 || unitBass == 0) {
        std::cerr << "failed to create units\n";
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
    pattern->name = "Main Pattern";
    pattern->lengthBeats = 4.0;
    pattern->payload = MidiPayload{};
    auto& notes = pattern->getMidiNotes();
    notes.push_back(MidiNote{36, 0.0, 0.5, 1.0f, 0.0f, unitKick});   // kick in region 1
    notes.push_back(MidiNote{42, 0.5, 0.25, 1.0f, 0.0f, unitHat});   // hat in region 1
    notes.push_back(MidiNote{36, 1.0, 0.5, 1.0f, 0.0f, unitKick});   // kick in region 1
    notes.push_back(MidiNote{42, 1.5, 0.25, 1.0f, 0.0f, unitHat});   // hat in region 1
    notes.push_back(MidiNote{45, 1.75, 0.5, 1.0f, 0.0f, unitBass});  // bass straddles the cut at 2.0
    notes.push_back(MidiNote{36, 3.5, 0.5, 1.0f, 0.0f, unitKick});   // kick in region 2

    auto& playlist = trackManager.getPlaylistModel();
    PlaylistLaneID laneId = playlist.createLane("Slice Lane");
    ClipInstance clip;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 4.0;
    clip.sourceOffset = 0.0;
    ClipInstanceID clipId = playlist.addClip(laneId, clip);

    ClipInstanceID secondId = playlist.splitClip(clipId, 2.0);
    if (!secondId.isValid()) {
        std::cerr << "FAIL: splitClip returned an invalid id\n";
        return 1;
    }

    // First half keeps the original pattern, untouched.
    ClipInstance* first = playlist.getClip(clipId);
    if (!first || first->patternId != patternId || first->sourceOffset != 0.0 || first->durationBeats != 2.0) {
        std::cerr << "FAIL: first half mismatch\n";
        return 1;
    }
    if (notes.size() != 6) {
        std::cerr << "FAIL: original pattern was mutated by the split (note count " << notes.size() << ")\n";
        return 1;
    }

    // Second half: rebased, region-only clone.
    ClipInstance* second = playlist.getClip(secondId);
    if (!second) {
        std::cerr << "FAIL: cannot read second clip\n";
        return 1;
    }
    if (second->patternId == patternId) {
        std::cerr << "FAIL: second half still references the original pattern\n";
        return 1;
    }
    if (second->sourceOffset != 0.0) {
        std::cerr << "FAIL: second half sourceOffset=" << second->sourceOffset << " (expected 0 after rebase)\n";
        return 1;
    }
    if (second->durationBeats != 2.0) {
        std::cerr << "FAIL: second half durationBeats=" << second->durationBeats << " (expected 2.0)\n";
        return 1;
    }
    // The scheduler anchors the instance at patternStartBeat() (= startBeat -
    // sourceOffset); a rebased second half must satisfy startBeat - sourceOffset
    // == startBeat, i.e. the scheduler anchor equals the clip's timeline start.
    if (std::abs(second->startBeat - second->sourceOffset - second->startBeat) > 0.001) {
        std::cerr << "FAIL: startBeat - sourceOffset=" << (second->startBeat - second->sourceOffset)
                  << " != startBeat=" << second->startBeat << " (scheduler anchor invariant)\n";
        return 1;
    }

    auto* cloned = patternManager.getPattern(second->patternId);
    if (!cloned || !cloned->isMidi()) {
        std::cerr << "FAIL: second half pattern is not a valid MIDI pattern\n";
        return 1;
    }
    if (std::abs(cloned->lengthBeats - 2.0) > 0.001) {
        std::cerr << "FAIL: cloned pattern lengthBeats=" << cloned->lengthBeats << " (expected 2.0)\n";
        return 1;
    }
    const auto& clonedNotes = cloned->getMidiNotes();
    if (clonedNotes.size() != 2) {
        std::cerr << "FAIL: cloned pattern has " << clonedNotes.size()
                  << " notes (expected 2 — ghost units must be pruned)\n";
        return 1;
    }
    const MidiNote* bass = findNote(clonedNotes, 0.0);
    if (!bass || std::abs(bass->durationBeats - 0.25) > 0.001) {
        std::cerr << "FAIL: straddling bass note not re-anchored at the cut (start 0.0, tail 0.25)\n";
        return 1;
    }
    const MidiNote* tail = findNote(clonedNotes, 1.5);
    if (!tail || std::abs(tail->durationBeats - 0.5) > 0.001) {
        std::cerr << "FAIL: region-2 kick note not rebased to local origin (1.5)\n";
        return 1;
    }

    std::cout << "pattern split region prune passed\n";
    return 0;
}
