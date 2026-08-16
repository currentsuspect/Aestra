// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Plugin/SamplerPlugin.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"

#include <cstdint>
#include <iostream>
#include <memory>

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

bool hasNoteOff(MidiBuffer& buffer, uint8_t pitch) {
    for (size_t i = 0; i < buffer.getEventCount(); ++i) {
        const auto& event = buffer.getEvent(i);
        if ((event.data[0] & 0xF0) == 0x80 && event.data[1] == pitch) {
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
    playback.rewindScheduledInstances();
    playback.refillWindow(0, sampleRate, sampleRate * 3);

    MidiBuffer midiAtOldTempoFrame;
    PatternPlaybackEngine::UnitMidiRoute route{unitId, &midiAtOldTempoFrame};
    playback.processAudio(sampleRate, 128, &route, 1);
    if (hasNoteOn(midiAtOldTempoFrame, pitch)) {
        std::cerr << "stale note-on fired at old 120 BPM frame after tempo rewind\n";
        return 1;
    }

    MidiBuffer midiAtNewTempoFrame;
    route.midiBuffer = &midiAtNewTempoFrame;
    playback.processAudio(sampleRate * 2, 128, &route, 1);
    if (!hasNoteOn(midiAtNewTempoFrame, pitch)) {
        std::cerr << "note-on did not fire at recomputed 60 BPM frame\n";
        return 1;
    }

    // A normal one-shot sampler still needs the Piano Roll note-off: the
    // sampler uses it to enter ADSR release before the sample reaches its
    // natural endpoint. This used to be suppressed for UnitType::Sampler.
    PatternManager gatePatternManager;
    const UnitID gateUnitId = unitManager.createUnit("One-Shot Gate Unit", UnitType::Sampler);
    auto gateSampler = std::make_shared<Plugins::SamplerPlugin>();
    unitManager.attachPlugin(gateUnitId, "com.Aestrastudios.sampler", gateSampler);
    PatternID gatePatternId = gatePatternManager.createPattern();
    auto* gatePattern = gatePatternManager.getPattern(gatePatternId);
    if (!gatePattern) {
        std::cerr << "failed to create one-shot gate pattern\n";
        return 1;
    }
    gatePattern->type = PatternSource::Type::Midi;
    gatePattern->lengthBeats = 4.0;
    gatePattern->payload = MidiPayload{};
    std::get<MidiPayload>(gatePattern->payload).notes.push_back(
        MidiNote{pitch, 0.0, 0.5, 1.0f, 0.0f, gateUnitId});

    clock.setTempo(120.0);
    PatternPlaybackEngine gatePlayback(&clock, &gatePatternManager, &unitManager);
    gatePlayback.schedulePatternInstance(gatePatternId, 0.0, 2);
    gatePlayback.refillWindow(0, sampleRate, sampleRate * 2);

    MidiBuffer gateOn;
    PatternPlaybackEngine::UnitMidiRoute gateRoute{gateUnitId, &gateOn};
    gatePlayback.processAudio(0, 128, &gateRoute, 1);
    if (!hasNoteOn(gateOn, pitch)) {
        std::cerr << "one-shot gate test did not emit note-on\n";
        return 1;
    }

    MidiBuffer gateOff;
    gateRoute.midiBuffer = &gateOff;
    gatePlayback.processAudio(12000, 128, &gateRoute, 1);
    if (!hasNoteOff(gateOff, pitch)) {
        std::cerr << "one-shot sampler note-off was suppressed before ADSR release\n";
        return 1;
    }

    std::cout << "pattern playback tempo change reschedule passed\n";
    return 0;
}
