// © 2026 Aestra Studios — All Rights Reserved.
// AestraCompUpgradeTest — old compressor state compatibility tests.

#include "Plugin/AestraComp.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

template <typename Blob>
std::vector<uint8_t> toBytes(const Blob& blob) {
    const auto* data = reinterpret_cast<const uint8_t*>(&blob);
    return {data, data + sizeof(blob)};
}

bool testOldV1BlobLoads() {
    struct BlobV1 {
        uint32_t magic = AestraComp::kStateMagicV1;
        uint32_t version = 1;
        float params[8] = {};
    } blob;

    blob.params[AestraComp::kThreshold] = 0.42f;
    blob.params[AestraComp::kRatio] = 0.33f;
    blob.params[AestraComp::kAttack] = 0.11f;
    blob.params[AestraComp::kRelease] = 0.77f;
    blob.params[AestraComp::kMakeup] = 0.25f;
    blob.params[AestraComp::kKnee] = 0.5f;
    blob.params[AestraComp::kMix] = 0.75f;
    blob.params[AestraComp::kBypass] = 0.0f;

    AestraComp comp;
    comp.initialize(48000.0, 256);
    if (!comp.loadState(toBytes(blob))) {
        std::cerr << "old v1 blob rejected\n";
        return false;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        if (std::abs(comp.getParameter(i) - blob.params[i]) > 1.0e-6f) {
            std::cerr << "old v1 param mismatch at " << i << "\n";
            return false;
        }
    }
    if (std::abs(comp.getParameter(AestraComp::kInputGain) - 0.5f) > 1.0e-6f ||
        std::abs(comp.getParameter(AestraComp::kOutputGain) - 0.5f) > 1.0e-6f ||
        comp.getParameter(AestraComp::kDetectorHPF) != 0.0f) {
        std::cerr << "old v1 defaults for new V1 params are wrong\n";
        return false;
    }
    return true;
}

bool testOldV2BlobLoadsAndIgnoresDeprecatedFields() {
    struct BlobV2 {
        uint32_t magic = AestraComp::kStateMagicV2;
        uint32_t version = 2;
        float params[AestraComp::kLegacyParamCount] = {};
    } blob;

    blob.params[AestraComp::kThreshold] = 0.6f;
    blob.params[AestraComp::kRatio] = 0.4f;
    blob.params[AestraComp::kAttack] = 0.2f;
    blob.params[AestraComp::kRelease] = 0.3f;
    blob.params[AestraComp::kMakeup] = 0.1f;
    blob.params[AestraComp::kKnee] = 0.25f;
    blob.params[AestraComp::kMix] = 0.8f;
    blob.params[AestraComp::kBypass] = 0.0f;

    blob.params[AestraComp::kLegacyDetectorModeIndex] = 1.0f;
    blob.params[AestraComp::kLegacyTopologyIndex] = 1.0f;
    blob.params[AestraComp::kLegacyHoldIndex] = 1.0f;
    blob.params[AestraComp::kLegacyAutoReleaseIndex] = 1.0f;
    blob.params[AestraComp::kLegacyRangeIndex] = 1.0f;
    blob.params[AestraComp::kLegacyLookaheadIndex] = 1.0f;
    blob.params[AestraComp::kLegacyStereoLinkIndex] = 0.0f;
    blob.params[AestraComp::kLegacySCHPFIndex] = 0.5f;
    blob.params[AestraComp::kLegacySCLPFIndex] = 1.0f;
    blob.params[AestraComp::kLegacySCListenIndex] = 1.0f;
    blob.params[AestraComp::kLegacyOutputTrimIndex] = 0.75f;
    blob.params[AestraComp::kLegacyStyleIndex] = 1.0f;
    blob.params[AestraComp::kLegacyQualityIndex] = 1.0f;

    AestraComp comp;
    comp.initialize(48000.0, 256);
    if (!comp.loadState(toBytes(blob))) {
        std::cerr << "old v2 blob rejected\n";
        return false;
    }

    if (std::abs(comp.getParameter(AestraComp::kInputGain) - 0.5f) > 1.0e-6f) {
        std::cerr << "old detector mode leaked into input gain\n";
        return false;
    }
    if (std::abs(comp.getParameter(AestraComp::kOutputGain) - 0.75f) > 1.0e-6f) {
        std::cerr << "old output trim did not map to output gain\n";
        return false;
    }
    if (std::abs(comp.getParameter(AestraComp::kDetectorHPF) - 0.5f) > 1.0e-6f) {
        std::cerr << "old SC HPF did not map to detector HPF\n";
        return false;
    }

    std::vector<float> input(1024, 0.25f);
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    comp.activate();
    comp.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));
    for (float sample : output) {
        if (!std::isfinite(sample)) {
            std::cerr << "old v2 state produced non-finite audio\n";
            return false;
        }
    }
    return true;
}

bool testCurrentStateUsesPluginIdIndependentName() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    Aestra::Audio::PluginInfo info;
    info.id = "com.Aestrastudios.comp";
    info.name = "Aestra Compressor";
    comp.setInfo(info);
    if (comp.getInfo().id != "com.Aestrastudios.comp" || comp.getInfo().name != "Aestra Compressor") {
        std::cerr << "plugin identity mismatch\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp V1 Upgrade Tests\n";
    if (!testOldV1BlobLoads()) return 1;
    if (!testOldV2BlobLoadsAndIgnoresDeprecatedFields()) return 1;
    if (!testCurrentStateUsesPluginIdIndependentName()) return 1;
    std::cout << "All AestraComp V1 upgrade tests passed.\n";
    return 0;
}
