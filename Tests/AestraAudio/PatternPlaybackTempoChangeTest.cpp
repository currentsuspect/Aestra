// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>

using namespace Aestra::Audio;

namespace {

bool hasNoteOn(MidiBuffer& buffer, uint8_t pitch) {
    for (size_t i = 0; i < buffer.getEventCount(); ++i) {
        const auto& event = buffer.getEvent(i);
        if ((event.data[0] & 0xF0) == 0x90 && event.data[1] == pitch && event.data[2] > 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    constexpr int sampleRate = 48000;
    constexpr UnitID unitId = 1;
    constexpr uint8_t pitch = 60;

    TimelineClock clock(120.0);
    PatternManager patternManager;
    UnitManager unitManager;
    const UnitID createdUnit = unitManager.createUnit("Tempo Test Unit", UnitType::Sampler);
    if (createdUnit != unitId) {
        std::cerr << "unexpected unit id " << createdUnit << "\n";
        return 1;
    }

    PatternID patternId = patternManager.createPattern();
    auto* pattern = patternManager.getPattern(patternId);
    if (!pattern) {
        std::cerr << "failed to create pattern\n";
        return 1;
    }
    pattern->type = PatternSource::Type::Midi;
    pattern->name = "Tempo Change Pattern";
    pattern->lengthBeats = 4.0;
    pattern->payload = MidiPayload{};
    auto& notes = std::get<MidiPayload>(pattern->payload).notes;
    notes.push_back(MidiNote{pitch, 2.0, 0.5, 1.0f, 0.0f, unitId});

    PatternPlaybackEngine playback(&clock, &patternManager, &unitManager);
    playback.schedulePatternInstance(patternId, 0.0, 1);

    playback.refillWindow(0, sampleRate, sampleRate * 3);

    clock.setTempo(60.0);
    playback.flush();
    playback.refillWindow(0, sampleRate, sampleRate * 3);

    MidiBuffer midiAtOldTempoFrame;
    PatternPlaybackEngine::UnitMidiRoute route{unitId, &midiAtOldTempoFrame};
    playback.processAudio(sampleRate, 128, &route, 1);
    if (hasNoteOn(midiAtOldTempoFrame, pitch)) {
        std::cerr << "stale note-on fired at old 120 BPM frame after tempo flush\n";
        return 1;
    }

    MidiBuffer midiAtNewTempoFrame;
    route.midiBuffer = &midiAtNewTempoFrame;
    playback.processAudio(sampleRate * 2, 128, &route, 1);
    if (!hasNoteOn(midiAtNewTempoFrame, pitch)) {
        std::cerr << "note-on did not fire at recomputed 60 BPM frame\n";
        return 1;
    }

    std::cout << "pattern playback tempo change reschedule passed\n";
    return 0;
}
