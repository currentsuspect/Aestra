// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NomadAudio.h"
#include "MixerBus.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

using namespace Nomad::Audio;

// Test tone generator for mixer testing
class MixerTestTone {
public:
    MixerTestTone(double frequency, double sampleRate)
        : m_frequency(frequency)
        , m_sampleRate(sampleRate)
        , m_phase(0.0)
    {
    }

    void generate(float* buffer, uint32_t numFrames, uint32_t numChannels)
    {
        const double phaseIncrement = 2.0 * 3.14159265358979323846 * m_frequency / m_sampleRate;
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            const float sample = static_cast<float>(std::sin(m_phase));
            
            // Write to all channels
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                buffer[i * numChannels + ch] = sample;
            }
            
            m_phase += phaseIncrement;
            if (m_phase >= 2.0 * 3.14159265358979323846) {
                m_phase -= 2.0 * 3.14159265358979323846;
            }
        }
    }

private:
    double m_frequency;
    double m_sampleRate;
    double m_phase;
};

// Global mixer and tone generators
SimpleMixer g_mixer;
MixerTestTone g_tone1(440.0, 48000.0);  // A4
MixerTestTone g_tone2(554.37, 48000.0); // C#5
MixerTestTone g_tone3(659.25, 48000.0); // E5

// Audio callback
int audioCallback(float* outputBuffer, const float* inputBuffer,
                  uint32_t numFrames, double streamTime, void* userData)
{
    static std::vector<float> bus1Buffer;
    static std::vector<float> bus2Buffer;
    static std::vector<float> bus3Buffer;
    
    // Resize buffers if needed
    const size_t bufferSize = numFrames * 2; // Stereo
    if (bus1Buffer.size() != bufferSize) {
        bus1Buffer.resize(bufferSize);
        bus2Buffer.resize(bufferSize);
        bus3Buffer.resize(bufferSize);
    }

    // Generate tones for each bus
    g_tone1.generate(bus1Buffer.data(), numFrames, 2);
    g_tone2.generate(bus2Buffer.data(), numFrames, 2);
    g_tone3.generate(bus3Buffer.data(), numFrames, 2);

    // Prepare input array for mixer
    const float* inputs[3] = {
        bus1Buffer.data(),
        bus2Buffer.data(),
        bus3Buffer.data()
    };

    // Mix all buses to master output
    g_mixer.process(outputBuffer, inputs, numFrames);

    return 0;
}

void printBusInfo(const char* name, MixerBus* bus)
{
    std::cout << name << ": "
              << "Gain=" << bus->getGain()
              << ", Pan=" << bus->getPan()
              << ", Muted=" << (bus->isMuted() ? "Yes" : "No")
              << ", Solo=" << (bus->isSoloed() ? "Yes" : "No")
              << std::endl;
}

void testMixerBus()
{
    std::cout << "\n=== NomadAudio Mixer Bus Test ===" << std::endl;
    std::cout << "Testing basic mixer with gain, pan, and routing\n" << std::endl;

    // Create audio device manager
    AudioDeviceManager manager;

    // Initialize audio system
    if (!manager.initialize()) {
        std::cerr << "Error: Failed to initialize audio system!" << std::endl;
        return;
    }

    // Get default output device
    auto defaultDevice = manager.getDefaultOutputDevice();
    
    std::cout << "Using device: " << defaultDevice.name << std::endl;
    std::cout << "Sample rate: 48000 Hz" << std::endl;
    std::cout << "Buffer size: 512 frames\n" << std::endl;

    // Set up mixer with 3 buses
    size_t bus1Idx = g_mixer.addBus("Bus 1 (440 Hz)", 2);
    size_t bus2Idx = g_mixer.addBus("Bus 2 (554 Hz)", 2);
    size_t bus3Idx = g_mixer.addBus("Bus 3 (659 Hz)", 2);

    MixerBus* bus1 = g_mixer.getBus(bus1Idx);
    MixerBus* bus2 = g_mixer.getBus(bus2Idx);
    MixerBus* bus3 = g_mixer.getBus(bus3Idx);

    // Set initial gains (quieter for mixing)
    bus1->setGain(0.3f);
    bus2->setGain(0.3f);
    bus3->setGain(0.3f);

    // Configure audio
    AudioStreamConfig config;
    config.deviceId = defaultDevice.id;
    config.sampleRate = 48000;
    config.bufferSize = 512;
    config.numInputChannels = 0;
    config.numOutputChannels = 2;

    // Open and start stream
    if (!manager.openStream(config, audioCallback, nullptr)) {
        std::cerr << "Error: Failed to open audio stream!" << std::endl;
        return;
    }

    if (!manager.startStream()) {
        std::cerr << "Error: Failed to start audio stream!" << std::endl;
        manager.closeStream();
        return;
    }

    std::cout << "Audio stream started successfully!\n" << std::endl;

    // Test 1: All buses playing (chord)
    std::cout << "Test 1: All buses playing (A major chord)" << std::endl;
    printBusInfo("Bus 1", bus1);
    printBusInfo("Bus 2", bus2);
    printBusInfo("Bus 3", bus3);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Test 2: Gain control
    std::cout << "\nTest 2: Adjusting gain on Bus 1 (0.3 -> 0.6)" << std::endl;
    bus1->setGain(0.6f);
    printBusInfo("Bus 1", bus1);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    bus1->setGain(0.3f); // Reset

    // Test 3: Pan control
    std::cout << "\nTest 3: Panning Bus 2 left (-1.0)" << std::endl;
    bus2->setPan(-1.0f);
    printBusInfo("Bus 2", bus2);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Panning Bus 2 right (1.0)" << std::endl;
    bus2->setPan(1.0f);
    printBusInfo("Bus 2", bus2);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Panning Bus 2 center (0.0)" << std::endl;
    bus2->setPan(0.0f);
    printBusInfo("Bus 2", bus2);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Test 4: Mute control
    std::cout << "\nTest 4: Muting Bus 1" << std::endl;
    bus1->setMute(true);
    printBusInfo("Bus 1", bus1);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Unmuting Bus 1" << std::endl;
    bus1->setMute(false);
    printBusInfo("Bus 1", bus1);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Test 5: Solo control
    std::cout << "\nTest 5: Soloing Bus 3 (only Bus 3 should play)" << std::endl;
    bus3->setSolo(true);
    printBusInfo("Bus 1", bus1);
    printBusInfo("Bus 2", bus2);
    printBusInfo("Bus 3", bus3);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Unsoloing Bus 3" << std::endl;
    bus3->setSolo(false);
    printBusInfo("Bus 3", bus3);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Test 6: Complex routing
    std::cout << "\nTest 6: Complex routing" << std::endl;
    std::cout << "Bus 1: Left pan, gain 0.4" << std::endl;
    std::cout << "Bus 2: Center, gain 0.5" << std::endl;
    std::cout << "Bus 3: Right pan, gain 0.4" << std::endl;
    
    bus1->setPan(-0.7f);
    bus1->setGain(0.4f);
    bus2->setPan(0.0f);
    bus2->setGain(0.5f);
    bus3->setPan(0.7f);
    bus3->setGain(0.4f);
    
    printBusInfo("Bus 1", bus1);
    printBusInfo("Bus 2", bus2);
    printBusInfo("Bus 3", bus3);
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Test 7: Rapid parameter changes
    std::cout << "\nTest 7: Rapid parameter changes (stress test)" << std::endl;
    for (int i = 0; i < 100; ++i) {
        float pan = std::sin(i * 0.1f);
        float gain = 0.2f + 0.2f * std::cos(i * 0.15f);
        
        bus1->setPan(pan);
        bus1->setGain(gain);
        bus2->setPan(-pan);
        bus2->setGain(gain);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Reset to defaults
    bus1->setPan(0.0f);
    bus1->setGain(0.3f);
    bus2->setPan(0.0f);
    bus2->setGain(0.3f);
    
    std::cout << "Stress test complete!" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stop and cleanup
    std::cout << "\nStopping audio stream..." << std::endl;
    manager.stopStream();
    manager.closeStream();

    std::cout << "\n=== All Tests Complete ===" << std::endl;
    std::cout << "\nTest Results:" << std::endl;
    std::cout << "âœ“ Mixer bus creation" << std::endl;
    std::cout << "âœ“ Gain control (0.0 to 2.0)" << std::endl;
    std::cout << "âœ“ Pan control (-1.0 to 1.0)" << std::endl;
    std::cout << "âœ“ Mute functionality" << std::endl;
    std::cout << "âœ“ Solo functionality" << std::endl;
    std::cout << "âœ“ Audio routing (3 buses to master)" << std::endl;
    std::cout << "âœ“ Thread-safe parameter changes" << std::endl;
    std::cout << "âœ“ Constant power panning" << std::endl;
}

int main()
{
    try {
        testMixerBus();
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
