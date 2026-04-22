// © 2026 Aestra Studios — All Rights Reserved.
// AestraCompUpgradeTest — engine-only coverage for compressor upgrade fixes.

#include "Plugin/AestraComp.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

std::vector<float> processMono(AestraComp& comp, const std::vector<float>& input) {
    std::vector<float> output(input.size(), 0.0f);
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    comp.process(inputs, outputs, 1, 1, static_cast<uint32_t>(input.size()));
    return output;
}

void setNeutral(AestraComp& comp) {
    comp.setParameter(AestraComp::kThreshold, 1.0f);
    comp.setParameter(AestraComp::kRatio, 0.0f);
    comp.setParameter(AestraComp::kAttack, 0.0991f);
    comp.setParameter(AestraComp::kRelease, 0.1414f);
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);
    comp.setParameter(AestraComp::kDetectorMode, 0.0f);
    comp.setParameter(AestraComp::kRange, 0.0f);
    comp.setParameter(AestraComp::kStereoLink, 1.0f);
    comp.setParameter(AestraComp::kOutputTrim, 0.5f);
}

bool testMakeupAppliedOnce() {
    AestraComp comp;
    comp.initialize(48000.0, 512);
    comp.activate();
    setNeutral(comp);
    comp.setParameter(AestraComp::kMakeup, 0.25f); // +6dB

    std::vector<float> input(4096, 0.1f);
    auto output = processMono(comp, input);
    const float tail = output.back();
    const float expected = 0.1f * std::pow(10.0f, 6.0f / 20.0f);
    if (std::abs(tail - expected) > 0.035f) {
        std::cerr << "Makeup gain applied incorrectly. tail=" << tail << " expected~=" << expected << "\n";
        return false;
    }
    if (tail > 0.30f) {
        std::cerr << "Makeup appears doubled. tail=" << tail << "\n";
        return false;
    }
    return true;
}

bool testSoftClipAndMeters() {
    AestraComp comp;
    comp.initialize(48000.0, 512);
    comp.activate();
    setNeutral(comp);
    comp.setParameter(AestraComp::kMakeup, 1.0f); // +24dB, forces saturator

    std::vector<float> input(4096, 1.0f);
    auto output = processMono(comp, input);
    const float tail = output.back();
    if (!std::isfinite(tail) || tail <= 0.90f || tail > 1.0f) {
        std::cerr << "Soft clip output out of range. tail=" << tail << "\n";
        return false;
    }
    if (comp.getInputLevel() < 0.99f || comp.getOutputLevel() < 0.90f) {
        std::cerr << "Input/output meters did not update. in=" << comp.getInputLevel()
                  << " out=" << comp.getOutputLevel() << "\n";
        return false;
    }
    return true;
}

bool testPeakAndRmsDiffer() {
    constexpr size_t n = 8192;
    std::vector<float> input(n, 0.02f);
    for (size_t i = 0; i < n; i += 64) {
        input[i] = 0.95f;
    }

    auto configure = [](AestraComp& comp, float mode) {
        comp.initialize(48000.0, 512);
        comp.activate();
        comp.setParameter(AestraComp::kThreshold, 0.7f); // -18dB
        comp.setParameter(AestraComp::kRatio, 0.5f);
        comp.setParameter(AestraComp::kAttack, 0.0f);
        comp.setParameter(AestraComp::kRelease, 0.05f);
        comp.setParameter(AestraComp::kMakeup, 0.0f);
        comp.setParameter(AestraComp::kKnee, 0.0f);
        comp.setParameter(AestraComp::kMix, 1.0f);
        comp.setParameter(AestraComp::kDetectorMode, mode);
        comp.setParameter(AestraComp::kOutputTrim, 0.5f);
    };

    AestraComp peak;
    configure(peak, 0.0f);
    auto peakOut = processMono(peak, input);

    AestraComp rms;
    configure(rms, 1.0f);
    auto rmsOut = processMono(rms, input);

    (void)peakOut;
    (void)rmsOut;

    const float peakGR = peak.getCurrentGainReductionDb();
    const float rmsGR = rms.getCurrentGainReductionDb();
    if (std::abs(peakGR - rmsGR) < 0.5f) {
        std::cerr << "Peak/RMS detection did not diverge enough. peakGR=" << peakGR
                  << " rmsGR=" << rmsGR << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp Upgrade Tests\n";
    if (!testMakeupAppliedOnce()) return 1;
    if (!testSoftClipAndMeters()) return 1;
    if (!testPeakAndRmsDiffer()) return 1;
    std::cout << "All AestraComp upgrade tests passed.\n";
    return 0;
}
