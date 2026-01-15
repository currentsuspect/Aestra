// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NomadAudio.h"
#include <iostream>
#include <cmath>
#include <thread>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Nomad::Audio;

// Simple sine wave generator for testing
struct SineWaveData {
    double phase = 0.0;
    double frequency = 440.0; // A4 note
    double sampleRate = 48000.0;
};

// Audio callback that generates a sine wave
int sineWaveCallback(
    float* outputBuffer,
    const float* inputBuffer,
    uint32_t numFrames,
    double streamTime,
    void* userData
) {
    SineWaveData* data = static_cast<SineWaveData*>(userData);
    
    for (uint32_t i = 0; i < numFrames; ++i) {
        float sample = static_cast<float>(0.3 * std::sin(2.0 * M_PI * data->phase));
        
        // Stereo output
        outputBuffer[i * 2 + 0] = sample; // Left
        outputBuffer[i * 2 + 1] = sample; // Right
        
        // Advance phase
        data->phase += data->frequency / data->sampleRate;
        if (data->phase >= 1.0) {
            data->phase -= 1.0;
        }
    }
    
    return 0;
}

int main() {
    std::cout << "=================================\n";
    std::cout << "  NomadAudio Test Application\n";
    std::cout << "=================================\n";
    std::cout << "Version: " << getVersion() << "\n";
    std::cout << "Backend: " << getBackendName() << "\n";
    std::cout << "=================================\n\n";

    // Initialize audio device manager
    AudioDeviceManager manager;
    
    std::cout << "Initializing audio system...\n";
    if (!manager.initialize()) {
        std::cerr << "ERROR: Failed to initialize audio system!\n";
        return 1;
    }
    std::cout << "âœ“ Audio system initialized\n\n";

    // List available devices
    std::cout << "Available Audio Devices:\n";
    std::cout << "------------------------\n";
    auto devices = manager.getDevices();
    for (const auto& device : devices) {
        std::cout << "Device " << device.id << ": " << device.name << "\n";
        std::cout << "  Input channels: " << device.maxInputChannels << "\n";
        std::cout << "  Output channels: " << device.maxOutputChannels << "\n";
        std::cout << "  Preferred sample rate: " << device.preferredSampleRate << " Hz\n";
        if (device.isDefaultOutput) {
            std::cout << "  [DEFAULT OUTPUT]\n";
        }
        if (device.isDefaultInput) {
            std::cout << "  [DEFAULT INPUT]\n";
        }
        std::cout << "\n";
    }

    // Get default output device
    auto defaultDevice = manager.getDefaultOutputDevice();
    std::cout << "Using default output device: " << defaultDevice.name << "\n\n";

    // Configure audio stream
    AudioStreamConfig config;
    config.deviceId = defaultDevice.id;
    config.sampleRate = 48000;
    config.bufferSize = 512;
    config.numOutputChannels = 2;
    config.numInputChannels = 0;

    std::cout << "Opening audio stream...\n";
    std::cout << "  Sample rate: " << config.sampleRate << " Hz\n";
    std::cout << "  Buffer size: " << config.bufferSize << " frames\n";
    std::cout << "  Channels: " << config.numOutputChannels << "\n";

    SineWaveData sineData;
    sineData.sampleRate = config.sampleRate;

    if (!manager.openStream(config, sineWaveCallback, &sineData)) {
        std::cerr << "ERROR: Failed to open audio stream!\n";
        return 1;
    }
    std::cout << "âœ“ Audio stream opened\n";
    std::cout << "  Latency: " << (manager.getStreamLatency() * 1000.0) << " ms\n\n";

    // Start audio stream
    std::cout << "Starting audio stream...\n";
    if (!manager.startStream()) {
        std::cerr << "ERROR: Failed to start audio stream!\n";
        return 1;
    }
    std::cout << "âœ“ Audio stream started\n\n";

    // Play sine wave for 3 seconds
    std::cout << "Playing 440 Hz sine wave for 3 seconds...\n";
    std::cout << "(You should hear a tone)\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Stop audio stream
    std::cout << "Stopping audio stream...\n";
    manager.stopStream();
    std::cout << "âœ“ Audio stream stopped\n\n";

    // Cleanup
    manager.closeStream();
    manager.shutdown();

    std::cout << "=================================\n";
    std::cout << "  Test completed successfully!\n";
    std::cout << "=================================\n";

    return 0;
}
