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

StereoBuffer processStereo(AestraDelay& delay,
                           const std::vector<float>& inL,
                           const std::vector<float>& inR) {
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

void writeFloat(std::vector<uint8_t>& bytes, size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(float));
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
        std::cerr << "Synced 1/8 delay missed grid. peak=" << peak
                  << " value=" << peakValue << " expected=" << expected << "\n";
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

    if (firstL < 0.70f || firstR < 0.10f || firstR > 0.20f ||
        secondR < 0.25f || secondL < 0.05f || secondL > 0.20f) {
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
        std::cerr << "Sustained ping-pong accumulated channel imbalance. energy L/R="
                  << energyL << "/" << energyR << " ratio=" << ratio << "\n";
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
        std::cerr << "Hot wet repeat was altered. L=" << out.left[480]
                  << " R=" << out.right[480] << "\n";
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
        std::cerr << "Dry path was altered at zero mix. L=" << out.left.back()
                  << " R=" << out.right.back() << "\n";
        return false;
    }
    return true;
}

bool testNanStateDoesNotEnterParametersOrProcessing() {
    AestraDelay delay;
    delay.initialize(48000.0, 512);
    delay.setParameter(AestraDelay::kTime, 0.25f);
    delay.setParameter(AestraDelay::kMix, 1.0f);

    std::vector<uint8_t> state = delay.saveState();
    constexpr size_t kParamOffset = sizeof(uint32_t) * 2;
    writeFloat(state, kParamOffset + sizeof(float) * AestraDelay::kTime, std::numeric_limits<float>::quiet_NaN());
    writeFloat(state, kParamOffset + sizeof(float) * AestraDelay::kFeedback, 0.0f);

    AestraDelay restored;
    restored.initialize(48000.0, 512);
    if (!restored.loadState(state)) {
        std::cerr << "AestraDelay rejected otherwise valid state containing a NaN parameter.\n";
        return false;
    }
    if (restored.getParameter(AestraDelay::kTime) != 0.25f) {
        std::cerr << "AestraDelay accepted NaN time parameter from state.\n";
        return false;
    }

    restored.activate();
    std::vector<float> inL(1024, 0.0f);
    std::vector<float> inR(1024, 0.0f);
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    auto out = processStereo(restored, inL, inR);
    for (size_t i = 0; i < out.left.size(); ++i) {
        if (!std::isfinite(out.left[i]) || !std::isfinite(out.right[i])) {
            std::cerr << "NaN state produced non-finite output at sample " << i << ".\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraDelay Upgrade Tests\n";
    if (!testSyncedEighthAt120Bpm()) return 1;
    if (!testPingPongAlternates()) return 1;
    if (!testPingPongSustainedInputDoesNotAccumulateLeft()) return 1;
    if (!testHighFeedbackStaysFinite()) return 1;
    if (!testHotWetRepeatIsClean()) return 1;
    if (!testModulationMaxStaysFinite()) return 1;
    if (!testDryPathIsNotSaturatedAtZeroMix()) return 1;
    if (!testNanStateDoesNotEnterParametersOrProcessing()) return 1;
    std::cout << "All AestraDelay upgrade tests passed.\n";
    return 0;
}
