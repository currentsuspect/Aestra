// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file FilterTest.cpp
 * @brief Test application for Filter DSP module
 * 
 * Tests:
 * - Low-pass filter frequency response
 * - High-pass filter frequency response
 * - Band-pass filter frequency response
 * - Resonance control
 * - Filter stability
 */

#include "Filter.h"
#include "Oscillator.h"
#include "AudioDeviceManager.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace Nomad::Audio;

// Test configuration
constexpr float SAMPLE_RATE = 44100.0f;
constexpr int BUFFER_SIZE = 512;

// Global objects for audio callback
Oscillator* g_oscillator = nullptr;
Filter* g_filter = nullptr;
bool g_running = true;

/**
 * @brief Audio callback - generates filtered oscillator output
 */
int audioCallback(float* outputBuffer, const float* /*inputBuffer*/, 
                  uint32_t nFrames, double /*streamTime*/, void* /*userData*/)
{
    float* output = outputBuffer;
    
    if (g_oscillator && g_filter) {
        for (unsigned int i = 0; i < nFrames; ++i) {
            float sample = g_oscillator->process();
            sample = g_filter->process(sample);
            
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
 * @brief Measure filter frequency response at a given frequency
 */
float measureResponse(Filter& filter, float testFreq) {
    Oscillator osc(SAMPLE_RATE);
    osc.setFrequency(testFreq);
    osc.setWaveform(WaveformType::Sine);
    
    filter.reset();
    
    // Let filter settle (1000 samples)
    for (int i = 0; i < 1000; ++i) {
        filter.process(osc.process());
    }
    
    // Measure RMS over next 1000 samples
    float sumSquares = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float sample = filter.process(osc.process());
        sumSquares += sample * sample;
    }
    
    return std::sqrt(sumSquares / 1000.0f);
}

/**
 * @brief Test low-pass filter
 */
bool testLowPassFilter() {
    std::cout << "\n[Test] Low-Pass Filter" << std::endl;
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::LowPass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    
    // Test frequencies
    float freq100Hz = measureResponse(filter, 100.0f);
    float freq1000Hz = measureResponse(filter, 1000.0f);
    float freq5000Hz = measureResponse(filter, 5000.0f);
    
    std::cout << "  100 Hz: " << freq100Hz << " (should be ~1.0)" << std::endl;
    std::cout << "  1000 Hz: " << freq1000Hz << " (cutoff)" << std::endl;
    std::cout << "  5000 Hz: " << freq5000Hz << " (should be attenuated)" << std::endl;
    
    // Verify low frequencies pass and high frequencies are attenuated
    bool passesLow = freq100Hz > 0.8f;
    bool attenuatesHigh = freq5000Hz < freq100Hz * 0.3f;
    
    std::cout << "  Low freq passes: " << (passesLow ? "âœ“" : "âœ—") << std::endl;
    std::cout << "  High freq attenuated: " << (attenuatesHigh ? "âœ“" : "âœ—") << std::endl;
    
    return passesLow && attenuatesHigh;
}

/**
 * @brief Test high-pass filter
 */
bool testHighPassFilter() {
    std::cout << "\n[Test] High-Pass Filter" << std::endl;
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::HighPass);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);
    
    // Test frequencies
    float freq100Hz = measureResponse(filter, 100.0f);
    float freq1000Hz = measureResponse(filter, 1000.0f);
    float freq5000Hz = measureResponse(filter, 5000.0f);
    
    std::cout << "  100 Hz: " << freq100Hz << " (should be attenuated)" << std::endl;
    std::cout << "  1000 Hz: " << freq1000Hz << " (cutoff)" << std::endl;
    std::cout << "  5000 Hz: " << freq5000Hz << " (should be ~1.0)" << std::endl;
    
    // Verify high frequencies pass and low frequencies are attenuated
    bool passesHigh = freq5000Hz > 0.8f;
    bool attenuatesLow = freq100Hz < freq5000Hz * 0.3f;
    
    std::cout << "  High freq passes: " << (passesHigh ? "âœ“" : "âœ—") << std::endl;
    std::cout << "  Low freq attenuated: " << (attenuatesLow ? "âœ“" : "âœ—") << std::endl;
    
    return passesHigh && attenuatesLow;
}

/**
 * @brief Test band-pass filter
 */
bool testBandPassFilter() {
    std::cout << "\n[Test] Band-Pass Filter" << std::endl;
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::BandPass);
    filter.setCutoff(1000.0f);
    filter.setResonance(2.0f);  // Higher Q for narrower band
    
    // Test frequencies
    float freq100Hz = measureResponse(filter, 100.0f);
    float freq1000Hz = measureResponse(filter, 1000.0f);
    float freq5000Hz = measureResponse(filter, 5000.0f);
    
    std::cout << "  100 Hz: " << freq100Hz << " (should be attenuated)" << std::endl;
    std::cout << "  1000 Hz: " << freq1000Hz << " (center, should be highest)" << std::endl;
    std::cout << "  5000 Hz: " << freq5000Hz << " (should be attenuated)" << std::endl;
    
    // Verify center frequency passes and others are attenuated
    bool passesBand = freq1000Hz > freq100Hz && freq1000Hz > freq5000Hz;
    bool attenuatesSides = freq100Hz < freq1000Hz * 0.5f && freq5000Hz < freq1000Hz * 0.5f;
    
    std::cout << "  Center freq highest: " << (passesBand ? "âœ“" : "âœ—") << std::endl;
    std::cout << "  Sides attenuated: " << (attenuatesSides ? "âœ“" : "âœ—") << std::endl;
    
    return passesBand && attenuatesSides;
}

/**
 * @brief Test resonance control
 */
bool testResonanceControl() {
    std::cout << "\n[Test] Resonance Control" << std::endl;
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::LowPass);
    filter.setCutoff(1000.0f);
    
    // Test with low resonance
    filter.setResonance(0.5f);
    float lowRes = measureResponse(filter, 1000.0f);
    
    // Test with high resonance
    filter.setResonance(5.0f);
    float highRes = measureResponse(filter, 1000.0f);
    
    std::cout << "  Low resonance (0.5): " << lowRes << std::endl;
    std::cout << "  High resonance (5.0): " << highRes << std::endl;
    
    // High resonance should boost cutoff frequency
    bool resonanceWorks = highRes > lowRes * 1.5f;
    
    std::cout << "  Resonance boosts cutoff: " << (resonanceWorks ? "âœ“" : "âœ—") << std::endl;
    
    return resonanceWorks;
}

/**
 * @brief Test filter stability
 */
bool testFilterStability() {
    std::cout << "\n[Test] Filter Stability" << std::endl;
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::LowPass);
    filter.setCutoff(1000.0f);
    filter.setResonance(8.0f);  // High resonance
    
    // Feed impulse and check for stability
    filter.reset();
    filter.process(1.0f);  // Impulse
    
    bool stable = true;
    float maxOutput = 0.0f;
    
    for (int i = 0; i < 10000; ++i) {
        float output = filter.process(0.0f);
        maxOutput = std::max(maxOutput, std::abs(output));
        
        // Check for instability (output growing unbounded)
        if (std::abs(output) > 100.0f || std::isnan(output) || std::isinf(output)) {
            stable = false;
            break;
        }
    }
    
    std::cout << "  Max output after impulse: " << maxOutput << std::endl;
    std::cout << "  Filter stable: " << (stable ? "âœ“" : "âœ—") << std::endl;
    
    return stable;
}

/**
 * @brief Interactive audio test
 */
void interactiveAudioTest() {
    std::cout << "\n[Interactive Test] Audio Output" << std::endl;
    std::cout << "Starting audio stream..." << std::endl;
    
    // Create oscillator and filter
    Oscillator osc(SAMPLE_RATE);
    osc.setFrequency(220.0f);  // A3
    osc.setWaveform(WaveformType::Saw);
    
    Filter filter(SAMPLE_RATE);
    filter.setType(FilterType::LowPass);
    filter.setCutoff(1000.0f);
    filter.setResonance(2.0f);
    
    g_oscillator = &osc;
    g_filter = &filter;
    
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
    std::cout << "  1 - Low-pass filter" << std::endl;
    std::cout << "  2 - High-pass filter" << std::endl;
    std::cout << "  3 - Band-pass filter" << std::endl;
    std::cout << "  + - Increase cutoff" << std::endl;
    std::cout << "  - - Decrease cutoff" << std::endl;
    std::cout << "  r - Increase resonance" << std::endl;
    std::cout << "  q - Quit" << std::endl;
    
    // Interactive loop
    char input;
    while (g_running) {
        std::cout << "\n[Cutoff: " << filter.getCutoff() << " Hz, ";
        std::cout << "Resonance: " << filter.getResonance() << "] > ";
        std::cin >> input;
        
        switch (input) {
            case '1':
                filter.setType(FilterType::LowPass);
                std::cout << "Switched to Low-pass filter" << std::endl;
                break;
            case '2':
                filter.setType(FilterType::HighPass);
                std::cout << "Switched to High-pass filter" << std::endl;
                break;
            case '3':
                filter.setType(FilterType::BandPass);
                std::cout << "Switched to Band-pass filter" << std::endl;
                break;
            case '+':
                filter.setCutoff(filter.getCutoff() * 1.5f);
                std::cout << "Cutoff: " << filter.getCutoff() << " Hz" << std::endl;
                break;
            case '-':
                filter.setCutoff(filter.getCutoff() / 1.5f);
                std::cout << "Cutoff: " << filter.getCutoff() << " Hz" << std::endl;
                break;
            case 'r':
            case 'R':
                filter.setResonance(std::min(filter.getResonance() + 1.0f, 10.0f));
                std::cout << "Resonance: " << filter.getResonance() << std::endl;
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
    g_filter = nullptr;
    
    std::cout << "\nâœ“ Audio stream stopped" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  NomadAudio - Filter Test" << std::endl;
    std::cout << "========================================" << std::endl;
    
    bool allPassed = true;
    
    // Run automated tests
    allPassed &= testLowPassFilter();
    allPassed &= testHighPassFilter();
    allPassed &= testBandPassFilter();
    allPassed &= testResonanceControl();
    allPassed &= testFilterStability();
    
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
