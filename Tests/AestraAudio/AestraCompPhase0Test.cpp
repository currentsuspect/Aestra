// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompPhase0Test — V1 compressor DSP contract tests.

#include "Plugin/AestraComp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraComp;

namespace {

constexpr uint32_t kBlockSize = 256;

float thresholdNorm(float db) { return (db + 60.0f) / 60.0f; }
float ratioNorm(float ratio) { return (ratio - 1.0f) / 19.0f; }
float attackNorm(float ms) { return (ms - 0.1f) / 99.9f; }
float releaseNorm(float ms) { return (ms - 10.0f) / 990.0f; }
float gainNorm(float db) { return (db + 24.0f) / 48.0f; }
float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
float linearToDb(float x) { return x > 1.0e-12f ? 20.0f * std::log10(x) : -120.0f; }

void configure(AestraComp& comp, double sampleRate = 48000.0) {
    comp.initialize(sampleRate, kBlockSize);
    comp.setParameter(AestraComp::kThreshold, thresholdNorm(-24.0f));
    comp.setParameter(AestraComp::kRatio, ratioNorm(4.0f));
    comp.setParameter(AestraComp::kAttack, attackNorm(0.1f));
    comp.setParameter(AestraComp::kRelease, releaseNorm(10.0f));
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);
    comp.setParameter(AestraComp::kInputGain, gainNorm(0.0f));
    comp.setParameter(AestraComp::kOutputGain, gainNorm(0.0f));
    comp.setParameter(AestraComp::kDetectorHPF, 0.0f);
    comp.activate();
}

std::vector<float> processMono(AestraComp& comp, const std::vector<float>& input, uint32_t blockSize = kBlockSize) {
    std::vector<float> output(input.size(), 0.0f);
    for (size_t offset = 0; offset < input.size(); offset += blockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(blockSize, input.size() - offset));
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {output.data() + offset};
        comp.process(inputs, outputs, 1, 1, frames);
    }
    return output;
}

std::pair<std::vector<float>, std::vector<float>> processStereo(AestraComp& comp,
                                                                const std::vector<float>& inL,
                                                                const std::vector<float>& inR) {
    std::vector<float> outL(inL.size(), 0.0f);
    std::vector<float> outR(inR.size(), 0.0f);
    for (size_t offset = 0; offset < inL.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, inL.size() - offset));
        const float* inputs[] = {inL.data() + offset, inR.data() + offset};
        float* outputs[] = {outL.data() + offset, outR.data() + offset};
        comp.process(inputs, outputs, 2, 2, frames);
    }
    return {outL, outR};
}

bool near(float a, float b, float tolerance) {
    return std::abs(a - b) <= tolerance;
}

bool testSilenceAndBypass() {
    AestraComp comp;
    configure(comp);
    std::vector<float> silence(4096, 0.0f);
    auto out = processMono(comp, silence);
    if (std::any_of(out.begin(), out.end(), [](float x) { return x != 0.0f; })) {
        std::cerr << "silence produced non-zero output\n";
        return false;
    }

    std::vector<float> input(4096);
    for (size_t i = 0; i < input.size(); ++i) input[i] = std::sin(static_cast<float>(i) * 0.01f) * 0.37f;
    comp.setParameter(AestraComp::kBypass, 1.0f);
    out = processMono(comp, input);
    for (size_t i = 0; i < input.size(); ++i) {
        if (out[i] != input[i]) {
            std::cerr << "bypass mismatch at " << i << "\n";
            return false;
        }
    }
    return true;
}

bool testStaticGainReduction() {
    AestraComp comp;
    configure(comp);
    std::vector<float> input(48000, 0.5f);
    auto out = processMono(comp, input);

    const float inputDb = linearToDb(0.5f);
    const float overDb = inputDb - (-24.0f);
    const float expected = 0.5f * dbToLinear(-overDb * (1.0f - 1.0f / 4.0f));
    if (!near(out.back(), expected, 0.015f)) {
        std::cerr << "static GR output=" << out.back() << " expected~=" << expected << "\n";
        return false;
    }
    return true;
}

bool testKneeMonotonic() {
    auto run = [](float amp, float kneeNorm) {
        AestraComp comp;
        configure(comp);
        comp.setParameter(AestraComp::kThreshold, thresholdNorm(-18.0f));
        comp.setParameter(AestraComp::kRatio, ratioNorm(6.0f));
        comp.setParameter(AestraComp::kKnee, kneeNorm);
        std::vector<float> input(48000, amp);
        return processMono(comp, input).back();
    };

    const float belowAmp = dbToLinear(-21.0f);
    const float nearAmp = dbToLinear(-18.0f);
    const float aboveAmp = dbToLinear(-9.0f);
    const float belowGain = run(belowAmp, 1.0f) / belowAmp;
    const float nearGain = run(nearAmp, 1.0f) / nearAmp;
    const float aboveGain = run(aboveAmp, 1.0f) / aboveAmp;
    if (!(belowGain > nearGain && nearGain > aboveGain)) {
        std::cerr << "soft knee gain is not monotonic: " << belowGain << ", " << nearGain << ", " << aboveGain
                  << "\n";
        return false;
    }

    const float hardBelow = run(belowAmp, 0.0f);
    if (!near(hardBelow, belowAmp, 0.002f)) {
        std::cerr << "hard knee compressed below-threshold signal\n";
        return false;
    }
    return true;
}

bool testAttackAndReleaseTiming() {
    AestraComp attackComp;
    configure(attackComp);
    attackComp.setParameter(AestraComp::kAttack, attackNorm(50.0f));
    std::vector<float> step(48000, 1.0f);
    auto attackOut = processMono(attackComp, step);
    if (!(std::abs(attackOut[32]) > std::abs(attackOut[12000]))) {
        std::cerr << "attack did not reduce gain over time\n";
        return false;
    }

    AestraComp releaseComp;
    configure(releaseComp);
    releaseComp.setParameter(AestraComp::kAttack, attackNorm(0.1f));
    releaseComp.setParameter(AestraComp::kRelease, releaseNorm(300.0f));
    std::vector<float> releaseInput(48000, 1.0f);
    std::fill(releaseInput.begin() + 24000, releaseInput.end(), 0.1f);
    auto releaseOut = processMono(releaseComp, releaseInput);
    if (!(std::abs(releaseOut[24100]) < std::abs(releaseOut.back()))) {
        std::cerr << "release did not recover gain\n";
        return false;
    }
    return true;
}

bool testSampleRateIndependence() {
    float reference = 0.0f;
    for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
        AestraComp comp;
        configure(comp, sampleRate);
        std::vector<float> input(static_cast<size_t>(sampleRate * 0.5), 0.5f);
        const float tail = processMono(comp, input).back();
        if (reference == 0.0f) reference = tail;
        if (!near(tail, reference, 0.015f)) {
            std::cerr << "sample-rate mismatch at " << sampleRate << ": " << tail << " vs " << reference << "\n";
            return false;
        }
    }
    return true;
}

bool testMixAndGainControls() {
    std::vector<float> input(48000, 0.5f);

    AestraComp wetComp;
    configure(wetComp);
    const float wet = processMono(wetComp, input).back();

    AestraComp dryComp;
    configure(dryComp);
    dryComp.setParameter(AestraComp::kMix, 0.0f);
    const float dry = processMono(dryComp, input).back();
    if (!near(dry, 0.5f, 0.0001f)) {
        std::cerr << "mix 0 not dry: " << dry << "\n";
        return false;
    }

    AestraComp halfComp;
    configure(halfComp);
    halfComp.setParameter(AestraComp::kMix, 0.5f);
    const float half = processMono(halfComp, input).back();
    if (!near(half, 0.5f * (dry + wet), 0.01f)) {
        std::cerr << "mix 50 mismatch: " << half << " expected " << 0.5f * (dry + wet) << "\n";
        return false;
    }

    AestraComp makeupComp;
    configure(makeupComp);
    makeupComp.setParameter(AestraComp::kMakeup, 0.25f); // +6 dB
    const float makeup = processMono(makeupComp, input).back();
    if (!near(makeup, wet * dbToLinear(6.0f), 0.03f)) {
        std::cerr << "makeup mismatch\n";
        return false;
    }

    AestraComp outputComp;
    configure(outputComp);
    outputComp.setParameter(AestraComp::kOutputGain, gainNorm(6.0f));
    const float output = processMono(outputComp, input).back();
    if (!near(output, wet * dbToLinear(6.0f), 0.03f)) {
        std::cerr << "output gain mismatch\n";
        return false;
    }

    AestraComp inputComp;
    configure(inputComp);
    inputComp.setParameter(AestraComp::kInputGain, gainNorm(6.0f));
    (void)processMono(inputComp, input).back();
    if (!(inputComp.getCurrentGainReductionDb() > wetComp.getCurrentGainReductionDb() + 3.0f)) {
        std::cerr << "input gain did not increase detector gain reduction\n";
        return false;
    }
    return true;
}

bool testDetectorHPFReducesLowFrequencyTriggering() {
    std::vector<float> lowTone(48000, 0.0f);
    for (size_t i = 0; i < lowTone.size(); ++i) {
        lowTone[i] = std::sin(static_cast<float>(i) * 2.0f * 3.14159265358979323846f * 60.0f / 48000.0f) * 0.8f;
    }

    AestraComp noFilter;
    configure(noFilter);
    noFilter.setParameter(AestraComp::kThreshold, thresholdNorm(-30.0f));
    noFilter.setParameter(AestraComp::kRatio, ratioNorm(8.0f));
    (void)processMono(noFilter, lowTone);
    const float grNoFilter = noFilter.getCurrentGainReductionDb();

    AestraComp withFilter;
    configure(withFilter);
    withFilter.setParameter(AestraComp::kThreshold, thresholdNorm(-30.0f));
    withFilter.setParameter(AestraComp::kRatio, ratioNorm(8.0f));
    withFilter.setParameter(AestraComp::kDetectorHPF, 1.0f);
    (void)processMono(withFilter, lowTone);
    const float grWithFilter = withFilter.getCurrentGainReductionDb();

    if (!(grWithFilter + 3.0f < grNoFilter)) {
        std::cerr << "detector HPF did not reduce low-frequency GR enough: noFilter=" << grNoFilter
                  << " withFilter=" << grWithFilter << "\n";
        return false;
    }
    return true;
}

bool testStereoLinkAndSanitization() {
    AestraComp comp;
    configure(comp);
    std::vector<float> inL(48000, 0.8f);
    std::vector<float> inR(48000, 0.2f);
    auto [outL, outR] = processStereo(comp, inL, inR);
    const float ratio = outL.back() / outR.back();
    if (!near(ratio, 4.0f, 0.05f)) {
        std::cerr << "linked stereo image changed ratio=" << ratio << "\n";
        return false;
    }

    AestraComp poison;
    configure(poison);
    std::vector<float> input = {0.0f, std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                1.0e30f, -1.0e30f, 0.25f};
    auto out = processMono(poison, input);
    for (float sample : out) {
        if (!std::isfinite(sample)) {
            std::cerr << "non-finite output from poisoned input\n";
            return false;
        }
    }
    if (!std::isfinite(poison.getCurrentGainReductionDb())) {
        std::cerr << "non-finite gain reduction state\n";
        return false;
    }
    return true;
}

bool testClassicFeedbackTopology() {
    AestraComp comp;
    configure(comp);
    comp.setParameter(AestraComp::kCompMode, 1.0f / 2.0f);
    comp.setParameter(AestraComp::kThreshold, thresholdNorm(-12.0f));
    comp.setParameter(AestraComp::kRatio, ratioNorm(4.0f));
    comp.setParameter(AestraComp::kAttack, attackNorm(0.1f));
    comp.setParameter(AestraComp::kRelease, releaseNorm(10.0f));
    std::vector<float> step(48000, 1.0f);
    auto out = processMono(comp, step);
    if (!(comp.getCurrentGainReductionDb() > 0.0f)) {
        std::cerr << "classic mode produced no gain reduction\n";
        return false;
    }
    if (!std::isfinite(out.back())) {
        std::cerr << "classic mode produced non-finite output\n";
        return false;
    }
    return true;
}

bool testOpticalModeDiffersFromClean() {
    AestraComp cleanComp;
    configure(cleanComp);
    cleanComp.setParameter(AestraComp::kThreshold, thresholdNorm(-18.0f));
    cleanComp.setParameter(AestraComp::kRatio, ratioNorm(6.0f));
    std::vector<float> step(48000, 0.8f);
    auto cleanOut = processMono(cleanComp, step);

    AestraComp optComp;
    configure(optComp);
    optComp.setParameter(AestraComp::kCompMode, 2.0f / 2.0f);
    optComp.setParameter(AestraComp::kThreshold, thresholdNorm(-18.0f));
    optComp.setParameter(AestraComp::kRatio, ratioNorm(6.0f));
    auto optOut = processMono(optComp, step);

    const float cleanLast = cleanOut.back();
    const float optLast = optOut.back();
    if (std::abs(cleanLast - optLast) < 0.001f) {
        std::cerr << "optical mode identical to clean: " << cleanLast << " vs " << optLast << "\n";
        return false;
    }
    if (!std::isfinite(optLast)) {
        std::cerr << "optical mode produced non-finite output\n";
        return false;
    }
    return true;
}

bool testModeSwitchResetsFeedback() {
    AestraComp comp;
    configure(comp);
    comp.setParameter(AestraComp::kCompMode, 1.0f / 2.0f);
    comp.setParameter(AestraComp::kThreshold, thresholdNorm(-12.0f));
    std::vector<float> step(24000, 1.0f);
    (void)processMono(comp, step);

    comp.setParameter(AestraComp::kCompMode, 0.0f);
    std::vector<float> step2(256, 0.0f);
    auto out = processMono(comp, step2);
    for (float s : out) {
        if (s != 0.0f) {
            std::cerr << "mode switch produced transient on silence\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp V1 DSP Tests\n";
    if (!testSilenceAndBypass()) return 1;
    if (!testStaticGainReduction()) return 1;
    if (!testKneeMonotonic()) return 1;
    if (!testAttackAndReleaseTiming()) return 1;
    if (!testSampleRateIndependence()) return 1;
    if (!testMixAndGainControls()) return 1;
    if (!testDetectorHPFReducesLowFrequencyTriggering()) return 1;
    if (!testStereoLinkAndSanitization()) return 1;
    if (!testClassicFeedbackTopology()) return 1;
    if (!testOpticalModeDiffersFromClean()) return 1;
    if (!testModeSwitchResetsFeedback()) return 1;
    std::cout << "All AestraComp V1 DSP tests passed.\n";
    return 0;
}
