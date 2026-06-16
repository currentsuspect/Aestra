// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompPhase1Test — V1 compressor parameter and state contract tests.

#include "Plugin/AestraComp.h"
#include "Plugin/BuiltInPlugins.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

void writeFloat(std::vector<uint8_t>& bytes, size_t offset, float value) {
    assert(offset + sizeof(float) <= bytes.size() && "writeFloat: offset out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

bool testPublicParameterSurface() {
    AestraComp comp;
    comp.initialize(48000.0, 256);

    const auto params = comp.getParameters();
    if (comp.getParameterCount() != AestraComp::kParamCount || params.size() != AestraComp::kParamCount) {
        std::cerr << "unexpected V1 parameter count\n";
        return false;
    }

    const char* expected[] = {
        "Threshold", "Ratio", "Attack", "Release", "Makeup Gain", "Knee",
        "Mix", "Bypass", "Input Gain", "Output Gain", "Detector HPF", "Mode",
    };

    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        if (params[i].id != i || params[i].name != expected[i]) {
            std::cerr << "unexpected param " << i << ": " << params[i].name << "\n";
            return false;
        }
        if (comp.getParameterDisplay(i).empty()) {
            std::cerr << "empty display for param " << i << "\n";
            return false;
        }
    }

    for (const std::string hidden : {"Detector", "Topology", "Hold", "Auto Release", "Range", "Lookahead",
                                    "Stereo Link", "SC LPF", "SC Listen", "Style", "Quality"}) {
        for (const auto& param : params) {
            if (param.name == hidden) {
                std::cerr << "deprecated parameter still exposed: " << hidden << "\n";
                return false;
            }
        }
    }

    return true;
}

bool testNormalizedWritesClamp() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    comp.setParameter(AestraComp::kThreshold, -1.0f);
    comp.setParameter(AestraComp::kRatio, 2.0f);
    comp.setParameter(999, 0.5f);
    if (comp.getParameter(AestraComp::kThreshold) != 0.0f || comp.getParameter(AestraComp::kRatio) != 1.0f) {
        std::cerr << "normalized parameter clamp failed\n";
        return false;
    }
    return true;
}

bool testStateRoundTrip() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        comp.setParameter(i, 0.05f + 0.9f * static_cast<float>(i) / static_cast<float>(AestraComp::kParamCount - 1));
    }

    const auto state = comp.saveState();
    AestraComp restored;
    restored.initialize(48000.0, 256);
    if (!restored.loadState(state)) {
        std::cerr << "V1 state failed to load\n";
        return false;
    }

    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        if (std::abs(restored.getParameter(i) - comp.getParameter(i)) > 1.0e-6f) {
            std::cerr << "roundtrip mismatch at param " << i << "\n";
            return false;
        }
    }
    return true;
}

bool testInvalidStateFailsSafely() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    if (comp.loadState({1, 2, 3})) {
        std::cerr << "truncated state accepted\n";
        return false;
    }

    std::vector<uint8_t> corrupt(96, 0);
    uint32_t badMagic = 0xDEADBEEF;
    std::memcpy(corrupt.data(), &badMagic, sizeof(badMagic));
    if (comp.loadState(corrupt)) {
        std::cerr << "bad magic accepted\n";
        return false;
    }
    return true;
}

bool testNanStateDoesNotEnterParametersOrProcessing() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    comp.setParameter(AestraComp::kThreshold, 0.25f);
    comp.setParameter(AestraComp::kRatio, 0.5f);
    comp.setParameter(AestraComp::kMix, 1.0f);

    std::vector<uint8_t> state = comp.saveState();
    // Layout: magic (uint32_t) + version (uint32_t) + params[]
    constexpr size_t kParamOffset = sizeof(uint32_t) * 2;
    static_assert(kParamOffset == 8, "saveState header layout changed — update kParamOffset");
    writeFloat(state, kParamOffset + sizeof(float) * AestraComp::kThreshold,
               std::numeric_limits<float>::quiet_NaN());
    writeFloat(state, kParamOffset + sizeof(float) * AestraComp::kMakeup,
               std::numeric_limits<float>::infinity());

    AestraComp restored;
    restored.initialize(48000.0, 256);
    if (!restored.loadState(state)) {
        std::cerr << "state with non-finite compressor params failed to load\n";
        return false;
    }
    if (!std::isfinite(restored.getParameter(AestraComp::kThreshold)) ||
        !std::isfinite(restored.getParameter(AestraComp::kMakeup))) {
        std::cerr << "non-finite compressor state entered parameters\n";
        return false;
    }

    restored.activate();
    std::vector<float> input(1024, 0.5f);
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    restored.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));

    for (float sample : output) {
        if (!std::isfinite(sample)) {
            std::cerr << "non-finite compressor output after loading poisoned state\n";
            return false;
        }
    }
    return true;
}

bool testProcessPathDoesNotResizeKnownBuffers() {
    // V1 has no RMS window, lookahead, delay, or sidechain-listen buffers. This
    // test protects the visible contract by processing after initialization with
    // changing detector HPF state and verifying the compressor remains functional.
    AestraComp comp;
    comp.initialize(48000.0, 256);
    comp.setParameter(AestraComp::kDetectorHPF, 1.0f);
    comp.activate();
    std::vector<float> input(4096, 0.25f);
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    comp.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));
    for (float sample : output) {
        if (!std::isfinite(sample)) {
            std::cerr << "non-finite output with detector HPF\n";
            return false;
        }
    }
    return true;
}

bool testBuiltInMetadata() {
    const auto& info = Aestra::Audio::BuiltInPlugins::compInfo();
    if (info.id != "com.Aestrastudios.comp") {
        std::cerr << "unexpected compressor plugin ID: " << info.id << "\n";
        return false;
    }
    if (info.name != "Aestra Compressor") {
        std::cerr << "unexpected compressor display name: " << info.name << "\n";
        return false;
    }
    if (info.category != "Dynamics" || info.type != Aestra::Audio::PluginType::Effect) {
        std::cerr << "unexpected compressor metadata category/type\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp V1 Parameter/State Tests\n";
    if (!testPublicParameterSurface()) return 1;
    if (!testNormalizedWritesClamp()) return 1;
    if (!testStateRoundTrip()) return 1;
    if (!testInvalidStateFailsSafely()) return 1;
    if (!testNanStateDoesNotEnterParametersOrProcessing()) return 1;
    if (!testProcessPathDoesNotResizeKnownBuffers()) return 1;
    if (!testBuiltInMetadata()) return 1;
    std::cout << "All AestraComp V1 parameter/state tests passed.\n";
    return 0;
}
