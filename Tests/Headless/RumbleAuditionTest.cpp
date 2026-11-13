// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// SPEC-025 R6 (first part): Rumble's editor auditions a picked sound with the engine's AuditionUnit, as the Sample
// Editor's Preview does, because the engine only runs an instrument that has something to play. Checks, through a
// real AudioEngine with the transport stopped: the short key press the editor sends sounds the 808, and the
// one-shot rings on after the key is up, as it does when played from a key.

#include "Core/AudioEngine.h"
#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"
#include "RumbleInstance.h"
#include "RumblePresetBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace Aestra::Audio;
    constexpr uint32_t sampleRate = 48000, blockSize = 256, channels = 2;

    UnitManager unitManager;
    PatternManager patternManager;
    TimelineClock clock(120.0);
    PatternPlaybackEngine playbackEngine(&clock, &patternManager, &unitManager);

    auto rumble = std::make_shared<Aestra::Plugins::RumbleInstance>();
    rumble->setSound(Aestra::Plugins::rumbleSoundOf(Aestra::Plugins::kRumbleFactoryPresets[3])); // Long tail
    require(rumble->initialize(sampleRate, blockSize), "Rumble failed to initialize");
    rumble->activate();
    require(rumble->waitForLevels(30000), "levels found");
    const UnitID unitId = unitManager.createUnit("Rumble", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.rumble", rumble);
    unitManager.setUnitEnabled(unitId, true);
    unitManager.setUnitMixerChannel(unitId, MASTER_MIXER_CHANNEL_ID);
    unitManager.captureUnitPluginState(unitId);

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine failed to initialize");
    engine.setSampleRate(sampleRate);
    engine.setBufferConfig(blockSize, channels);
    engine.setBPM(120.0f);
    engine.setUnitManager(&unitManager);
    engine.setPatternPlaybackEngine(&playbackEngine);
    engine.setPatternPlaybackMode(true, 2.0);

    std::vector<float> out(blockSize * channels, 0.0f);
    auto peakOver = [&](double seconds) {
        float peak = 0.0f;
        const uint32_t blocks = static_cast<uint32_t>(seconds * sampleRate / blockSize);
        for (uint32_t b = 0; b < blocks; ++b) {
            std::fill(out.begin(), out.end(), 0.0f);
            engine.processBlock(out.data(), nullptr, blockSize, 0.0);
            for (float v : out) peak = std::max(peak, std::fabs(v));
        }
        return peak;
    };
    require(peakOver(0.1) < 1.0e-6f, "silent before the audition");

    // What RumblePluginEditor's audition sink sends: key C3, full velocity, a tenth of a second.
    AudioQueueCommand cmd;
    cmd.type = AudioQueueCommandType::AuditionUnit;
    cmd.trackIndex = static_cast<uint32_t>(unitId);
    cmd.value1 = 60.0f;
    cmd.value2 = 1.0f;
    cmd.samplePos = static_cast<uint64_t>(0.1 * sampleRate);
    engine.commandQueue().push(cmd);

    const float front = peakOver(0.1);
    require(front > 0.2f, "the audition sounds the 808");
    std::printf("[PASS] audition sounds (peak %.3f)\n", front);
    peakOver(0.3);
    const float ringing = peakOver(0.2);
    require(ringing > 0.02f, "the one-shot rings on after the key is up");
    std::printf("[PASS] still ringing between 0.5 and 0.7 s (peak %.3f)\n", ringing);
    return 0;
}
