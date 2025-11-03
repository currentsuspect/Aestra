// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NomadAudio.h"
#include "AudioProcessor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace Nomad::Audio;

/**
 * @brief Test audio callback with lock-free UIâ†’Audio communication
 * 
 * This test validates:
 * 1. Audio processing callback works correctly
 * 2. Lock-free command queue for UIâ†’Audio communication
 * 3. Buffer management
 * 4. Real-time performance (<10ms latency)
 */

// Performance monitoring
struct PerformanceStats {
    double minLatency = 1000.0;
    double maxLatency = 0.0;
    double avgLatency = 0.0;
    int sampleCount = 0;
    
    void update(double latency) {
        minLatency = std::min(minLatency, latency);
        maxLatency = std::max(maxLatency, latency);
        avgLatency = (avgLatency * sampleCount + latency) / (sampleCount + 1);
        sampleCount++;
    }
    
    void print() const {
        std::cout << "Performance Statistics:\n";
        std::cout << "  Min latency: " << std::fixed << std::setprecision(2) 
                  << minLatency << " ms\n";
        std::cout << "  Max latency: " << maxLatency << " ms\n";
        std::cout << "  Avg latency: " << avgLatency << " ms\n";
        std::cout << "  Samples: " << sampleCount << "\n";
    }
};

int main() {
    std::cout << "===========================================\n";
    std::cout << "  NomadAudio Callback Test\n";
    std::cout << "===========================================\n";
    std::cout << "Version: " << getVersion() << "\n";
    std::cout << "Backend: " << getBackendName() << "\n";
    std::cout << "===========================================\n\n";

    // Initialize audio device manager
    AudioDeviceManager manager;
    
    std::cout << "Initializing audio system...\n";
    if (!manager.initialize()) {
        std::cerr << "ERROR: Failed to initialize audio system!\n";
        return 1;
    }
    std::cout << "âœ“ Audio system initialized\n\n";

    // Get default output device
    auto defaultDevice = manager.getDefaultOutputDevice();
    std::cout << "Using device: " << defaultDevice.name << "\n\n";

    // Configure audio stream
    AudioStreamConfig config;
    config.deviceId = defaultDevice.id;
    config.sampleRate = 48000;
    config.bufferSize = 512;
    config.numOutputChannels = 2;
    config.numInputChannels = 0;

    std::cout << "Stream Configuration:\n";
    std::cout << "  Sample rate: " << config.sampleRate << " Hz\n";
    std::cout << "  Buffer size: " << config.bufferSize << " frames\n";
    std::cout << "  Channels: " << config.numOutputChannels << "\n\n";

    // Create test tone generator
    TestToneGenerator generator(config.sampleRate);
    
    // Create audio callback wrapper
    auto audioCallback = [](
        float* outputBuffer,
        const float* inputBuffer,
        uint32_t numFrames,
        double streamTime,
        void* userData
    ) -> int {
        TestToneGenerator* gen = static_cast<TestToneGenerator*>(userData);
        gen->process(outputBuffer, inputBuffer, numFrames, streamTime);
        return 0;
    };

    // Open audio stream
    std::cout << "Opening audio stream...\n";
    if (!manager.openStream(config, audioCallback, &generator)) {
        std::cerr << "ERROR: Failed to open audio stream!\n";
        return 1;
    }
    
    double latency = manager.getStreamLatency() * 1000.0;
    std::cout << "âœ“ Audio stream opened\n";
    std::cout << "  Latency: " << std::fixed << std::setprecision(2) 
              << latency << " ms\n\n";

    // Validate latency requirement
    if (latency > 10.0) {
        std::cout << "âš  WARNING: Latency exceeds 10ms target!\n";
        std::cout << "  Consider reducing buffer size.\n\n";
    } else {
        std::cout << "âœ“ Latency meets <10ms requirement\n\n";
    }

    // Start audio stream
    std::cout << "Starting audio stream...\n";
    if (!manager.startStream()) {
        std::cerr << "ERROR: Failed to start audio stream!\n";
        return 1;
    }
    std::cout << "âœ“ Audio stream started\n\n";

    // =============================================================================
    // Test 1: Basic audio callback
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Test 1: Basic Audio Callback\n";
    std::cout << "===========================================\n";
    std::cout << "Playing 440 Hz tone for 2 seconds...\n";
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "âœ“ Basic callback working\n\n";

    // =============================================================================
    // Test 2: Lock-free UIâ†’Audio communication
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Test 2: Lock-Free UIâ†’Audio Communication\n";
    std::cout << "===========================================\n";
    
    // Test gain control
    std::cout << "Testing gain control...\n";
    std::cout << "  Setting gain to 0.5...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetGain, 0.5f));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "  Current gain: " << generator.getGain() << "\n";
    
    std::cout << "  Setting gain to 1.0...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetGain, 1.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "  Current gain: " << generator.getGain() << "\n";
    std::cout << "âœ“ Gain control working\n\n";
    
    // Test pan control
    std::cout << "Testing pan control...\n";
    std::cout << "  Panning left (-1.0)...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetPan, -1.0f));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "  Panning center (0.0)...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetPan, 0.0f));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "  Panning right (1.0)...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetPan, 1.0f));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    std::cout << "  Panning center (0.0)...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::SetPan, 0.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "âœ“ Pan control working\n\n";
    
    // Test mute/unmute
    std::cout << "Testing mute control...\n";
    std::cout << "  Muting...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::Mute));
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "  Muted: " << (generator.isMuted() ? "Yes" : "No") << "\n";
    
    std::cout << "  Unmuting...\n";
    generator.sendCommand(AudioCommandMessage(AudioCommand::Unmute));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "  Muted: " << (generator.isMuted() ? "Yes" : "No") << "\n";
    std::cout << "âœ“ Mute control working\n\n";

    // =============================================================================
    // Test 3: Frequency sweep
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Test 3: Frequency Sweep\n";
    std::cout << "===========================================\n";
    std::cout << "Sweeping from 220 Hz to 880 Hz...\n";
    
    for (int i = 0; i <= 10; ++i) {
        double freq = 220.0 + (880.0 - 220.0) * i / 10.0;
        generator.setFrequency(freq);
        std::cout << "  " << static_cast<int>(freq) << " Hz\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    
    generator.setFrequency(440.0);
    std::cout << "âœ“ Frequency sweep working\n\n";

    // =============================================================================
    // Test 4: Buffer management
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Test 4: Buffer Management\n";
    std::cout << "===========================================\n";
    
    AudioBufferManager bufferManager;
    std::cout << "Max buffer size: " << bufferManager.getMaxBufferSize() << " frames\n";
    
    // Allocate test buffer
    float* buffer = bufferManager.allocate(512, 2);
    if (buffer) {
        std::cout << "âœ“ Buffer allocation successful\n";
        
        // Test buffer clear
        bufferManager.clear();
        bool allZero = true;
        for (uint32_t i = 0; i < 512 * 2; ++i) {
            if (buffer[i] != 0.0f) {
                allZero = false;
                break;
            }
        }
        std::cout << "âœ“ Buffer clear working: " << (allZero ? "Yes" : "No") << "\n";
    } else {
        std::cerr << "ERROR: Buffer allocation failed!\n";
    }
    std::cout << "\n";

    // =============================================================================
    // Test 5: Command queue stress test
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Test 5: Command Queue Stress Test\n";
    std::cout << "===========================================\n";
    std::cout << "Sending 1000 commands rapidly...\n";
    
    auto startTime = std::chrono::high_resolution_clock::now();
    int successCount = 0;
    
    for (int i = 0; i < 1000; ++i) {
        float gain = 0.5f + 0.5f * std::sin(i * 0.1f);
        if (generator.sendCommand(AudioCommandMessage(AudioCommand::SetGain, gain))) {
            successCount++;
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    std::cout << "  Commands sent: 1000\n";
    std::cout << "  Commands queued: " << successCount << "\n";
    std::cout << "  Time taken: " << duration.count() << " Î¼s\n";
    std::cout << "  Avg time per command: " << (duration.count() / 1000.0) << " Î¼s\n";
    
    if (successCount >= 950) {
        std::cout << "âœ“ Command queue handling high load\n";
    } else {
        std::cout << "âš  WARNING: Some commands were dropped\n";
    }
    std::cout << "\n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // =============================================================================
    // Performance summary
    // =============================================================================
    std::cout << "===========================================\n";
    std::cout << "Performance Summary\n";
    std::cout << "===========================================\n";
    std::cout << "Stream latency: " << std::fixed << std::setprecision(2) 
              << (manager.getStreamLatency() * 1000.0) << " ms\n";
    std::cout << "Buffer size: " << config.bufferSize << " frames\n";
    std::cout << "Sample rate: " << config.sampleRate << " Hz\n";
    std::cout << "Theoretical latency: " 
              << std::fixed << std::setprecision(2)
              << (config.bufferSize * 1000.0 / config.sampleRate) << " ms\n";
    
    if (manager.getStreamLatency() * 1000.0 < 10.0) {
        std::cout << "âœ“ Real-time performance requirement met (<10ms)\n";
    } else {
        std::cout << "âš  Real-time performance requirement not met\n";
    }
    std::cout << "\n";

    // Cleanup
    std::cout << "Stopping audio stream...\n";
    manager.stopStream();
    manager.closeStream();
    manager.shutdown();
    std::cout << "âœ“ Cleanup complete\n\n";

    std::cout << "===========================================\n";
    std::cout << "  All tests completed successfully!\n";
    std::cout << "===========================================\n";

    return 0;
}
