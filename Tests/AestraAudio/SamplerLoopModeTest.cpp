// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/PluginHost.h"
#include "Plugin/SamplerPlugin.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Aestra::Audio::MidiBuffer;
using Aestra::Audio::Plugins::SamplerPlugin;

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

std::vector<float> makeRamp(uint32_t frames) {
    std::vector<float> sample(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        sample[i] = 0.1f + 0.8f * static_cast<float>(i) / static_cast<float>(frames - 1);
    }
    return sample;
}

std::vector<float> renderLoop(SamplerPlugin::LoopMode mode, uint32_t sourceRate = 1000,
                              uint32_t engineRate = 1000, uint32_t outputFrames = 180) {
    constexpr uint32_t SAMPLE_FRAMES = 128;

    SamplerPlugin sampler;
    require(sampler.initialize(engineRate, outputFrames), "sampler initialization failed");
    require(sampler.loadSampleData("loop-mode-ramp", makeRamp(SAMPLE_FRAMES), sourceRate, 1),
            "ramp sample load failed");
    sampler.setEnvelope(0.001f, 0.001f, 1.0f, 0.1f);
    sampler.setSampleWindow(0.25f, 0.75f);
    sampler.setLoopMode(mode);
    sampler.setRootMidiNote(60);
    sampler.activate();

    MidiBuffer midi;
    midi.addNoteOn(1, 60, 127, 0);

    std::vector<float> left(outputFrames, 0.0f);
    std::vector<float> right(outputFrames, 0.0f);
    float* outputs[] = {left.data(), right.data()};
    sampler.process(nullptr, outputs, 0, 2, outputFrames, &midi, nullptr);

    for (uint32_t i = 0; i < outputFrames; ++i) {
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "loop render produced NaN or Inf");
        require(std::abs(left[i] - right[i]) < 1.0e-6f, "mono loop render lost stereo parity");
    }
    return left;
}

double linearSlope(const std::vector<float>& values, size_t begin, size_t end) {
    require(begin < end && end <= values.size(), "invalid slope window");
    const double count = static_cast<double>(end - begin);
    const double meanX = static_cast<double>(begin + end - 1) * 0.5;
    double meanY = 0.0;
    for (size_t i = begin; i < end; ++i) {
        meanY += values[i];
    }
    meanY /= count;

    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = begin; i < end; ++i) {
        const double dx = static_cast<double>(i) - meanX;
        numerator += dx * (static_cast<double>(values[i]) - meanY);
        denominator += dx * dx;
    }
    return denominator > 0.0 ? numerator / denominator : 0.0;
}

std::vector<uint8_t> stateBytes(const std::string& json) {
    return std::vector<uint8_t>(json.begin(), json.end());
}

} // namespace

int main() {
    const auto forward = renderLoop(SamplerPlugin::LoopMode::Forward);
    const auto pingPong = renderLoop(SamplerPlugin::LoopMode::PingPong);

    require(linearSlope(forward, 12, 48) > 3.0e-4, "forward loop did not traverse toward its end");
    require(linearSlope(forward, 78, 112) > 3.0e-4, "forward loop did not wrap and traverse forward again");

    require(linearSlope(pingPong, 12, 48) > 3.0e-4, "bidirectional loop did not traverse forward first");
    require(linearSlope(pingPong, 78, 112) < -3.0e-4, "bidirectional loop did not reverse at its end");
    require(linearSlope(pingPong, 142, 174) > 3.0e-4, "bidirectional loop did not reverse again at its start");

    // A source/engine ratio of 131 crosses more than two 63-frame loop spans
    // per output frame. The folded overshoot must still traverse both ways
    // without an unbounded correction loop or invalid output.
    constexpr uint32_t EXTREME_SOURCE_RATE = 131000;
    constexpr uint32_t EXTREME_ENGINE_RATE = 1000;
    constexpr uint32_t EXTREME_OUTPUT_FRAMES = 80;
    const auto extremePingPong = renderLoop(SamplerPlugin::LoopMode::PingPong, EXTREME_SOURCE_RATE,
                                            EXTREME_ENGINE_RATE, EXTREME_OUTPUT_FRAMES);
    require(linearSlope(extremePingPong, 4, 12) > 3.0e-4,
            "extreme-rate bidirectional loop did not traverse forward");
    require(linearSlope(extremePingPong, 18, 26) < -3.0e-4,
            "extreme-rate bidirectional loop did not reverse from its end");
    require(linearSlope(extremePingPong, 32, 40) > 3.0e-4,
            "extreme-rate bidirectional loop did not reverse from its start");
    require(std::any_of(extremePingPong.begin(), extremePingPong.end(),
                        [](float value) { return std::abs(value) > 1.0e-5f; }),
            "extreme-rate bidirectional loop produced silence");

    SamplerPlugin saved;
    saved.setLoopMode(SamplerPlugin::LoopMode::PingPong);
    SamplerPlugin restored;
    require(restored.loadState(saved.saveState()), "saved loop-mode state failed to load");
    require(restored.getLoopMode() == SamplerPlugin::LoopMode::PingPong, "saved bidirectional mode was not restored");

    SamplerPlugin legacyForward;
    require(legacyForward.loadState(stateBytes(R"({"loopEnabled":true})")), "legacy enabled-loop state failed to load");
    require(legacyForward.getLoopMode() == SamplerPlugin::LoopMode::Forward,
            "legacy enabled-loop state did not map to forward mode");

    SamplerPlugin legacyOneShot;
    require(legacyOneShot.loadState(stateBytes(R"({"loopEnabled":false})")), "legacy one-shot state failed to load");
    require(legacyOneShot.getLoopMode() == SamplerPlugin::LoopMode::OneShot,
            "legacy disabled-loop state did not map to one-shot mode");

    std::cout << "[PASS] SamplerLoopModeTest\n";
    return 0;
}
