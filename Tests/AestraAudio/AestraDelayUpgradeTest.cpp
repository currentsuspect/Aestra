// © 2026 Aestra Studios — All Rights Reserved.
// AestraDelayUpgradeTest — engine coverage for sync, ping-pong, and saturation behavior.

#include "Plugin/AestraDelay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using Aestra::Audio::Plugins::AestraDelay;

namespace {

struct StereoBuffer {
    std::vector<float> left;
    std::vector<float> right;
};

StereoBuffer processStereo(AestraDelay& delay, const std::vector<float>& inL, const std::vector<float>& inR) {
    StereoBuffer out{std::vector<float>(inL.size(), 0.0f), std::vector<float>(inL.size(), 0.0f)};
    const float* inputs[] = {inL.data(), inR.data()};
    float* outputs[] = {out.left.data(), out.right.data()};
    delay.process(inputs, outputs, 2, 2, static_cast<uint32_t>(inL.size()));
    return out;
}

size_t peakIndexInRange(const std::vector<float>& buffer, size_t begin, size_t end) {
    end = std::min(end, buffer.size());
    size_t best = begin;
    float peak = 0.0f;
    for (size_t i = begin; i < end; ++i) {
        const float v = std::abs(buffer[i]);
        if (v > peak) {
            peak = v;
            best = i;
        }
    }
    return best;
}

bool testSyncedEighthAt120Bpm() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setBPM(120.0f);
    delay.setParameter(AestraDelay::kSyncMode, 1.0f);
    delay.setParameter(AestraDelay::kNoteDivision, AestraDelay::noteDivisionParamFromIndex(AestraDelay::kDiv1_8));
    delay.setParameter(AestraDelay::kFeedback, 0.0f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(13000, 0.0f);
    std::vector<float> inR(13000, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    auto out = processStereo(delay, inL, inR);
    constexpr size_t expected = 12000; // 120 BPM eighth note = 250ms at 48k.
    const size_t peak = peakIndexInRange(out.left, expected - 8, expected + 9);
    const float peakValue = std::abs(out.left[peak]);
    if (peakValue < 0.75f || std::abs(static_cast<int>(peak) - static_cast<int>(expected)) > 2) {
        std::cerr << "Synced 1/8 delay missed grid. peak=" << peak << " value=" << peakValue << " expected=" << expected
                  << "\n";
        return false;
    }
    return true;
}

bool testPingPongAlternates() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kSyncMode, 0.0f);
    delay.setParameter(AestraDelay::kTime, 0.0f); // 10ms minimum.
    delay.setParameter(AestraDelay::kFeedback, 0.85f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kStereoMode, 1.0f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(1800, 0.0f);
    std::vector<float> inR(1800, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    auto out = processStereo(delay, inL, inR);
    const float firstL = std::abs(out.left[480]);
    const float firstR = std::abs(out.right[480]);
    const float secondL = std::abs(out.left[960]);
    const float secondR = std::abs(out.right[960]);

    if (firstL < 0.70f || firstR < 0.10f || firstR > 0.20f || secondR < 0.25f || secondL < 0.05f || secondL > 0.20f) {
        std::cerr << "Ping-pong image did not alternate with bleed. first L/R=" << firstL << "/" << firstR
                  << " second L/R=" << secondL << "/" << secondR << "\n";
        return false;
    }
    return true;
}

bool testPingPongSustainedInputDoesNotAccumulateLeft() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kSyncMode, 0.0f);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 0.5f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kStereoMode, 1.0f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(480 * 10, 1.2f);
    std::vector<float> inR(480 * 10, 0.8f);

    auto out = processStereo(delay, inL, inR);

    float energyL = 0.0f;
    float energyR = 0.0f;
    for (size_t i = 480; i < out.left.size(); ++i) {
        energyL += std::abs(out.left[i]);
        energyR += std::abs(out.right[i]);
    }

    const float ratio = energyR > 1.0e-6f ? energyL / energyR : 999.0f;
    if (ratio < 0.8f || ratio > 1.25f) {
        std::cerr << "Sustained ping-pong accumulated channel imbalance. energy L/R=" << energyL << "/" << energyR
                  << " ratio=" << ratio << "\n";
        return false;
    }
    return true;
}

bool testHighFeedbackStaysFinite() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 1.0f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(4096, 0.0f);
    std::vector<float> inR(4096, 0.0f);
    for (size_t i = 0; i < 64; ++i) {
        inL[i] = 3.0f;
        inR[i] = 3.0f;
    }

    auto out = processStereo(delay, inL, inR);
    for (float v : out.left) {
        if (!std::isfinite(v) || std::abs(v) > 64.00001f) {
            std::cerr << "Feedback finite guard failed. sample=" << v << "\n";
            return false;
        }
    }
    for (float v : out.right) {
        if (!std::isfinite(v) || std::abs(v) > 64.00001f) {
            std::cerr << "Feedback finite guard failed. sample=" << v << "\n";
            return false;
        }
    }
    return true;
}

bool testHotWetRepeatIsClean() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 0.0f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(1024, 0.0f);
    std::vector<float> inR(1024, 0.0f);
    inL[0] = 2.25f;
    inR[0] = -2.25f;

    auto out = processStereo(delay, inL, inR);
    if (std::abs(out.left[480] - 2.25f) > 1.0e-6f || std::abs(out.right[480] + 2.25f) > 1.0e-6f) {
        std::cerr << "Hot wet repeat was altered. L=" << out.left[480] << " R=" << out.right[480] << "\n";
        return false;
    }
    return true;
}

bool testModulationMaxStaysFinite() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 0.8f);
    delay.setParameter(AestraDelay::kDamping, 0.1f);
    delay.setParameter(AestraDelay::kStereoShift, 1.0f);
    delay.setParameter(AestraDelay::kModDepth, 1.0f);
    delay.setParameter(AestraDelay::kModRate, 1.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(8192, 0.0f);
    std::vector<float> inR(8192, 0.0f);
    for (size_t i = 0; i < inL.size(); ++i) {
        inL[i] = std::sin(static_cast<float>(i) * 0.07f) * 0.8f;
        inR[i] = std::sin(static_cast<float>(i) * 0.05f) * 0.8f;
    }

    auto out = processStereo(delay, inL, inR);
    for (float v : out.left) {
        if (!std::isfinite(v)) {
            std::cerr << "Max modulation produced non-finite left sample.\n";
            return false;
        }
    }
    for (float v : out.right) {
        if (!std::isfinite(v)) {
            std::cerr << "Max modulation produced non-finite right sample.\n";
            return false;
        }
    }
    return true;
}

bool testDelayTimeChangesAreCrossfaded() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kSyncMode, 0.0f);
    delay.setParameter(AestraDelay::kTime, 0.0f); // Start at 10ms.
    delay.setParameter(AestraDelay::kFeedback, 0.0f);
    delay.setParameter(AestraDelay::kDamping, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    constexpr size_t firstFrames = 30000;
    constexpr size_t secondFrames = 2048;
    constexpr float omega = 2.0f * 3.14159265358979323846f * 437.0f / 48000.0f;
    std::vector<float> firstL(firstFrames, 0.0f);
    std::vector<float> firstR(firstFrames, 0.0f);
    for (size_t i = 0; i < firstFrames; ++i) {
        firstL[i] = std::sin(omega * static_cast<float>(i));
        firstR[i] = firstL[i];
    }
    const auto before = processStereo(delay, firstL, firstR);

    delay.setParameter(AestraDelay::kSyncMode, 1.0f);
    delay.setParameter(AestraDelay::kNoteDivision, AestraDelay::noteDivisionParamFromIndex(AestraDelay::kDiv1_4));
    std::vector<float> secondL(secondFrames, 0.0f);
    std::vector<float> secondR(secondFrames, 0.0f);
    for (size_t i = 0; i < secondFrames; ++i) {
        secondL[i] = std::sin(omega * static_cast<float>(firstFrames + i));
        secondR[i] = secondL[i];
    }
    const auto after = processStereo(delay, secondL, secondR);

    float maxDelta = std::abs(after.left.front() - before.left.back());
    for (size_t i = 1; i < 1024; ++i)
        maxDelta = std::max(maxDelta, std::abs(after.left[i] - after.left[i - 1]));
    if (maxDelta > 0.12f) {
        std::cerr << "Delay-time change produced a discontinuity. max delta=" << maxDelta << "\n";
        return false;
    }
    return true;
}

bool testBypassAdvancesDelayState() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 0.0f);
    delay.setParameter(AestraDelay::kStereoShift, 0.5f);
    delay.setParameter(AestraDelay::kModDepth, 0.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.activate();

    std::vector<float> impulseL(100, 0.0f);
    std::vector<float> impulseR(100, 0.0f);
    impulseL[0] = 1.0f;
    impulseR[0] = 1.0f;
    processStereo(delay, impulseL, impulseR);

    delay.setParameter(AestraDelay::kBypass, 1.0f);
    std::vector<float> bypassL(600, 0.0f);
    std::vector<float> bypassR(600, 0.0f);
    const auto bypassed = processStereo(delay, bypassL, bypassR);
    if (*std::max_element(bypassed.left.begin(), bypassed.left.end()) != 0.0f) {
        std::cerr << "Bypass did not remain sample-transparent.\n";
        return false;
    }

    delay.setParameter(AestraDelay::kBypass, 0.0f);
    std::vector<float> resumedL(600, 0.0f);
    std::vector<float> resumedR(600, 0.0f);
    const auto resumed = processStereo(delay, resumedL, resumedR);
    float peak = 0.0f;
    for (float sample : resumed.left)
        peak = std::max(peak, std::abs(sample));
    if (peak > 1.0e-6f) {
        std::cerr << "A frozen pre-bypass repeat resurfaced after resume. peak=" << peak << "\n";
        return false;
    }
    return true;
}

bool testNonFiniteControlAndStateAreRejected() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kFeedback, 0.42f);
    delay.setParameter(AestraDelay::kFeedback, std::nanf(""));
    if (std::abs(delay.getParameter(AestraDelay::kFeedback) - 0.42f) > 1.0e-6f) {
        std::cerr << "Non-finite parameter input changed the live value.\n";
        return false;
    }

    auto state = delay.saveState();
    const float nanValue = std::nanf("");
    constexpr size_t feedbackOffset = sizeof(uint32_t) * 2 + sizeof(float) * AestraDelay::kFeedback;
    std::memcpy(state.data() + feedbackOffset, &nanValue, sizeof(nanValue));
    delay.setParameter(AestraDelay::kTime, 0.73f);
    if (delay.loadState(state)) {
        std::cerr << "Delay accepted a state containing NaN.\n";
        return false;
    }
    if (std::abs(delay.getParameter(AestraDelay::kTime) - 0.73f) > 1.0e-6f) {
        std::cerr << "Rejected state partially mutated parameters.\n";
        return false;
    }
    return true;
}

bool testNonFiniteAudioIsContained() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 1.0f);
    delay.setParameter(AestraDelay::kMix, 1.0f);
    delay.activate();

    std::vector<float> inL(1024, 0.0f);
    std::vector<float> inR(1024, 0.0f);
    inL[0] = std::nanf("");
    inR[1] = std::numeric_limits<float>::infinity();
    const auto out = processStereo(delay, inL, inR);
    for (size_t i = 0; i < out.left.size(); ++i) {
        if (!std::isfinite(out.left[i]) || !std::isfinite(out.right[i])) {
            std::cerr << "Non-finite audio escaped containment at sample " << i << ".\n";
            return false;
        }
    }
    return true;
}

bool testDryPathIsNotSaturatedAtZeroMix() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kMix, 0.0f);
    delay.setParameter(AestraDelay::kFeedback, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(512, 2.25f);
    std::vector<float> inR(512, -2.25f);
    auto out = processStereo(delay, inL, inR);
    if (std::abs(out.left.back() - 2.25f) > 1.0e-6f || std::abs(out.right.back() + 2.25f) > 1.0e-6f) {
        std::cerr << "Dry path was altered at zero mix. L=" << out.left.back() << " R=" << out.right.back() << "\n";
        return false;
    }
    return true;
}

bool testOutputTrimAppliesAtZeroMix() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kMix, 0.0f);
    delay.setParameter(AestraDelay::kOutputTrim, 1.0f);
    delay.setParameter(AestraDelay::kBypass, 0.0f);
    delay.activate();

    std::vector<float> inL(64, 0.25f);
    std::vector<float> inR(64, -0.25f);
    auto out = processStereo(delay, inL, inR);
    const float expected = 0.25f * std::pow(10.0f, 12.0f / 20.0f);
    if (std::abs(out.left.back() - expected) > 1.0e-5f || std::abs(out.right.back() + expected) > 1.0e-5f) {
        std::cerr << "Output trim did not apply at zero mix. L=" << out.left.back() << " R=" << out.right.back()
                  << " expected=" << expected << "\n";
        return false;
    }
    return true;
}

bool testFeedbackHighpassReducesLowFrequencyRepeats() {
    auto renderEnergy = [](float lowCut) {
        AestraDelay delay;
        delay.initialize(48000.0, 512);
        delay.setParameter(AestraDelay::kTime, 0.0f);
        delay.setParameter(AestraDelay::kFeedback, 0.85f);
        delay.setParameter(AestraDelay::kDamping, 0.0f);
        delay.setParameter(AestraDelay::kStereoShift, 0.5f);
        delay.setParameter(AestraDelay::kModDepth, 0.0f);
        delay.setParameter(AestraDelay::kMix, 1.0f);
        delay.setParameter(AestraDelay::kFeedbackHighpass, lowCut);
        delay.setParameter(AestraDelay::kOutputTrim, 0.5f);
        delay.setParameter(AestraDelay::kBypass, 0.0f);
        delay.activate();

        std::vector<float> inL(4800, 0.0f);
        std::vector<float> inR(4800, 0.0f);
        for (size_t i = 0; i < 480; ++i) {
            inL[i] = 1.0f;
            inR[i] = 1.0f;
        }

        auto out = processStereo(delay, inL, inR);
        float energy = 0.0f;
        for (size_t i = 1440; i < out.left.size(); ++i) {
            energy += std::abs(out.left[i]) + std::abs(out.right[i]);
        }
        return energy;
    };

    const float lowCutMin = renderEnergy(0.0f);
    const float lowCutMax = renderEnergy(1.0f);
    if (!(lowCutMax < lowCutMin * 0.45f)) {
        std::cerr << "Feedback high-pass did not reduce low-frequency repeat energy. min/max=" << lowCutMin << "/"
                  << lowCutMax << "\n";
        return false;
    }
    return true;
}

bool testVersion2StateLoadsWithNewDefaults() {
    struct LegacyStateBlobV2 {
        uint32_t magic;
        uint32_t version;
        float params[11];
    } blob{};

    blob.magic = AestraDelay::kStateMagicV2;
    blob.version = 2;
    blob.params[AestraDelay::kTime] = 0.2f;
    blob.params[AestraDelay::kFeedback] = 0.4f;
    blob.params[AestraDelay::kMix] = 0.7f;
    blob.params[AestraDelay::kNoteDivision] = AestraDelay::noteDivisionParamFromIndex(AestraDelay::kDiv1_4);

    std::vector<uint8_t> state(sizeof(blob));
    std::memcpy(state.data(), &blob, sizeof(blob));

    AestraDelay delay;
    delay.initialize(48000.0, 512);
    if (!delay.loadState(state)) {
        std::cerr << "V2 state failed to load.\n";
        return false;
    }
    if (std::abs(delay.getParameter(AestraDelay::kFeedbackHighpass) - 0.0f) > 1.0e-6f ||
        std::abs(delay.getParameter(AestraDelay::kOutputTrim) - 0.5f) > 1.0e-6f ||
        std::abs(delay.getParameter(AestraDelay::kMix) - 0.7f) > 1.0e-6f) {
        std::cerr << "V2 migration defaults were not preserved. lowCut="
                  << delay.getParameter(AestraDelay::kFeedbackHighpass)
                  << " output=" << delay.getParameter(AestraDelay::kOutputTrim)
                  << " mix=" << delay.getParameter(AestraDelay::kMix) << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraDelay Upgrade Tests\n";
    if (!testSyncedEighthAt120Bpm())
        return 1;
    if (!testPingPongAlternates())
        return 1;
    if (!testPingPongSustainedInputDoesNotAccumulateLeft())
        return 1;
    if (!testHighFeedbackStaysFinite())
        return 1;
    if (!testHotWetRepeatIsClean())
        return 1;
    if (!testModulationMaxStaysFinite())
        return 1;
    if (!testDelayTimeChangesAreCrossfaded())
        return 1;
    if (!testBypassAdvancesDelayState())
        return 1;
    if (!testNonFiniteControlAndStateAreRejected())
        return 1;
    if (!testNonFiniteAudioIsContained())
        return 1;
    if (!testDryPathIsNotSaturatedAtZeroMix())
        return 1;
    if (!testOutputTrimAppliesAtZeroMix())
        return 1;
    if (!testFeedbackHighpassReducesLowFrequencyRepeats())
        return 1;
    if (!testVersion2StateLoadsWithNewDefaults())
        return 1;
    std::cout << "All AestraDelay upgrade tests passed.\n";
    return 0;
}
