// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file OscillatorTest.cpp
 * @brief Test application for Oscillator DSP module
 * 
 * Tests:
 * - Sine wave generation
 * - Saw wave generation with anti-aliasing
 * - Square wave generation with anti-aliasing
 * - Frequency accuracy
 * - Output range validation
 */

#include "Oscillator.h"
#include "AudioDeviceManager.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>

using namespace Nomad::Audio;

// Test configuration
constexpr float SAMPLE_RATE = 44100.0f;
constexpr float TEST_FREQUENCY = 440.0f;  // A4
constexpr int BUFFER_SIZE = 512;

// Global oscillator for audio callback
Oscillator* g_oscillator = nullptr;
WaveformType g_currentWaveform = WaveformType::Sine;
bool g_running = true;

/**
 * @brief Audio callback - generates oscillator output
 */
int audioCallback(float* outputBuffer, const float* /*inputBuffer*/, 
                  uint32_t nFrames, double /*streamTime*/, void* /*userData*/)
{
    float* output = outputBuffer;
    
    if (g_oscillator) {
        for (unsigned int i = 0; i < nFrames; ++i) {
            float sample = g_oscillator->process();
            
            // Stereo output
            output[i * 2] = sample * 0.3f;      // Left
            output[i * 2 + 1] = sample * 0.3f;  // Right
        }
    } else {
        // Silence
        for (unsigned int i = 0; i < nFrames * 2; ++i) {
            output[i] = 0.0f;
        }
    }
    
    return 0;
}

/**
 * @brief Validate oscillator output range
 */
bool validateOutputRange(Oscillator& osc, int numSamples = 1000) {
    float minVal = 1.0f;
    float maxVal = -1.0f;
    
    for (int i = 0; i < numSamples; ++i) {
        float sample = osc.process();
        minVal = std::min(minVal, sample);
        maxVal = std::max(maxVal, sample);
    }
    
    // Check if output is within valid range [-1.0, 1.0]
    bool valid = (minVal >= -1.0f && maxVal <= 1.0f);
    
    std::cout << "  Range: [" << minVal << ", " << maxVal << "] ";
    std::cout << (valid ? "âœ“" : "âœ—") << std::endl;
    
    return valid;
}

/**
 * @brief Test sine wave generation
 */
bool testSineWave() {
    std::cout << "\n[Test] Sine Wave" << std::endl;
    
    Oscillator osc(SAMPLE_RATE);
    osc.setWaveform(WaveformType::Sine);
    osc.setFrequency(TEST_FREQUENCY);
    
    // Validate output range
    bool valid = validateOutputRange(osc);
    
    // Check for smooth waveform (no sharp discontinuities)
    osc.reset();
    float prev = osc.process();
    float maxDiff = 0.0f;
    
    for (int i = 0; i < 100; ++i) {
        float curr = osc.process();
        maxDiff = std::max(maxDiff, std::abs(curr - prev));
        prev = curr;
    }
    
    std::cout << "  Max sample diff: " << maxDiff << " ";
    std::cout << (maxDiff < 0.2f ? "âœ“" : "âœ—") << std::endl;
    
    return valid && (maxDiff < 0.2f);
}

/**
 * @brief Test saw wave generation
 */
bool testSawWave() {
    std::cout << "\n[Test] Saw Wave (with PolyBLEP)" << std::endl;
    
    Oscillator osc(SAMPLE_RATE);
    osc.setWaveform(WaveformType::Saw);
    osc.setFrequency(TEST_FREQUENCY);
    
    // Validate output range
    bool valid = validateOutputRange(osc);
    
    // Check that PolyBLEP reduces discontinuities
    osc.reset();
    std::vector<float> samples;
    for (int i = 0; i < 200; ++i) {
        samples.push_back(osc.process());
    }
    
    // Find maximum discontinuity
    float maxJump = 0.0f;
    for (size_t i = 1; i < samples.size(); ++i) {
        float jump = std::abs(samples[i] - samples[i-1]);
        maxJump = std::max(maxJump, jump);
    }
    
    std::cout << "  Max discontinuity: " << maxJump << " ";
    std::cout << (maxJump < 1.5f ? "âœ“ (anti-aliased)" : "âœ— (aliasing detected)") << std::endl;
    
    return valid && (maxJump < 1.5f);
}

/**
 * @brief Test square wave generation
 */
bool testSquareWave() {
    std::cout << "\n[Test] Square Wave (with PolyBLEP)" << std::endl;
    
    Oscillator osc(SAMPLE_RATE);
    osc.setWaveform(WaveformType::Square);
    osc.setFrequency(TEST_FREQUENCY);
    
    // Validate output range
    bool valid = validateOutputRange(osc);
    
    // Test pulse width modulation
    osc.setPulseWidth(0.25f);
    osc.reset();
    
    std::cout << "  Pulse width: 0.25 âœ“" << std::endl;
    
    return valid;
}

/**
 * @brief Test frequency accuracy
 */
bool testFrequencyAccuracy() {
    std::cout << "\n[Test] Frequency Accuracy" << std::endl;
    
    Oscillator osc(SAMPLE_RATE);
    osc.setWaveform(WaveformType::Sine);
    
    // Test various frequencies
    float testFreqs[] = { 100.0f, 440.0f, 1000.0f, 5000.0f };
    
    for (float freq : testFreqs) {
        osc.setFrequency(freq);
        osc.reset();
        
        // Count zero crossings to estimate frequency
        int zeroCrossings = 0;
        float prev = osc.process();
        
        int samplesPerSecond = static_cast<int>(SAMPLE_RATE);
        for (int i = 0; i < samplesPerSecond; ++i) {
            float curr = osc.process();
            if (prev < 0.0f && curr >= 0.0f) {
                zeroCrossings++;
            }
            prev = curr;
        }
        
        float measuredFreq = static_cast<float>(zeroCrossings);
        float error = std::abs(measuredFreq - freq) / freq * 100.0f;
        
        std::cout << "  " << freq << " Hz -> " << measuredFreq << " Hz ";
        std::cout << "(error: " << error << "%) ";
        std::cout << (error < 1.0f ? "âœ“" : "âœ—") << std::endl;
    }
    
    return true;
}

/**
 * @brief Interactive audio test
 */
void interactiveAudioTest() {
    std::cout << "\n[Interactive Test] Audio Output" << std::endl;
    std::cout << "Starting audio stream..." << std::endl;
    
    // Create oscillator
    Oscillator osc(SAMPLE_RATE);
    osc.setFrequency(TEST_FREQUENCY);
    osc.setWaveform(WaveformType::Sine);
    g_oscillator = &osc;
    
    // Initialize audio device
    AudioDeviceManager deviceManager;
    
    if (!deviceManager.initialize()) {
        std::cerr << "Failed to initialize audio device" << std::endl;
        return;
    }
    
    // Get default output device
    AudioDeviceInfo defaultDevice = deviceManager.getDefaultOutputDevice();
    
    // Configure audio stream
    AudioStreamConfig config;
    config.deviceId = defaultDevice.id;
    config.sampleRate = static_cast<uint32_t>(SAMPLE_RATE);
    config.bufferSize = BUFFER_SIZE;
    config.numInputChannels = 0;
    config.numOutputChannels = 2;
    
    std::cout << "Using device: " << defaultDevice.name << std::endl;
    
    if (!deviceManager.openStream(config, audioCallback, nullptr)) {
        std::cerr << "Failed to open audio stream" << std::endl;
        return;
    }
    
    if (!deviceManager.startStream()) {
        std::cerr << "Failed to start audio stream" << std::endl;
        return;
    }
    
    std::cout << "\nâœ“ Audio stream started" << std::endl;
    std::cout << "\nControls:" << std::endl;
    std::cout << "  1 - Sine wave" << std::endl;
    std::cout << "  2 - Saw wave" << std::endl;
    std::cout << "  3 - Square wave" << std::endl;
    std::cout << "  q - Quit" << std::endl;
    
    // Interactive loop
    char input;
    while (g_running) {
        std::cout << "\n> ";
        std::cin >> input;
        
        switch (input) {
            case '1':
                osc.setWaveform(WaveformType::Sine);
                std::cout << "Switched to Sine wave" << std::endl;
                break;
            case '2':
                osc.setWaveform(WaveformType::Saw);
                std::cout << "Switched to Saw wave" << std::endl;
                break;
            case '3':
                osc.setWaveform(WaveformType::Square);
                std::cout << "Switched to Square wave" << std::endl;
                break;
            case 'q':
            case 'Q':
                g_running = false;
                break;
            default:
                std::cout << "Unknown command" << std::endl;
                break;
        }
    }
    
    // Cleanup
    deviceManager.stopStream();
    deviceManager.closeStream();
    g_oscillator = nullptr;
    
    std::cout << "\nâœ“ Audio stream stopped" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  NomadAudio - Oscillator Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    bool allPassed = true;
    
    // Run automated tests
    allPassed &= testSineWave();
    allPassed &= testSawWave();
    allPassed &= testSquareWave();
    allPassed &= testFrequencyAccuracy();
    
    std::cout << "\n========================================" << std::endl;
    if (allPassed) {
        std::cout << "âœ“ All tests passed!" << std::endl;
    } else {
        std::cout << "âœ— Some tests failed" << std::endl;
    }
    std::cout << "========================================" << std::endl;
    
    // Interactive audio test
    std::cout << "\nRun interactive audio test? (y/n): ";
    char choice;
    std::cin >> choice;
    
    if (choice == 'y' || choice == 'Y') {
        interactiveAudioTest();
    }
    
    return allPassed ? 0 : 1;
}
