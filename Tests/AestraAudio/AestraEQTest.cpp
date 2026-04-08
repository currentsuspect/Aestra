// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQTest — Validates the parametric EQ plugin processes audio correctly.

#include "Plugin/AestraEQ.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr double kTau = 6.28318530717958647692;

// Generate sine wave
void generateTone(float* buffer, uint32_t frames, double freq, float amp, double phaseOffset = 0.0) {
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / kSampleRate + phaseOffset;
        buffer[i] = static_cast<float>(std::sin(kTau * freq * t) * amp);
    }
}

// Calculate RMS
float calculateRMS(const float* buffer, uint32_t frames) {
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return static_cast<float>(std::sqrt(sum / frames));
}

// Test 1: EQ initializes and passes audio when bypassed
bool testBypassPassthrough() {
    std::cout << "  [1/3] Bypass passthrough... ";
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    // Set bypass
    eq.setParameter(AestraEQ::kParamBypass, 1.0f);

    const uint32_t frames = kBlockSize;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.5f);

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    // Verify output matches input (bypass passthrough)
    float maxDiff = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float diff = std::abs(output[i] - input[i]);
        if (diff > maxDiff) maxDiff = diff;
    }

    if (maxDiff > 1e-6f) {
        std::cerr << "FAILED: bypass diff " << maxDiff << "\n";
        return false;
    }

    std::cout << "PASSED\n";
    return true;
}

// Test 2: Low cut actually attenuates low frequencies
bool testLowCutAttenuation() {
    std::cout << "  [2/3] Low cut attenuation... ";
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    // Disable ALL bands first
    for (uint32_t b = 0; b < AestraEQ::kNumBands; ++b) {
        eq.setParameter(b * 5, 0.0f); // disabled
    }

    // Configure ONLY Band 0 as Low Cut at 200Hz
    eq.setParameter(0, 1.0f);           // Band 0 enabled
    eq.setParameter(1, 1.0f / 7.0f);    // LowCut type (1/7)
    eq.setParameter(2, std::log10(200.0f / 20.0f) / std::log10(20000.0f / 20.0f)); // 200Hz
    eq.setParameter(3, 0.5f);           // Gain
    eq.setParameter(4, 0.7f);           // Q
    eq.setParameter(AestraEQ::kParamBypass, 0.0f); // Bypass OFF

    // Process in small blocks to catch NaN/inf
    const uint32_t totalFrames = kBlockSize * 4;
    std::vector<float> input(totalFrames, 0.0f);
    std::vector<float> output(totalFrames, 0.0f);
    generateTone(input.data(), totalFrames, 80.0, 0.5f);

    bool foundInf = false;
    uint32_t badBlock = 0;
    const float* inPtr = input.data();
    float* outPtr = output.data();

    // Process block by block
    for (uint32_t offset = 0; offset < totalFrames && !foundInf; offset += kBlockSize) {
        uint32_t frames = std::min(kBlockSize, totalFrames - offset);
        const float* blockIn = input.data() + offset;
        float* blockOut = output.data() + offset;
        eq.process(&blockIn, &blockOut, 1, 1, frames);

        for (uint32_t i = 0; i < frames; ++i) {
            if (std::isinf(blockOut[i]) || std::isnan(blockOut[i])) {
                foundInf = true;
                badBlock = offset + i;
                // Debug: print surrounding samples
                std::cerr << "\n    Debug at inf: in[" << i << "]=" << blockIn[i]
                          << " out[" << i << "]=" << blockOut[i] << "\n";
                if (i > 0) std::cerr << "    prev out=" << blockOut[i-1] << "\n";
                break;
            }
        }
    }

    const float inputRMS = calculateRMS(input.data(), totalFrames);
    const float outputRMS = calculateRMS(output.data(), totalFrames);

    if (outputRMS >= inputRMS * 0.5f) {
        std::cerr << "FAILED: low cut didn't attenuate enough. in="
                  << inputRMS << " out=" << outputRMS << "\n";
        return false;
    }

    std::cout << "PASSED (80Hz: " << inputRMS << " → " << outputRMS << ")\n";
    return true;
}

// Test 3: Bell boost actually boosts at target frequency
bool testBellBoost() {
    std::cout << "  [3/3] Bell boost at 1kHz... ";
    AestraEQ eq;
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();

    // Disable ALL bands first
    for (uint32_t b = 0; b < AestraEQ::kNumBands; ++b) {
        eq.setParameter(b * 5, 0.0f); // disabled
    }

    // Configure ONLY Band 3 as Bell at 1kHz with +12dB boost
    eq.setParameter(15, 1.0f);          // Band 3 enabled
    eq.setParameter(16, 0.0f);          // Bell type
    eq.setParameter(17, std::log10(1000.0f / 20.0f) / std::log10(20000.0f / 20.0f)); // 1kHz
    eq.setParameter(18, 0.833f);        // +12dB boost
    eq.setParameter(19, 0.5f);          // Q
    eq.setParameter(AestraEQ::kParamBypass, 0.0f); // Bypass OFF

    const uint32_t frames = kBlockSize * 4;
    std::vector<float> input(frames, 0.0f);
    std::vector<float> output(frames, 0.0f);
    generateTone(input.data(), frames, 1000.0, 0.3f); // 1kHz tone

    const float* inPtr = input.data();
    float* outPtr = output.data();
    eq.process(&inPtr, &outPtr, 1, 1, frames);

    const float inputRMS = calculateRMS(input.data(), frames);
    const float outputRMS = calculateRMS(output.data(), frames);

    if (outputRMS <= inputRMS) {
        std::cerr << "FAILED: bell didn't boost. in=" << inputRMS << " out=" << outputRMS << "\n";
        return false;
    }

    const float boostDb = 20.0f * std::log10(outputRMS / inputRMS + 1e-12f);
    std::cout << "PASSED (1kHz: " << inputRMS << " → " << outputRMS
              << ", +" << boostDb << " dB)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Aestra EQ Plugin Tests ===\n";

    bool allPassed = true;
    allPassed &= testBypassPassthrough();
    allPassed &= testLowCutAttenuation();
    allPassed &= testBellBoost();

    std::cout << "\n";
    if (allPassed) {
        std::cout << "All EQ tests passed.\n";
        return 0;
    } else {
        std::cout << "FAILED: one or more EQ tests failed.\n";
        return 1;
    }
}
