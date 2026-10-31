// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Pins the sampler's velocity response (#462, FD-64): gain = (velocity/127)^2.
// Changing the curve changes the balance of every existing project, so a change
// must be a deliberate, announced decision, not a drift. This test fails on one.

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

// Peak of one centred note at the given velocity, rendered through the real plugin.
float renderPeak(uint8_t velocity) {
    constexpr uint32_t RATE = 48000;
    constexpr uint32_t FRAMES = 2048;

    SamplerPlugin sampler;
    require(sampler.initialize(RATE, FRAMES), "sampler initialization failed");
    // Constant (DC) source: the output level is the gain, with no phase or
    // interpolation ripple to blur the ratio.
    require(sampler.loadSampleData("velocity-dc", std::vector<float>(RATE, 0.5f), RATE, 1),
            "sample load failed");
    sampler.setEnvelope(0.001f, 0.001f, 1.0f, 0.1f);
    sampler.setRootMidiNote(60);
    sampler.activate();

    MidiBuffer midi;
    midi.addNoteOn(1, 60, velocity, 0);

    std::vector<float> left(FRAMES, 0.0f), right(FRAMES, 0.0f);
    float* outputs[] = {left.data(), right.data()};
    sampler.process(nullptr, outputs, 0, 2, FRAMES, &midi, nullptr);

    float peak = 0.0f;
    for (uint32_t i = 0; i < FRAMES; ++i) {
        require(std::isfinite(left[i]) && std::isfinite(right[i]), "render produced NaN or Inf");
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    }
    return peak;
}

} // namespace

int main() {
    const float ref = renderPeak(127);
    require(ref > 1.0e-3f, "full-velocity render was silent");

    for (int velocity : {100, 64, 32, 1}) {
        const float peak = renderPeak(static_cast<uint8_t>(velocity));
        const double expected = std::pow(velocity / 127.0, 2.0);
        const double actual = static_cast<double>(peak) / static_cast<double>(ref);
        std::cout << "vel " << velocity << " ratio " << actual << " expected " << expected << '\n';
        require(std::abs(actual - expected) <= 0.01 * expected + 1.0e-6,
                "velocity " + std::to_string(velocity) + " gain is off the squared curve");
    }

    std::cout << "[PASS] SamplerVelocityCurveTest\n";
    return 0;
}
