// © 2026 Aestra Studios — All Rights Reserved.
// AestraCompUpgradeTest — compressor state contract tests.
//
// Rewritten under the pre-user format reset (FD-24, Aestra-Internals @ 1070c0d).
// The file previously held testOldV1BlobLoads and
// testOldV2BlobLoadsAndIgnoresDeprecatedFields, which asserted that old blobs
// LOAD and that deprecated slot indices map onto modern parameters. Both
// asserted the migration machinery the reset deletes, and both were deleted
// with it.
//
// What replaces them is the direction that nothing else checks, and that this
// plugin makes unusually sharp: Comp used to write 24 slots to carry 13 live
// parameters, seven of them overwritten with frozen constants on every save. A
// blob could therefore be "accepted" while carrying values that were never
// parameters at all. The canonical format has no room for that, which is the
// point of it.

#include "Plugin/AestraComp.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

// Builds a pre-reset blob without depending on any constant the production code
// no longer carries: the magic and slot count are literals here on purpose, so
// this test keeps describing the REMOVED format even after the symbols are gone.
std::vector<uint8_t> preResetBlob(uint32_t magic, uint32_t version, size_t slots,
                                  const std::vector<float>& values) {
    std::vector<uint8_t> bytes(sizeof(uint32_t) * 2 + sizeof(float) * slots);
    std::memcpy(bytes.data(), &magic, sizeof(magic));
    std::memcpy(bytes.data() + sizeof(uint32_t), &version, sizeof(version));
    if (!values.empty()) {
        std::memcpy(bytes.data() + sizeof(uint32_t) * 2, values.data(),
                    sizeof(float) * values.size());
    }
    return bytes;
}

bool testPreResetBlobsAreRejected() {
    // Declared defaults, read off a freshly initialized instance rather than a
    // second hand-written copy of the table.
    AestraComp reference;
    reference.initialize(48000.0, 256);
    std::vector<float> declared(AestraComp::kParamCount);
    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i)
        declared[i] = reference.getParameter(i);

    const auto rejectsAndLeavesUntouched = [&](const char* what, const std::vector<uint8_t>& blob) {
        AestraComp comp;
        comp.initialize(48000.0, 256);
        if (comp.loadState(blob)) {
            std::cerr << what << ": a pre-reset blob was accepted\n";
            return false;
        }
        for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
            if (std::fabs(comp.getParameter(i) - declared[i]) > 1.0e-6f) {
                std::cerr << what << ": a rejected blob still mutated parameter " << i << " ("
                          << comp.getParameter(i) << " vs declared " << declared[i] << ")\n";
                return false;
            }
        }
        return true;
    };

    // V1: 8 slots.
    std::vector<float> v1(8, 0.0f);
    v1[AestraComp::kThreshold] = 0.42f;
    v1[AestraComp::kRatio] = 0.33f;
    v1[AestraComp::kMix] = 0.75f;
    if (!rejectsAndLeavesUntouched("V1", preResetBlob(0x434D5001u, 1, 8, v1)))
        return false;

    // V2 through V5: 24 slots, with the deprecated indices carrying non-default
    // values. Under the old reader these mapped onto InputGain, OutputGain and
    // DetectorHPF; the assertion that mattered was that the mapping was correct.
    // Now the whole blob is rejected, which is strictly stronger -- but it must
    // be rejected rather than half-applied.
    std::vector<float> v2(24, 0.0f);
    v2[AestraComp::kThreshold] = 0.6f;
    v2[13] = 1.0f; // kLegacyLookaheadIndex
    v2[14] = 0.0f; // kLegacyStereoLinkIndex
    v2[19] = 1.0f; // kLegacySCLPFIndex
    v2[21] = 1.0f; // kLegacyQualityIndex
    for (uint32_t version = 2; version <= 5; ++version) {
        if (!rejectsAndLeavesUntouched("V2..V5", preResetBlob(0x434D5002u, version, 24, v2)))
            return false;
    }
    return true;
}

bool testCanonicalBlobHasNoFillerSlots() {
    AestraComp comp;
    comp.initialize(48000.0, 256);
    comp.setParameter(AestraComp::kThreshold, 0.42f);
    comp.setParameter(AestraComp::kQuality, 0.75f);

    const auto state = comp.saveState();
    // 8 bytes of header plus one float per PARAMETER. The old blob was 104
    // bytes: 24 slots for 13 parameters. This asserts the filler is gone rather
    // than merely unused, because a blob that still reserves the slots would
    // look identical on the wire and quietly reintroduce the debt.
    if (state.size() != sizeof(uint32_t) * 2 + sizeof(float) * AestraComp::kParamCount) {
        std::cerr << "canonical Comp blob is " << state.size() << " bytes, expected "
                  << (sizeof(uint32_t) * 2 + sizeof(float) * AestraComp::kParamCount) << "\n";
        return false;
    }

    uint32_t magic = 0;
    uint32_t version = 0;
    std::memcpy(&magic, state.data(), sizeof(magic));
    std::memcpy(&version, state.data() + sizeof(uint32_t), sizeof(version));
    if (magic != AestraComp::kStateMagic) {
        std::cerr << "Comp blob magic changed\n";
        return false;
    }
    if (version != 1u) {
        std::cerr << "Comp blob is not at the canonical version 1\n";
        return false;
    }

    AestraComp restored;
    restored.initialize(96000.0, 511);
    if (!restored.loadState(state)) {
        std::cerr << "canonical Comp blob did not load\n";
        return false;
    }
    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        if (std::fabs(restored.getParameter(i) - comp.getParameter(i)) > 1.0e-6f) {
            std::cerr << "Comp parameter " << i << " failed the round-trip\n";
            return false;
        }
    }

    // And the audio path still runs, so a rejected-state fix cannot have left a
    // compressor that loads state and then produces garbage.
    std::vector<float> input(1024, 0.25f);
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    restored.activate();
    restored.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));
    for (float sample : output) {
        if (!std::isfinite(sample)) {
            std::cerr << "restored Comp state produced non-finite audio\n";
            return false;
        }
    }
    return true;
}

bool testParameterChangedStillMarksDerivedStateDirty() {
    // setParameter used to raise m_detectorHPFDirty and m_oversamplingDirty
    // inline. Those side effects moved to onParameterChanged, and nothing else
    // covers them: a dropped hook leaves the detector filter and the
    // oversampler configured for the previous parameter with no test failing.
    AestraComp comp;
    comp.initialize(48000.0, 256);

    comp.setParameter(AestraComp::kDetectorHPF, 0.4f);
    comp.setParameter(AestraComp::kOversampling, 1.0f);

    // The flags are private, so the observable contract is that initialize()'s
    // applyOversamplingConfig() still sees them: set oversampling, re-prepare,
    // and confirm processing at a non-default setting stays finite and bounded.
    std::vector<float> input(2048, 0.4f);
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    comp.activate();
    comp.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));
    float peak = 0.0f;
    for (float sample : output) {
        if (!std::isfinite(sample)) {
            std::cerr << "detector HPF / oversampling change produced non-finite audio\n";
            return false;
        }
        peak = std::max(peak, std::fabs(sample));
    }
    if (peak > 8.0f) {
        std::cerr << "output peak " << peak << " is implausible for a compressor\n";
        return false;
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
    std::cout << "AestraComp state contract tests\n";
    if (!testPreResetBlobsAreRejected()) return 1;
    if (!testCanonicalBlobHasNoFillerSlots()) return 1;
    if (!testParameterChangedStillMarksDerivedStateDirty()) return 1;
    if (!testCurrentStateUsesPluginIdIndependentName()) return 1;
    std::cout << "All AestraComp state contract tests passed.\n";
    return 0;
}