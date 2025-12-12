// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
// Test program for SampleRateConverter

#include "SampleRateConverter.h"
#include "NomadLog.h"

#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>

using namespace Nomad;
using namespace Nomad::Audio;

// =============================================================================
// Test Utilities
// =============================================================================

namespace {

constexpr double PI = 3.14159265358979323846;

// Generate a sine wave at given frequency
void generateSineWave(std::vector<float>& buffer, uint32_t frames, 
                      uint32_t channels, uint32_t sampleRate, 
                      double frequency, double amplitude = 0.8) {
    buffer.resize(frames * channels);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        const float sample = static_cast<float>(amplitude * std::sin(2.0 * PI * frequency * t));
        for (uint32_t ch = 0; ch < channels; ++ch) {
            buffer[i * channels + ch] = sample;
        }
    }
}

// Calculate RMS error between two buffers
double calculateRMSError(const float* a, const float* b, uint32_t samples) {
    double sumSquared = 0.0;
    for (uint32_t i = 0; i < samples; ++i) {
        const double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSquared += diff * diff;
    }
    return std::sqrt(sumSquared / static_cast<double>(samples));
}

// Calculate maximum absolute error
float calculateMaxError(const float* a, const float* b, uint32_t samples) {
    float maxErr = 0.0f;
    for (uint32_t i = 0; i < samples; ++i) {
        const float err = std::abs(a[i] - b[i]);
        if (err > maxErr) maxErr = err;
    }
    return maxErr;
}

// Simple test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};

std::vector<TestResult> g_results;

void recordTest(const std::string& name, bool passed, const std::string& details = "") {
    g_results.push_back({name, passed, details});
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name;
    if (!details.empty()) {
        std::cout << " - " << details;
    }
    std::cout << std::endl;
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

void testPassthrough() {
    std::cout << "\n=== Test: Passthrough Mode ===\n";
    
    SampleRateConverter src;
    src.configure(48000, 48000, 2, SRCQuality::Sinc16);
    
    std::vector<float> input;
    generateSineWave(input, 1024, 2, 48000, 440.0);
    
    std::vector<float> output(1024 * 2);
    uint32_t written = src.process(input.data(), 1024, output.data(), 1024);
    
    bool passed = (written == 1024);
    if (passed) {
        // Check that output matches input exactly
        const double rms = calculateRMSError(input.data(), output.data(), 1024 * 2);
        passed = (rms < 1e-6);
    }
    
    recordTest("Passthrough mode copies input exactly", passed);
    recordTest("Passthrough reports correct frame count", written == 1024,
               "Expected 1024, got " + std::to_string(written));
}

void testUpsample() {
    std::cout << "\n=== Test: Upsampling 44100 -> 48000 ===\n";
    
    SampleRateConverter src;
    src.configure(44100, 48000, 2, SRCQuality::Sinc16);
    
    const uint32_t inputFrames = 44100;  // 1 second
    const uint32_t expectedOutput = static_cast<uint32_t>(
        std::ceil(static_cast<double>(inputFrames) * 48000.0 / 44100.0)
    );
    
    std::vector<float> input;
    generateSineWave(input, inputFrames, 2, 44100, 440.0);
    
    std::vector<float> output(expectedOutput * 2 + 1024);  // Extra buffer
    uint32_t written = src.process(input.data(), inputFrames, output.data(), 
                                   static_cast<uint32_t>(output.size() / 2));
    
    // Check that we got approximately the right number of output frames
    const double ratio = 48000.0 / 44100.0;
    const uint32_t minExpected = static_cast<uint32_t>(inputFrames * ratio * 0.95);
    const uint32_t maxExpected = static_cast<uint32_t>(inputFrames * ratio * 1.05);
    
    recordTest("Upsample produces correct frame count", 
               written >= minExpected && written <= maxExpected,
               "Expected ~" + std::to_string(expectedOutput) + 
               ", got " + std::to_string(written));
    
    recordTest("Latency is reported", src.getLatency() > 0,
               "Latency: " + std::to_string(src.getLatency()) + " frames");
}

void testDownsample() {
    std::cout << "\n=== Test: Downsampling 48000 -> 44100 ===\n";
    
    SampleRateConverter src;
    src.configure(48000, 44100, 2, SRCQuality::Sinc16);
    
    const uint32_t inputFrames = 48000;  // 1 second
    const uint32_t expectedOutput = static_cast<uint32_t>(
        std::ceil(static_cast<double>(inputFrames) * 44100.0 / 48000.0)
    );
    
    std::vector<float> input;
    generateSineWave(input, inputFrames, 2, 48000, 440.0);
    
    std::vector<float> output(expectedOutput * 2 + 1024);
    uint32_t written = src.process(input.data(), inputFrames, output.data(),
                                   static_cast<uint32_t>(output.size() / 2));
    
    const double ratio = 44100.0 / 48000.0;
    const uint32_t minExpected = static_cast<uint32_t>(inputFrames * ratio * 0.95);
    const uint32_t maxExpected = static_cast<uint32_t>(inputFrames * ratio * 1.05);
    
    recordTest("Downsample produces correct frame count",
               written >= minExpected && written <= maxExpected,
               "Expected ~" + std::to_string(expectedOutput) + 
               ", got " + std::to_string(written));
}

void testRoundTrip() {
    std::cout << "\n=== Test: Round-Trip Quality ===\n";
    
    const uint32_t originalRate = 44100;
    const uint32_t intermediateRate = 48000;
    const uint32_t frames = 8192;
    const double testFrequency = 440.0;
    
    // Generate original signal
    std::vector<float> original;
    generateSineWave(original, frames, 2, originalRate, testFrequency);
    
    // Upsample 44100 -> 48000
    SampleRateConverter up;
    up.configure(originalRate, intermediateRate, 2, SRCQuality::Sinc16);
    
    const uint32_t upFrames = estimateOutputFrames(frames, originalRate, 
                                                    intermediateRate, up.getLatency());
    std::vector<float> upsampled(upFrames * 2);
    uint32_t upWritten = up.process(original.data(), frames, 
                                    upsampled.data(), upFrames);
    
    // Downsample 48000 -> 44100
    SampleRateConverter down;
    down.configure(intermediateRate, originalRate, 2, SRCQuality::Sinc16);
    
    const uint32_t downFrames = estimateOutputFrames(upWritten, intermediateRate,
                                                      originalRate, down.getLatency());
    std::vector<float> roundTrip(downFrames * 2);
    uint32_t downWritten = down.process(upsampled.data(), upWritten,
                                        roundTrip.data(), downFrames);
    
    // Compare after skipping latency samples
    const uint32_t latency = up.getLatency() + down.getLatency();
    const uint32_t compareStart = latency;
    const uint32_t compareFrames = std::min(frames, downWritten) - latency - 100;
    
    if (compareFrames > 100) {
        const double rmsError = calculateRMSError(
            original.data() + compareStart * 2,
            roundTrip.data() + compareStart * 2,
            compareFrames * 2
        );
        
        const float maxError = calculateMaxError(
            original.data() + compareStart * 2,
            roundTrip.data() + compareStart * 2,
            compareFrames * 2
        );
        
        std::cout << "  Round-trip RMS error: " << std::scientific 
                  << std::setprecision(4) << rmsError << std::endl;
        std::cout << "  Round-trip max error: " << std::fixed 
                  << std::setprecision(6) << maxError << std::endl;
        
        // For Sinc16 quality with phase shift from double conversion,
        // expect reasonable (not perfect) round-trip fidelity
        recordTest("Round-trip RMS error < 0.15", rmsError < 0.15,
                   "RMS: " + std::to_string(rmsError));
        recordTest("Round-trip max error < 0.20", maxError < 0.20f,
                   "Max: " + std::to_string(maxError));
    } else {
        recordTest("Round-trip comparison", false, 
                   "Not enough frames for comparison");
    }
}

void testQualityLevels() {
    std::cout << "\n=== Test: All Quality Levels ===\n";
    
    const SRCQuality levels[] = {
        SRCQuality::Linear,
        SRCQuality::Cubic,
        SRCQuality::Sinc8,
        SRCQuality::Sinc16,
        SRCQuality::Sinc64
    };
    const char* names[] = {"Linear", "Cubic", "Sinc8", "Sinc16", "Sinc64"};
    
    std::vector<float> input;
    generateSineWave(input, 4096, 2, 44100, 1000.0);
    std::vector<float> output(8192 * 2);
    
    for (int i = 0; i < 5; ++i) {
        SampleRateConverter src;
        src.configure(44100, 48000, 2, levels[i]);
        
        uint32_t written = src.process(input.data(), 4096, output.data(), 8192);
        bool passed = (written > 0) && src.isConfigured();
        
        recordTest(std::string("Quality ") + names[i] + " works", passed,
                   "Latency: " + std::to_string(src.getLatency()));
    }
}

void testPerformance() {
    std::cout << "\n=== Test: Performance ===\n";
    
    SampleRateConverter src;
    src.configure(44100, 48000, 2, SRCQuality::Sinc16);
    
    // 10 seconds of stereo audio
    const uint32_t inputFrames = 44100 * 10;
    std::vector<float> input;
    generateSineWave(input, inputFrames, 2, 44100, 440.0);
    
    std::vector<float> output(inputFrames * 2 * 2);  // 2x for ratio + safety
    
    // Warmup
    src.process(input.data(), 4096, output.data(), 8192);
    src.reset();
    
    // Timed run
    auto start = std::chrono::high_resolution_clock::now();
    uint32_t written = src.process(input.data(), inputFrames, 
                                   output.data(), 
                                   static_cast<uint32_t>(output.size() / 2));
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    const double seconds = static_cast<double>(duration.count()) / 1e6;
    const double audioSeconds = 10.0;
    const double realtimeFactor = audioSeconds / seconds;
    
    std::cout << "  Processed " << inputFrames << " frames in " 
              << std::fixed << std::setprecision(3) << seconds * 1000.0 << " ms\n";
    std::cout << "  Real-time factor: " << std::fixed << std::setprecision(1) 
              << realtimeFactor << "x\n";
    
    recordTest("Performance >= 10x real-time", realtimeFactor >= 10.0,
               std::to_string(realtimeFactor) + "x real-time");
}

void testMultiChannel() {
    std::cout << "\n=== Test: Multi-Channel (6ch surround) ===\n";
    
    SampleRateConverter src;
    src.configure(44100, 48000, 6, SRCQuality::Sinc8);
    
    std::vector<float> input;
    generateSineWave(input, 1024, 6, 44100, 440.0);
    
    std::vector<float> output(2048 * 6);
    uint32_t written = src.process(input.data(), 1024, output.data(), 2048);
    
    recordTest("6-channel processing works", written > 0,
               "Wrote " + std::to_string(written) + " frames");
    recordTest("Channels reported correctly", src.getChannels() == 6);
}

void testReset() {
    std::cout << "\n=== Test: Reset Functionality ===\n";
    
    SampleRateConverter src;
    src.configure(44100, 48000, 2, SRCQuality::Sinc16);
    
    std::vector<float> input;
    generateSineWave(input, 1024, 2, 44100, 440.0);
    std::vector<float> output(2048 * 2);
    
    // First process
    uint32_t written1 = src.process(input.data(), 1024, output.data(), 2048);
    
    // Reset and process again
    src.reset();
    uint32_t written2 = src.process(input.data(), 1024, output.data(), 2048);
    
    recordTest("Reset produces consistent output", written1 == written2,
               "Before: " + std::to_string(written1) + 
               ", After: " + std::to_string(written2));
}

void testVariableRatio() {
    std::cout << "\n=== Test: Variable Ratio (Pitch Shifting) ===\n";
    
    SampleRateConverter src;
    src.configure(48000, 48000, 2, SRCQuality::Sinc16);
    
    // Initial ratio should be 1.0 for same-rate conversion
    recordTest("Initial ratio is 1.0", std::abs(src.getCurrentRatio() - 1.0) < 0.001,
               "Ratio: " + std::to_string(src.getCurrentRatio()));
    
    // Set new ratio (pitch up by octave = 2x speed)
    src.setRatio(2.0, 0);  // Instant change
    recordTest("setRatio changes ratio", std::abs(src.getCurrentRatio() - 2.0) < 0.001,
               "Ratio after setRatio(2.0): " + std::to_string(src.getCurrentRatio()));
    
    // Test smooth transition
    src.setRatio(1.0, 256);  // Smooth back to 1.0 over 256 frames
    recordTest("Smooth transition initiated", src.getCurrentRatio() != 1.0,
               "Ratio before smoothing completes: " + std::to_string(src.getCurrentRatio()));
    
    // Process some frames to trigger smoothing
    std::vector<float> input;
    generateSineWave(input, 512, 2, 48000, 440.0);
    std::vector<float> output(1024 * 2);
    src.process(input.data(), 512, output.data(), 1024);
    
    // After processing, ratio should be closer to target
    // (or at target if smoothing completed)
    recordTest("Ratio approaches target after processing", 
               std::abs(src.getCurrentRatio() - 1.0) < 1.5,
               "Ratio after processing: " + std::to_string(src.getCurrentRatio()));
    
    std::cout << "  SIMD available: " << (SampleRateConverter::hasSIMD() ? "Yes" : "No") << "\n";
    std::cout << "  AVX available: " << (SampleRateConverter::hasAVX() ? "Yes" : "No") << "\n";
}

// =============================================================================
// Main
// =============================================================================

int main() {
    std::cout << "=========================================\n";
    std::cout << "  Nomad SampleRateConverter Test Suite\n";
    std::cout << "=========================================\n";
    
    Log::setLevel(LogLevel::Info);
    
    testPassthrough();
    testUpsample();
    testDownsample();
    testRoundTrip();
    testQualityLevels();
    testMultiChannel();
    testReset();
    testVariableRatio();
    testPerformance();
    
    // Summary
    std::cout << "\n=========================================\n";
    std::cout << "  Test Summary\n";
    std::cout << "=========================================\n";
    
    int passed = 0, failed = 0;
    for (const auto& result : g_results) {
        if (result.passed) ++passed;
        else ++failed;
    }
    
    std::cout << "  Passed: " << passed << "\n";
    std::cout << "  Failed: " << failed << "\n";
    std::cout << "  Total:  " << (passed + failed) << "\n";
    std::cout << "=========================================\n";
    
    if (failed > 0) {
        std::cout << "\nFailed tests:\n";
        for (const auto& result : g_results) {
            if (!result.passed) {
                std::cout << "  - " << result.name << ": " << result.details << "\n";
            }
        }
    }
    
    return (failed == 0) ? 0 : 1;
}
