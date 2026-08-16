// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ArsenalPauseResumeTest — pause in pattern mode must preserve the playhead.
//
// Pause hard-cuts active voices (one-shot workflow) but must NOT rewind:
// playPatternInArsenal() resumes from the stored position by design, so the
// pause path only needs to keep the cue at the paused position. The cue must
// be moved BEFORE the stop command goes out — the audio thread's drain is
// authoritative, so storing after the fact races it (the hard-stop rewind
// follows the same rule).

#include "Core/AudioCommandQueue.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <iostream>

using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;
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

    // The stop command must carry the PAUSED position (3.0 s), never the
    // original cue (1.0 s) and never zero — the engine drain is authoritative.
    const uint64_t pausedSamples = static_cast<uint64_t>(3.0 * kSampleRate);
    bool stopCarriesPausedPosition = false;
    for (const auto& cmd : log.commands) {
        if (cmd.type == AudioQueueCommandType::SetTransportState && cmd.value1 == 0.0f) {
            stopCarriesPausedPosition = (cmd.samplePos == pausedSamples);
        }
    }
    check(stopCarriesPausedPosition, "pause stop command carries the paused position (3.0 s)");

    check(tm.getPosition() == 3.0, "m_position stays at the paused playhead");
    check(tm.isPlaying() == false, "transport is stopped after pause");
    check(tm.getPlayStartPosition() == 3.0, "cue is the paused playhead (stop-after-resume returns here)");

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
