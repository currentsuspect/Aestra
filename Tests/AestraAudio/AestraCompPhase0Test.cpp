// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompPhase0Test — Validates Phase 0 cleanup fixes.
// Run: cmake --build build --target AestraCompPhase0Test && ./build/bin/AestraCompPhase0Test

#include "Plugin/AestraComp.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr double kTau = 6.28318530717958647692;
constexpr float kTolerance = 0.5f; // dB tolerance for level tests

void generateTone(float* buffer, uint32_t frames, double freq, float ampLinear) {
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        buffer[i] = static_cast<float>(std::sin(kTau * freq * t) * ampLinear);
    }
}

float linearToDb(float linear) {
    return linear > 1e-12f ? 20.0f * std::log10(linear) : -120.0f;
}

float calculateRMS(const float* buffer, uint32_t frames) {
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return static_cast<float>(std::sqrt(sum / frames));
}

float calculatePeak(const float* buffer, uint32_t frames) {
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// Process N blocks through compressor, return output buffer
std::vector<float> processBlocks(AestraComp& comp, const float* input,
                                  uint32_t numBlocks, uint32_t blockSize) {
    std::vector<float> output(numBlocks * blockSize, 0.0f);
    const float* inPtr = input;
    float* outPtr = output.data();

    for (uint32_t b = 0; b < numBlocks; ++b) {
        comp.process(&inPtr, &outPtr, 1, 1, blockSize);
        inPtr += blockSize;
        outPtr += blockSize;
    }
    return output;
}

// Stereo version
std::pair<std::vector<float>, std::vector<float>>
processStereoBlocks(AestraComp& comp, const float* inL, const float* inR,
                     uint32_t numBlocks, uint32_t blockSize) {
    uint32_t totalFrames = numBlocks * blockSize;
    std::vector<float> outL(totalFrames, 0.0f);
    std::vector<float> outR(totalFrames, 0.0f);

    for (uint32_t b = 0; b < numBlocks; ++b) {
        uint32_t offset = b * blockSize;
        const float* inputs[2] = { inL + offset, inR + offset };
        float* outputs[2] = { outL.data() + offset, outR.data() + offset };
        comp.process(inputs, outputs, 2, 2, blockSize);
    }
    return { outL, outR };
}

// =====================================================================
// V-1: Makeup gain applied exactly once
// Input: -12dB sine, threshold -20dB, ratio 4:1, makeup +6dB
// Expected: output ≈ -12 + 6 = -6dB (some compression applied)
// If double makeup: output would be ~0dB
// =====================================================================
bool testMakeupGainSingleApplication() {
    std::cout << "  [V-1] Makeup gain single application... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();

    // Settings
    comp.setParameter(AestraComp::kThreshold, 0.333f); // -60 + 0.333*60 = -40dB
    comp.setParameter(AestraComp::kRatio, 0.158f);     // 1 + 0.158*19 ≈ 4:1
    comp.setParameter(AestraComp::kAttack, 0.0f);      // fastest attack
    comp.setParameter(AestraComp::kRelease, 0.0f);     // fastest release
    comp.setParameter(AestraComp::kMakeup, 0.25f);     // 0.25*24 = +6dB
    comp.setParameter(AestraComp::kKnee, 0.0f);        // hard knee
    comp.setParameter(AestraComp::kMix, 1.0f);             // 100% wet
    comp.setParameter(AestraComp::kBypass, 0.0f);      // not bypassed

    // Input: -12dB sine ≈ 0.251 linear
    const uint32_t numBlocks = 20; // ~213ms at 48kHz, plenty for envelope to settle
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.251f);

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    // Measure output RMS (skip first 5 blocks for envelope settling)
    float outputRms = calculateRMS(output.data() + 5 * kBlockSize, (numBlocks - 5) * kBlockSize);
    float outputDb = linearToDb(outputRms);

    // With threshold -40dB and input -12dB, signal is 28dB above threshold
    // At 4:1 ratio, gain reduction ≈ 28 * (1 - 1/4) = 21dB
    // Output = -12 - 21 + 6 = -27dB (approximately)
    // With double makeup: -12 - 21 + 12 = -21dB
    float expectedDb = -12.0f - 21.0f + 6.0f; // ≈ -27dB

    if (std::abs(outputDb - expectedDb) > 5.0f) {
        std::cerr << "FAILED: output " << std::fixed << std::setprecision(1)
                  << outputDb << "dB, expected ~" << expectedDb
                  << "dB (diff=" << std::abs(outputDb - expectedDb) << "dB)\n";
        return false;
    }

    std::cout << "PASSED (output=" << std::fixed << std::setprecision(1)
              << outputDb << "dB)\n";
    return true;
}

// =====================================================================
// V-2: No hard output clamp
// Input: hot signal with high makeup, output should soft-saturate instead of
// flattening at exactly +/-1.0.
// =====================================================================
bool testNoHardClamp() {
    std::cout << "  [V-2] No hard output clamp... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();

    // Low threshold, high ratio, high makeup — should produce output > 0dB
    comp.setParameter(AestraComp::kThreshold, 0.0f);   // -60dB
    comp.setParameter(AestraComp::kRatio, 0.0f);       // 1:1 (no compression)
    comp.setParameter(AestraComp::kAttack, 0.5f);
    comp.setParameter(AestraComp::kRelease, 0.5f);
    comp.setParameter(AestraComp::kMakeup, 1.0f);      // +24dB
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);

    // Input: -6dB sine
    const uint32_t numBlocks = 10;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.5f);

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    // Output peak should approach 1.0 without hard-clamping to exactly 1.0.
    float peak = calculatePeak(output.data() + 2 * kBlockSize, (numBlocks - 2) * kBlockSize);

    if (peak < 0.90f || peak >= 1.0f) {
        std::cerr << "FAILED: peak " << std::fixed << std::setprecision(4)
                  << peak << " outside soft-saturation range\n";
        return false;
    }

    std::cout << "PASSED (peak=" << std::fixed << std::setprecision(4) << peak << ")\n";
    return true;
}

// =====================================================================
// V-3: No zipper noise during attack automation
// Smooth transition when attack changes during playback
// =====================================================================
bool testNoZipperNoise() {
    std::cout << "  [V-3] No zipper noise during automation... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();

    comp.setParameter(AestraComp::kThreshold, 0.5f);   // -30dB
    comp.setParameter(AestraComp::kRatio, 0.3f);       // ~6.7:1
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);

    const uint32_t totalFrames = 48000; // 1 second
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.5f);

    // Process in blocks while ramping attack
    std::vector<float> output(totalFrames, 0.0f);
    const float* inPtr = input.data();
    float* outPtr = output.data();

    for (uint32_t b = 0; b < totalFrames / kBlockSize; ++b) {
        float t = static_cast<float>(b) / (totalFrames / kBlockSize);
        comp.setParameter(AestraComp::kAttack, t); // ramp 0→1
        comp.process(&inPtr, &outPtr, 1, 1, kBlockSize);
        inPtr += kBlockSize;
        outPtr += kBlockSize;
    }

    // Check for large sample-to-sample jumps (> 0.1 = ~-20dB jump = zipper)
    float maxJump = 0.0f;
    for (uint32_t i = 1; i < totalFrames; ++i) {
        float jump = std::abs(output[i] - output[i - 1]);
        if (jump > maxJump) maxJump = jump;
    }

    if (maxJump > 0.1f) {
        std::cerr << "FAILED: max sample jump " << std::fixed << std::setprecision(4)
                  << maxJump << " (> 0.1 = zipper noise)\n";
        return false;
    }

    std::cout << "PASSED (maxJump=" << std::fixed << std::setprecision(4) << maxJump << ")\n";
    return true;
}

// =====================================================================
// V-4: Bypass transparency
// Bypass on vs off with no compression — should be identical
// =====================================================================
bool testBypassTransparency() {
    std::cout << "  [V-4] Bypass transparency... ";

    const uint32_t totalFrames = kBlockSize * 4;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.3f);

    // Process with bypass ON
    AestraComp compBypass;
    compBypass.initialize(kSampleRate, kBlockSize);
    compBypass.activate();
    compBypass.setParameter(AestraComp::kBypass, 1.0f);

    auto outBypass = processBlocks(compBypass, input.data(), 4, kBlockSize);

    // Process with bypass OFF but no compression (threshold at -60dB, ratio 1:1)
    AestraComp compPass;
    compPass.initialize(kSampleRate, kBlockSize);
    compPass.activate();
    compPass.setParameter(AestraComp::kThreshold, 0.0f); // -60dB
    compPass.setParameter(AestraComp::kRatio, 0.0f);     // 1:1
    compPass.setParameter(AestraComp::kMakeup, 0.0f);
    compPass.setParameter(AestraComp::kMix, 1.0f);
    compPass.setParameter(AestraComp::kBypass, 0.0f);

    auto outPass = processBlocks(compPass, input.data(), 4, kBlockSize);

    // Compare (skip first block for envelope settling)
    float maxDiff = 0.0f;
    for (uint32_t i = kBlockSize; i < totalFrames; ++i) {
        float diff = std::abs(outBypass[i] - outPass[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    if (maxDiff > 1e-5f) {
        std::cerr << "FAILED: bypass vs passthrough diff " << std::fixed
                  << std::setprecision(6) << maxDiff << "\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

// =====================================================================
// V-5: Metadata consistency
// Param IDs match enum, display strings are valid, state round-trips
// =====================================================================
bool testMetadataConsistency() {
    std::cout << "  [V-5] Metadata consistency... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);

    // Check parameter count
    if (comp.getParameterCount() != AestraComp::kParamCount) {
        std::cerr << "FAILED: getParameterCount()=" << comp.getParameterCount()
                  << " != kParamCount=" << AestraComp::kParamCount << "\n";
        return false;
    }

    // Check getParameters() vector size and IDs
    auto params = comp.getParameters();
    if (params.size() != AestraComp::kParamCount) {
        std::cerr << "FAILED: getParameters() size=" << params.size()
                  << " != kParamCount=" << AestraComp::kParamCount << "\n";
        return false;
    }

    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        if (params[i].id != i) {
            std::cerr << "FAILED: param[" << i << "].id=" << params[i].id
                      << " (expected " << i << ")\n";
            return false;
        }
        if (params[i].name.empty()) {
            std::cerr << "FAILED: param[" << i << "] has empty name\n";
            return false;
        }
    }

    // Check display strings for all params
    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        std::string display = comp.getParameterDisplay(i);
        if (display.empty()) {
            std::cerr << "FAILED: param[" << i << "] has empty display\n";
            return false;
        }
    }

    // Check state round-trip
    comp.setParameter(AestraComp::kThreshold, 0.42f);
    comp.setParameter(AestraComp::kRatio, 0.33f);
    comp.setParameter(AestraComp::kAttack, 0.11f);
    comp.setParameter(AestraComp::kRelease, 0.77f);

    auto state = comp.saveState();

    AestraComp comp2;
    comp2.initialize(kSampleRate, kBlockSize);
    if (!comp2.loadState(state)) {
        std::cerr << "FAILED: loadState returned false\n";
        return false;
    }

    for (uint32_t i = 0; i < AestraComp::kParamCount; ++i) {
        float v1 = comp.getParameter(i);
        float v2 = comp2.getParameter(i);
        if (std::abs(v1 - v2) > 1e-6f) {
            std::cerr << "FAILED: state round-trip param[" << i << "]: "
                      << v1 << " != " << v2 << "\n";
            return false;
        }
    }

    std::cout << "PASSED\n";
    return true;
}

// =====================================================================
// V-6: Gain reduction metering works
// =====================================================================
bool testGRMetering() {
    std::cout << "  [V-6] Gain reduction metering... ";

    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();

    // Strong compression settings
    comp.setParameter(AestraComp::kThreshold, 0.5f);   // -30dB
    comp.setParameter(AestraComp::kRatio, 0.9f);       // ~18:1
    comp.setParameter(AestraComp::kAttack, 0.0f);
    comp.setParameter(AestraComp::kRelease, 0.5f);
    comp.setParameter(AestraComp::kMakeup, 0.0f);
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);

    // Hot input: -6dB
    const uint32_t numBlocks = 10;
    const uint32_t totalFrames = numBlocks * kBlockSize;
    std::vector<float> input(totalFrames);
    generateTone(input.data(), totalFrames, 1000.0, 0.5f);

    auto output = processBlocks(comp, input.data(), numBlocks, kBlockSize);

    float gr = comp.getCurrentGainReductionDb();

    // Should have significant GR (> 10dB)
    if (gr < 10.0f) {
        std::cerr << "FAILED: GR=" << std::fixed << std::setprecision(1)
                  << gr << "dB (expected > 10dB)\n";
        return false;
    }

    std::cout << "PASSED (GR=" << std::fixed << std::setprecision(1) << gr << "dB)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp Phase 0 Tests\n";
    std::cout << "========================\n";

    int passed = 0;
    int total = 6;

    if (testMakeupGainSingleApplication()) passed++;
    if (testNoHardClamp()) passed++;
    if (testNoZipperNoise()) passed++;
    if (testBypassTransparency()) passed++;
    if (testMetadataConsistency()) passed++;
    if (testGRMetering()) passed++;

    std::cout << "\n" << passed << "/" << total << " tests passed\n";

    return passed == total ? 0 : 1;
}
