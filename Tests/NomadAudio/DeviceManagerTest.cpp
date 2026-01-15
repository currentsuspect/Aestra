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
    double frequency = 440.0;
    double sampleRate = 48000.0;
};

int sineWaveCallback(
    float* outputBuffer,
    const float* inputBuffer,
    uint32_t numFrames,
    double streamTime,
    void* userData
) {
    (void)inputBuffer;
    (void)streamTime;
    
    SineWaveData* data = static_cast<SineWaveData*>(userData);
    
    for (uint32_t i = 0; i < numFrames; ++i) {
        float sample = static_cast<float>(0.3 * std::sin(2.0 * M_PI * data->phase));
        
        outputBuffer[i * 2 + 0] = sample;
        outputBuffer[i * 2 + 1] = sample;
        
        data->phase += data->frequency / data->sampleRate;
        if (data->phase >= 1.0) {
            data->phase -= 1.0;
        }
    }
    
    return 0;
}

void printDeviceInfo(const AudioDeviceInfo& device) {
    std::cout << "  Device " << device.id << ": " << device.name << "\n";
    std::cout << "    Input channels: " << device.maxInputChannels << "\n";
    std::cout << "    Output channels: " << device.maxOutputChannels << "\n";
    std::cout << "    Preferred sample rate: " << device.preferredSampleRate << " Hz\n";
    std::cout << "    Supported sample rates: ";
    for (size_t i = 0; i < device.supportedSampleRates.size(); ++i) {
        std::cout << device.supportedSampleRates[i];
        if (i < device.supportedSampleRates.size() - 1) {
            std::cout << ", ";
        }
    }
    std::cout << " Hz\n";
    if (device.isDefaultOutput) {
        std::cout << "    [DEFAULT OUTPUT]\n";
    }
    if (device.isDefaultInput) {
        std::cout << "    [DEFAULT INPUT]\n";
    }
}

bool testDeviceEnumeration(AudioDeviceManager& manager) {
    std::cout << "\n=== Test 1: Device Enumeration ===\n";
    
    auto devices = manager.getDevices();
    if (devices.empty()) {
        std::cerr << "âœ— FAILED: No audio devices found!\n";
        return false;
    }
    
    std::cout << "âœ“ Found " << devices.size() << " audio device(s)\n\n";
    for (const auto& device : devices) {
        printDeviceInfo(device);
        std::cout << "\n";
    }
    
    return true;
}

bool testDeviceSelection(AudioDeviceManager& manager) {
    std::cout << "=== Test 2: Device Selection ===\n";
    
    auto defaultOutput = manager.getDefaultOutputDevice();
    if (defaultOutput.name.empty()) {
        std::cerr << "âœ— FAILED: No default output device found!\n";
        return false;
    }
    
    std::cout << "âœ“ Default output device: " << defaultOutput.name << "\n";
    
    auto defaultInput = manager.getDefaultInputDevice();
    if (!defaultInput.name.empty()) {
        std::cout << "âœ“ Default input device: " << defaultInput.name << "\n";
    } else {
        std::cout << "  (No default input device available)\n";
    }
    
    return true;
}

bool testSampleRateConfiguration(AudioDeviceManager& manager, SineWaveData& sineData) {
    std::cout << "\n=== Test 3: Sample Rate Configuration ===\n";
    
    auto defaultDevice = manager.getDefaultOutputDevice();
    
    // Test different sample rates
    std::vector<uint32_t> testRates = {44100, 48000, 96000};
    
    for (uint32_t rate : testRates) {
        // Check if rate is supported
        bool supported = false;
        for (uint32_t supportedRate : defaultDevice.supportedSampleRates) {
            if (supportedRate == rate) {
                supported = true;
                break;
            }
        }
        
        if (!supported) {
            std::cout << "  Skipping " << rate << " Hz (not supported)\n";
            continue;
        }
        
        AudioStreamConfig config;
        config.deviceId = defaultDevice.id;
        config.sampleRate = rate;
        config.bufferSize = 512;
        config.numOutputChannels = 2;
        config.numInputChannels = 0;
        
        sineData.sampleRate = rate;
        
        std::cout << "  Testing " << rate << " Hz... ";
        
        if (!manager.openStream(config, sineWaveCallback, &sineData)) {
            std::cerr << "âœ— FAILED\n";
            return false;
        }
        
        if (!manager.startStream()) {
            std::cerr << "âœ— FAILED to start\n";
            manager.closeStream();
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        manager.stopStream();
        manager.closeStream();
        
        std::cout << "âœ“ OK\n";
    }
    
    return true;
}

bool testBufferSizeConfiguration(AudioDeviceManager& manager, SineWaveData& sineData) {
    std::cout << "\n=== Test 4: Buffer Size Configuration ===\n";
    
    auto defaultDevice = manager.getDefaultOutputDevice();
    
    // Test different buffer sizes
    std::vector<uint32_t> testBufferSizes = {128, 256, 512, 1024};
    
    for (uint32_t bufferSize : testBufferSizes) {
        AudioStreamConfig config;
        config.deviceId = defaultDevice.id;
        config.sampleRate = 48000;
        config.bufferSize = bufferSize;
        config.numOutputChannels = 2;
        config.numInputChannels = 0;
        
        sineData.sampleRate = 48000;
        
        std::cout << "  Testing buffer size " << bufferSize << " frames... ";
        
        if (!manager.openStream(config, sineWaveCallback, &sineData)) {
            std::cerr << "âœ— FAILED\n";
            return false;
        }
        
        if (!manager.startStream()) {
            std::cerr << "âœ— FAILED to start\n";
            manager.closeStream();
            return false;
        }
        
        double latency = manager.getStreamLatency();
        std::cout << "âœ“ OK (latency: " << (latency * 1000.0) << " ms)\n";
        
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        manager.stopStream();
        manager.closeStream();
    }
    
    return true;
}

bool testDeviceSwitching(AudioDeviceManager& manager, SineWaveData& sineData) {
    std::cout << "\n=== Test 5: Device Switching ===\n";
    
    auto devices = manager.getDevices();
    if (devices.size() < 2) {
        std::cout << "  Skipping (only one device available)\n";
        return true;
    }
    
    // Find two output devices
    std::vector<AudioDeviceInfo> outputDevices;
    for (const auto& device : devices) {
        if (device.maxOutputChannels >= 2) {
            outputDevices.push_back(device);
        }
    }
    
    if (outputDevices.size() < 2) {
        std::cout << "  Skipping (need at least 2 output devices)\n";
        return true;
    }
    
    // Open stream on first device
    AudioStreamConfig config;
    config.deviceId = outputDevices[0].id;
    config.sampleRate = 48000;
    config.bufferSize = 512;
    config.numOutputChannels = 2;
    config.numInputChannels = 0;
    
    sineData.sampleRate = 48000;
    
    std::cout << "  Opening stream on device: " << outputDevices[0].name << "\n";
    if (!manager.openStream(config, sineWaveCallback, &sineData)) {
        std::cerr << "âœ— FAILED to open stream\n";
        return false;
    }
    
    if (!manager.startStream()) {
        std::cerr << "âœ— FAILED to start stream\n";
        manager.closeStream();
        return false;
    }
    
    std::cout << "  âœ“ Playing on first device...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Switch to second device
    std::cout << "  Switching to device: " << outputDevices[1].name << "\n";
    if (!manager.switchDevice(outputDevices[1].id)) {
        std::cerr << "âœ— FAILED to switch device\n";
        manager.stopStream();
        manager.closeStream();
        return false;
    }
    
    std::cout << "  âœ“ Playing on second device...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Switch back to first device
    std::cout << "  Switching back to device: " << outputDevices[0].name << "\n";
    if (!manager.switchDevice(outputDevices[0].id)) {
        std::cerr << "âœ— FAILED to switch back\n";
        manager.stopStream();
        manager.closeStream();
        return false;
    }
    
    std::cout << "  âœ“ Playing on first device again...\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    manager.stopStream();
    manager.closeStream();
    
    std::cout << "âœ“ Device switching test passed\n";
    return true;
}

bool testDynamicConfiguration(AudioDeviceManager& manager, SineWaveData& sineData) {
    std::cout << "\n=== Test 6: Dynamic Configuration Changes ===\n";
    
    auto defaultDevice = manager.getDefaultOutputDevice();
    
    // Open initial stream
    AudioStreamConfig config;
    config.deviceId = defaultDevice.id;
    config.sampleRate = 48000;
    config.bufferSize = 512;
    config.numOutputChannels = 2;
    config.numInputChannels = 0;
    
    sineData.sampleRate = 48000;
    
    if (!manager.openStream(config, sineWaveCallback, &sineData)) {
        std::cerr << "âœ— FAILED to open stream\n";
        return false;
    }
    
    if (!manager.startStream()) {
        std::cerr << "âœ— FAILED to start stream\n";
        manager.closeStream();
        return false;
    }
    
    std::cout << "  Initial config: 48000 Hz, 512 frames\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Change buffer size
    std::cout << "  Changing buffer size to 256 frames... ";
    if (!manager.setBufferSize(256)) {
        std::cerr << "âœ— FAILED\n";
        manager.stopStream();
        manager.closeStream();
        return false;
    }
    std::cout << "âœ“ OK\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Change sample rate (if 44100 is supported)
    bool supports44100 = false;
    for (uint32_t rate : defaultDevice.supportedSampleRates) {
        if (rate == 44100) {
            supports44100 = true;
            break;
        }
    }
    
    if (supports44100) {
        std::cout << "  Changing sample rate to 44100 Hz... ";
        if (!manager.setSampleRate(44100)) {
            std::cerr << "âœ— FAILED\n";
            manager.stopStream();
            manager.closeStream();
            return false;
        }
        sineData.sampleRate = 44100;
        std::cout << "âœ“ OK\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    manager.stopStream();
    manager.closeStream();
    
    std::cout << "âœ“ Dynamic configuration test passed\n";
    return true;
}

int main() {
    std::cout << "=========================================\n";
    std::cout << "  NomadAudio Device Manager Test Suite\n";
    std::cout << "=========================================\n";
    std::cout << "Version: " << getVersion() << "\n";
    std::cout << "Backend: " << getBackendName() << "\n";
    std::cout << "=========================================\n";

    AudioDeviceManager manager;
    SineWaveData sineData;
    
    std::cout << "\nInitializing audio system...\n";
    if (!manager.initialize()) {
        std::cerr << "âœ— FAILED: Could not initialize audio system!\n";
        return 1;
    }
    std::cout << "âœ“ Audio system initialized\n";

    int failedTests = 0;
    int totalTests = 6;

    if (!testDeviceEnumeration(manager)) failedTests++;
    if (!testDeviceSelection(manager)) failedTests++;
    if (!testSampleRateConfiguration(manager, sineData)) failedTests++;
    if (!testBufferSizeConfiguration(manager, sineData)) failedTests++;
    if (!testDeviceSwitching(manager, sineData)) failedTests++;
    if (!testDynamicConfiguration(manager, sineData)) failedTests++;

    manager.shutdown();

    std::cout << "\n=========================================\n";
    std::cout << "  Test Results\n";
    std::cout << "=========================================\n";
    std::cout << "Passed: " << (totalTests - failedTests) << "/" << totalTests << "\n";
    std::cout << "Failed: " << failedTests << "/" << totalTests << "\n";
    std::cout << "=========================================\n";

    if (failedTests == 0) {
        std::cout << "âœ“ All tests passed!\n";
        return 0;
    } else {
        std::cout << "âœ— Some tests failed\n";
        return 1;
    }
}
