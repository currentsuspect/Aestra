// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/AestraDrift.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraDrift;

namespace {

void writeFloat(std::vector<uint8_t>& bytes, size_t offset, float value) {
    assert(offset + sizeof(float) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

void testNanStateDoesNotEnterParametersOrProcessing() {
    AestraDrift drift;
    assert(drift.initialize(48000.0, 64));
    drift.setParameter(AestraDrift::kPitch, 0.25f);

    std::vector<uint8_t> state = drift.saveState();
    assert(state.size() >= sizeof(uint32_t) * 2 + sizeof(float) * AestraDrift::kParamCount);

    constexpr size_t kParamOffset = sizeof(uint32_t) * 2;
    writeFloat(state, kParamOffset + sizeof(float) * AestraDrift::kPitch, std::numeric_limits<float>::quiet_NaN());
    writeFloat(state, kParamOffset + sizeof(float) * AestraDrift::kMix, 0.5f);

    AestraDrift restored;
    assert(restored.initialize(48000.0, 64));
    assert(restored.loadState(state));
    assert(restored.getParameter(AestraDrift::kPitch) == 0.5f);
    assert(restored.getParameter(AestraDrift::kMix) == 0.5f);

    restored.activate();
    float inL[64] = {};
    float inR[64] = {};
    float outL[64] = {};
    float outR[64] = {};
    const float* inputs[2] = {inL, inR};
    float* outputs[2] = {outL, outR};

    restored.process(inputs, outputs, 2, 2, 64, nullptr, nullptr);
    for (uint32_t i = 0; i < 64; ++i) {
        assert(std::isfinite(outL[i]));
        assert(std::isfinite(outR[i]));
    }
}

void testInfStateDoesNotEnterParametersOrProcessing() {
    AestraDrift drift;
    assert(drift.initialize(48000.0, 64));
    drift.setParameter(AestraDrift::kPitch, 0.25f);

    std::vector<uint8_t> state = drift.saveState();
    assert(state.size() >= sizeof(uint32_t) * 2 + sizeof(float) * AestraDrift::kParamCount);

    constexpr size_t kParamOffset = sizeof(uint32_t) * 2;
    writeFloat(state, kParamOffset + sizeof(float) * AestraDrift::kPitch, std::numeric_limits<float>::infinity());
    writeFloat(state, kParamOffset + sizeof(float) * AestraDrift::kMix, 0.5f);

    AestraDrift restored;
    assert(restored.initialize(48000.0, 64));
    assert(restored.loadState(state));
    assert(restored.getParameter(AestraDrift::kPitch) == 0.5f);
    assert(restored.getParameter(AestraDrift::kMix) == 0.5f);

    restored.activate();
    float inL[64] = {};
    float inR[64] = {};
    float outL[64] = {};
    float outR[64] = {};
    const float* inputs[2] = {inL, inR};
    float* outputs[2] = {outL, outR};

    restored.process(inputs, outputs, 2, 2, 64, nullptr, nullptr);
    for (uint32_t i = 0; i < 64; ++i) {
        assert(std::isfinite(outL[i]));
        assert(std::isfinite(outR[i]));
    }
}

} // namespace

int main() {
    testNanStateDoesNotEnterParametersOrProcessing();
    testInfStateDoesNotEnterParametersOrProcessing();
    return 0;
}
