// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// UnitAuditionLengthTest
// The Sample Editor's Preview sounds the unit through AuditionUnit for the length of the trimmed sample, and a
// second press stops it. Before, every audition was a fixed eighth of a second (the piano roll's click), so an
// editor could not let you hear a sample through. Checks: no length keeps the click; a length keeps sounding;
// a negative note stops the unit's audition early.

#include "Core/AudioEngine.h"
#include "Models/PatternManager.h"
#include "Models/UnitManager.h"
#include "Playback/PatternPlaybackEngine.h"
#include "Playback/TimelineClock.h"
#include "Plugin/SamplerPlugin.h"

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
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 256;
    constexpr uint32_t channels = 2;

    UnitManager unitManager;
    PatternManager patternManager;
    TimelineClock clock(120.0);
    PatternPlaybackEngine playbackEngine(&clock, &patternManager, &unitManager);

    // A 2 s sustained tone, full sustain and a short release: what you hear is how long the note is held.
    auto sampler = std::make_shared<Plugins::SamplerPlugin>();
    require(sampler->initialize(sampleRate, blockSize), "SamplerPlugin failed to initialize");
    sampler->activate();
    sampler->setEnvelope(0.002f, 0.01f, 1.0f, 0.01f);
    {
        std::vector<float> data(sampleRate * 2, 0.0f);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = 0.5f * static_cast<float>(
                                 std::sin(2.0 * 3.14159265358979 * 220.0 * static_cast<double>(i) / sampleRate));
        }
        require(sampler->loadSampleData("audition-length-tone", std::move(data), sampleRate, 1),
                "loadSampleData failed");
    }
    const UnitID unitId = unitManager.createUnit("Preview Sampler", UnitGroup::Synth);
    unitManager.attachPlugin(unitId, "com.Aestrastudios.sampler", sampler);
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
    // Peak of the output over the next `seconds`.
    auto peakOver = [&](double seconds) {
        float peak = 0.0f;
        const uint32_t blocks = static_cast<uint32_t>(seconds * sampleRate / blockSize);
        for (uint32_t b = 0; b < blocks; ++b) {
            std::fill(out.begin(), out.end(), 0.0f);
            engine.processBlock(out.data(), nullptr, blockSize, 0.0);
            for (float v : out)
                peak = std::max(peak, std::fabs(v));
        }
        return peak;
    };
    auto audition = [&](float note, double seconds) {
        AudioQueueCommand cmd;
        cmd.type = AudioQueueCommandType::AuditionUnit;
        cmd.trackIndex = static_cast<uint32_t>(unitId);
        cmd.value1 = note;
        cmd.value2 = 100.0f / 127.0f;
        cmd.samplePos = static_cast<uint64_t>(seconds * sampleRate);
        engine.commandQueue().push(cmd);
    };
    peakOver(0.1);

    // 1. No length: the piano roll's click, gone well before half a second.
    audition(60.0f, 0.0);
    require(peakOver(0.1) > 0.02f, "a click sounds");
    peakOver(0.15); // its eighth of a second and the release
    const float clickLater = peakOver(0.25);
    require(clickLater < 0.01f, "a click without a length still stops after an eighth of a second");
    std::printf("[PASS] no length: an eighth-second click (peak from 0.25 s: %.4f)\n", clickLater);
    peakOver(0.3);

    // 2. A length: Preview keeps sounding through it.
    audition(60.0f, 1.5);
    peakOver(0.5); // well past a click's eighth of a second and its release
    const float held = peakOver(0.2);
    require(held > 0.02f, "a preview with a length is still sounding between 0.5 and 0.7 s");
    std::printf("[PASS] 1.5 s preview: still sounding between 0.5 and 0.7 s (peak %.3f)\n", held);
    peakOver(1.0);
    require(peakOver(0.3) < 0.01f, "and it ends after its length");
    std::printf("[PASS] it ends after its length\n");

    // 3. Pressed again: a negative note stops the unit's audition early.
    audition(60.0f, 1.5);
    require(peakOver(0.2) > 0.02f, "the preview starts");
    audition(-1.0f, 0.0);
    peakOver(0.1);
    const float afterStop = peakOver(0.3);
    require(afterStop < 0.01f, "stop silences the preview long before its length");
    std::printf("[PASS] stop: silent 0.1 s after the second press (peak %.4f)\n", afterStop);
    return 0;
}
