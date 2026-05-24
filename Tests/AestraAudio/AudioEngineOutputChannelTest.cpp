// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AudioEngine.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;

namespace {

void testMonoOutputDoesNotWriteStereoStride() {
    constexpr uint32_t kFrames = 128;
    constexpr float kSentinel = 12345.0f;

    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(kFrames, 1);
    engine.setTestToneEnabled(true);

    std::vector<float> output(kFrames * 2, kSentinel);
    assert(engine.processBlock(output.data(), nullptr, kFrames, 0.0) == 0);

    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        assert(std::isfinite(output[frame]));
    }
    for (size_t i = kFrames; i < output.size(); ++i) {
        assert(output[i] == kSentinel);
    }

    std::printf("[PASS] mono output uses mono stride without guard overwrite\n");
}

void testExtraOutputChannelsAreCleared() {
    constexpr uint32_t kFrames = 64;
    constexpr uint32_t kChannels = 4;

    AudioEngine engine;
    engine.setSampleRate(48000);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setTestToneEnabled(true);

    std::vector<float> output(static_cast<size_t>(kFrames) * kChannels, 1.0f);
    assert(engine.processBlock(output.data(), nullptr, kFrames, 0.0) == 0);

    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const size_t base = static_cast<size_t>(frame) * kChannels;
        assert(std::isfinite(output[base]));
        assert(std::isfinite(output[base + 1]));
        assert(output[base + 2] == 0.0f);
        assert(output[base + 3] == 0.0f);
    }

    std::printf("[PASS] extra output channels are cleared\n");
}

} // namespace

int main() {
    testMonoOutputDoesNotWriteStereoStride();
    testExtraOutputChannelsAreCleared();
    return 0;
}
