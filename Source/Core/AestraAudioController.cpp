// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraAudioController.h"
#include "AestraContent.h"
#include "AudioThreadConstraints.h"
#include "AudioRT.h"
#include "AudioTelemetry.h"
#include "PreviewEngine.h"
#include "TrackManager.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <thread>
#include <chrono>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {
uint64_t estimateCycleHz() {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t c0 = Aestra::Audio::RT::readCycleCounter();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t c1 = Aestra::Audio::RT::readCycleCounter();
    const double sec = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    if (sec <= 0.0 || c1 <= c0) return 0;
    return static_cast<uint64_t>(static_cast<double>(c1 - c0) / sec);
#else
    return 0;
#endif
}
}

AestraAudioController::AestraAudioController() {
    m_audioManager = std::make_unique<AudioDeviceManager>();
    m_audioEngine = std::make_unique<AudioEngine>();
}

AestraAudioController::~AestraAudioController() {
    shutdown();
}

bool AestraAudioController::initialize() {
    if (!m_audioManager->initialize()) {
        Log::error("Failed to initialize audio engine");
        m_initialized = false;
        return false;
    }
    Log::info("Audio engine initialized");
    return true;
}

void AestraAudioController::shutdown() {
    if (m_initialized && m_audioManager) {
        stopStream();
        closeStream();
    }
    m_audioEngine.reset();
    m_audioManager.reset();
    m_initialized = false;
}

void AestraAudioController::setContent(std::shared_ptr<AestraContent> content) {
    m_content = content;
}

bool AestraAudioController::openDefaultStream(void* userData) {
    // Override userData with this if null, but typically caller passes app?
    // Actually, callback needs access to Controller.
    // If we pass 'this' as userData, callback calls Controller methods.

    void* callbackUserData = (userData) ? userData : this;

    try {
        std::vector<AudioDeviceInfo> devices;
        int retryCount = 0;
        const int maxRetries = 3;

        while (devices.empty() && retryCount < maxRetries) {
            if (retryCount > 0) {
                Log::info("Retry " + std::to_string(retryCount) + "/" + std::to_string(maxRetries) + " - waiting for WASAPI...");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            devices = m_audioManager->getDevices();
            retryCount++;
        }

        if (devices.empty()) {
            Log::warning("No audio devices found. Please check your audio drivers.");
            return false;
        }

        Log::info("Audio devices found");

        // Find first output device
        AudioDeviceInfo outputDevice;
        bool foundOutput = false;

        for (const auto& device : devices) {
            if (device.maxOutputChannels > 0) {
                outputDevice = device;
                foundOutput = true;
                break;
            }
        }

        if (!foundOutput) {
            Log::warning("No output audio device found");
            return false;
        }

        Log::info("Using audio device: " + outputDevice.name);

        // Configure audio stream
        AudioStreamConfig config;
        config.deviceId = outputDevice.id;
        config.sampleRate = 48000;
        config.bufferSize = 256;

        config.numInputChannels = outputDevice.maxInputChannels;
        config.numOutputChannels = 2;

        if (m_audioEngine) {
            m_audioEngine->setSampleRate(config.sampleRate);
            m_audioEngine->setBufferConfig(config.bufferSize, config.numOutputChannels);
        }

        m_streamConfig = config;

        // Open audio stream
        if (m_audioManager->openStream(config, audioCallback, callbackUserData)) {
            Log::info("Audio stream opened");
            m_initialized = true;
            return true;
        } else {
            Log::warning("Failed to open audio stream");
            return false;
        }
    } catch (const std::exception& e) {
        Log::error("Exception while initializing audio: " + std::string(e.what()));
        return false;
    }
}

bool AestraAudioController::startStream() {
    if (!m_initialized || !m_audioManager) return false;
    if (m_isAudioRunning) return true;

    // 1. Get Actual Rate/Buffer from Driver (if any) BEFORE starting thread
    double actualRate = static_cast<double>(m_audioManager->getStreamSampleRate());
    if (actualRate <= 0.0) {
        actualRate = static_cast<double>(m_streamConfig.sampleRate);
    }
    
    uint32_t actualBuffer = m_audioManager->getStreamBufferSize();
    if (actualBuffer == 0) actualBuffer = m_streamConfig.bufferSize;

    // Update config locally
    m_streamConfig.sampleRate = static_cast<uint32_t>(actualRate);
    m_streamConfig.bufferSize = actualBuffer;

    Log::info("AestraAudioController: Stream Config Target - Rate: " + std::to_string(actualRate) + 
              ", Buffer: " + std::to_string(actualBuffer));

    if (m_audioEngine) {
        m_audioEngine->setSampleRate(m_streamConfig.sampleRate);
        m_audioEngine->setBufferConfig(m_streamConfig.bufferSize, m_streamConfig.numOutputChannels);
        
        // Register input callback wrapper
        m_audioEngine->setInputCallback([](const float* input, uint32_t n, void* user) {
            auto* controller = static_cast<AestraAudioController*>(user);
            if (controller) {
                if (auto content = controller->m_content.lock()) {
                    if (content->getTrackManager()) {
                        content->getTrackManager()->processInput(input, n);
                    }
                }
            }
        }, this);

        // Setup Telemetry
        const uint64_t hz = estimateCycleHz();
        if (hz > 0) {
            m_audioEngine->telemetry().cycleHz.store(hz, std::memory_order_relaxed);
        }

        m_audioEngine->loadMetronomeClicks(
            "AestraAudio/assets/Aestra_metronome.wav",
            "AestraAudio/assets/Aestra_metronome_up.wav"
        );
        m_audioEngine->setBPM(120.0f);
    }

    // [FIX] Update Content Managers to ensure AudioGraph is rebuilt with correct rate!
    if (auto content = m_content.lock()) {
        if (auto tm = content->getTrackManager()) {
            tm->setOutputSampleRate(static_cast<double>(m_streamConfig.sampleRate));
            tm->setInputSampleRate(static_cast<double>(m_streamConfig.sampleRate));
            Log::info("AestraAudioController: Updated TrackManager Sample Rate to " + std::to_string(m_streamConfig.sampleRate));
        }
        if (auto pe = content->getPreviewEngine()) {
            pe->setOutputSampleRate(static_cast<double>(m_streamConfig.sampleRate));
        }
    }

    m_audioManager->setAutoBufferScaling(true, 5);

    // 2. Start the stream (Thread starts here)
    if (m_audioManager->startStream()) {
        Log::info("Audio stream started successfully");
        m_isAudioRunning = true;
        return true;
    }

    Log::error("Failed to start audio stream");
    return false;
}

void AestraAudioController::stopStream() {
    if (m_audioManager) m_audioManager->stopStream();
}

void AestraAudioController::closeStream() {
    if (m_audioManager) m_audioManager->closeStream();
}

int AestraAudioController::audioCallback(float* outputBuffer, const float* inputBuffer,
                         uint32_t nFrames, double streamTime, void* userData) {
    // B-005: Mark this as audio thread for constraint checking
    Aestra::Audio::AudioThreadGuard audioThreadGuard;
    Aestra::Audio::AudioThreadStats::instance().totalCallbacks.fetch_add(1, std::memory_order_relaxed);

    AestraAudioController* controller = static_cast<AestraAudioController*>(userData);
    if (!controller || !outputBuffer) return 1;

    Aestra::Audio::RT::initAudioThread();
    const uint64_t cbStartCycles = Aestra::Audio::RT::readCycleCounter();

    double actualRate = static_cast<double>(controller->m_streamConfig.sampleRate);
    if (actualRate <= 0.0) actualRate = 48000.0;

    if (controller->m_audioEngine) {
        controller->m_audioEngine->setSampleRate(static_cast<uint32_t>(actualRate));
        controller->m_audioEngine->processBlock(outputBuffer, inputBuffer, nFrames, streamTime);
    } else {
        std::fill(outputBuffer, outputBuffer + nFrames * 2, 0.0f);
    }

    // Preview mixing
    if (auto content = controller->m_content.lock()) {
        if (content->getPreviewEngine()) {
            auto previewEngine = content->getPreviewEngine();
            previewEngine->setOutputSampleRate(actualRate);
            previewEngine->process(outputBuffer, nFrames);
        }
    }

    const uint64_t cbEndCycles = Aestra::Audio::RT::readCycleCounter();
    if (controller->m_audioEngine && cbEndCycles > cbStartCycles) {
        auto& tel = controller->m_audioEngine->telemetry();
        tel.lastBufferFrames.store(nFrames, std::memory_order_relaxed);
        tel.lastSampleRate.store(static_cast<uint32_t>(actualRate), std::memory_order_relaxed);
        const uint64_t hz = tel.cycleHz.load(std::memory_order_relaxed);
        if (hz > 0) {
           const uint64_t deltaCycles = cbEndCycles - cbStartCycles;
           const uint64_t ns = (deltaCycles * 1000000000ull) / hz;
           tel.lastCallbackNs.store(ns, std::memory_order_relaxed);
        }
    }

    return 0;
}
