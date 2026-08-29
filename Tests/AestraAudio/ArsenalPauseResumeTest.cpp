// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ArsenalPauseResumeTest — pause in pattern mode must preserve the playhead.
//
// Pause hard-cuts active voices (one-shot workflow) but must NOT rewind:
// playPatternInArsenal() resumes from the stored position by design, so the
// pause path keeps the playhead via the #590 preserve-sentinel (the engine's
// own authoritative position). Routing pause through stop() would trigger the
// T-8 single-stop reset (land at 0) and break resume.

#include "Core/AudioCommandQueue.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <iostream>

using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;
using Aestra::Audio::kTransportPreservePosition;
using Aestra::Audio::MidiPayload;
using Aestra::Audio::PatternID;
using Aestra::Audio::PatternSource;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

struct CommandLog {
    std::vector<AudioQueueCommand> commands;
};

} // namespace

int main() {
    constexpr double kSampleRate = 48000.0;

    TrackManager tm;
    tm.setOutputSampleRate(kSampleRate);

    CommandLog log;
    tm.setCommandSink([&log](const AudioQueueCommand& cmd) { log.commands.push_back(cmd); });

    auto& patternManager = tm.getPatternManager();
    PatternID patternId = patternManager.createPattern();
    if (!patternId.isValid()) {
        std::cerr << "failed to create pattern\n";
        return 1;
    }
    auto* pattern = patternManager.getPattern(patternId);
    if (!pattern) {
        std::cerr << "failed to read pattern\n";
        return 1;
    }
    pattern->type = PatternSource::Type::Midi;
    pattern->lengthBeats = 4.0;
    pattern->payload = MidiPayload{};

    // Play from the cue at 1.0 s.
    tm.setPosition(1.0);
    tm.playPatternInArsenal(patternId);

    // Simulate the UI-frame position sync while pattern playback advances: the
    // playhead is now at 3.0 s.
    tm.setPosition(3.0);

    // Pause: hard-cut voices, preserve the playhead.
    tm.pauseArsenalPlayback();
    // The stop command must carry the #590 preserve-sentinel — never the
    // paused position (which the UI cache can lag) and never zero (T-8's
    // single-stop reset is stop's semantic, not pause's). The engine keeps
    // its own authoritative playhead.
    const uint64_t pausedSamples = static_cast<uint64_t>(3.0 * kSampleRate);
    bool pauseCarriesSentinel = false;
    for (const auto& cmd : log.commands) {
        if (cmd.type == AudioQueueCommandType::SetTransportState && cmd.value1 == 0.0f) {
            pauseCarriesSentinel =
                (cmd.samplePos == kTransportPreservePosition && cmd.samplePos != pausedSamples);
        }
    }
    check(pauseCarriesSentinel, "pause command carries the preserve sentinel, not a position");

    check(tm.getPosition() == 3.0, "m_position stays at the paused playhead");
    check(tm.isPlaying() == false, "transport is stopped after pause");
    check(tm.getPlayStartPosition() == 1.0, "pause does not move the cue (stop's reset is stop's own)");

    // Play again: must resume from the stored playhead, not beat zero and not
    // the original cue.
    log.commands.clear();
    tm.playPatternInArsenal(patternId);
    bool playCarriesPausedPosition = false;
    for (const auto& cmd : log.commands) {
        if (cmd.type == AudioQueueCommandType::SetTransportState && cmd.value1 != 0.0f) {
            playCarriesPausedPosition = (cmd.samplePos == pausedSamples);
        }
    }
    check(playCarriesPausedPosition, "play after pause resumes from the stored playhead (3.0 s)");

    if (g_failures > 0) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "arsenal pause/resume passed\n";
    return 0;
}
