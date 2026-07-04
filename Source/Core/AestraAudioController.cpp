// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraAudioController.h"
#include "AestraContent.h"
#include "AudioThreadConstraints.h"
#include "AudioRT.h"
#include "AudioTelemetry.h"
#include "PreviewEngine.h"
#include "TrackManager.h"
#include "AestraPlatform.h"
#include "../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {
std::filesystem::path getAudioSettingsConfigPath() {
    if (auto* utils = Aestra::Platform::getUtils()) {
        std::error_code ec;
        std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
        if (!appDataDir.empty()) {
            std::filesystem::create_directories(appDataDir, ec);
            if (!ec) {
                return appDataDir / "audio_settings.conf";
            }
        }
    }
    return std::filesystem::current_path() / "audio_settings.conf";
}

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

struct SavedAudioSelection {
    int outputDeviceId{-1};
    int inputDeviceId{-1};
};

SavedAudioSelection loadSavedAudioSelection() {
    SavedAudioSelection selection;

    const std::filesystem::path configPath = getAudioSettingsConfigPath();
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return selection;
    }

    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        try {
            if (key == "device") {
                selection.outputDeviceId = std::stoi(value);
            } else if (key == "input_device") {
                selection.inputDeviceId = std::stoi(value);
            }
        } catch (const std::exception&) {
        }
    }

    return selection;
}

const AudioDeviceInfo* findDeviceById(const std::vector<AudioDeviceInfo>& devices, int id) {
    if (id < 0) {
        return nullptr;
    }

    auto it = std::find_if(devices.begin(), devices.end(), [id](const AudioDeviceInfo& device) {
        return static_cast<int>(device.id) == id;
    });
    return it != devices.end() ? &(*it) : nullptr;
}

bool looksLikeMonitorInput(const std::string& name) {
    std::string lowered = name;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered.find("monitor") != std::string::npos ||
           lowered.find("loopback") != std::string::npos ||
           lowered.find("what u hear") != std::string::npos ||
           lowered.find("stereo mix") != std::string::npos ||
           lowered.find("wasapi") != std::string::npos;
}

const AudioDeviceInfo* choosePreferredInputDevice(const std::vector<AudioDeviceInfo>& devices, int savedId) {
    if (const auto* saved = findDeviceById(devices, savedId); saved && saved->maxInputChannels > 0) {
        return saved;
    }

    for (const auto& device : devices) {
        if (device.maxInputChannels > 0 && device.isDefaultInput && !looksLikeMonitorInput(device.name)) {
            return &device;
        }
    }
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0 && !looksLikeMonitorInput(device.name)) {
            return &device;
        }
    }
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0 && device.isDefaultInput) {
            return &device;
        }
    }
    for (const auto& device : devices) {
        if (device.maxInputChannels > 0) {
            return &device;
        }
    }

    return nullptr;
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
    if (m_audioEngine) {
        m_audioEngine->drainDeferredResourcesForShutdown();
    }
    m_audioEngine.reset();
    m_audioManager.reset();
    m_initialized = false;
}

void AestraAudioController::setContent(std::shared_ptr<AestraContent> content) {
    m_content = content;
    Aestra::Audio::TrackManager* trackManager = content ? content->getTrackManager().get() : nullptr;
    Aestra::Audio::PreviewEngine* previewEngine = content ? content->getPreviewEngine() : nullptr;
    m_rtTrackManager.store(trackManager, std::memory_order_release);
    m_rtPreviewEngine.store(previewEngine, std::memory_order_release);
    m_rtContent.store(std::move(content), std::memory_order_release);
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
        for (const auto& device : devices) {
            Log::info("[Audio] Device " + std::to_string(device.id) + ": " + device.name +
                      " | in=" + std::to_string(device.maxInputChannels) +
                      " out=" + std::to_string(device.maxOutputChannels) +
                      " | defaultIn=" + std::string(device.isDefaultInput ? "yes" : "no") +
                      " defaultOut=" + std::string(device.isDefaultOutput ? "yes" : "no"));
        }

        const SavedAudioSelection savedSelection = loadSavedAudioSelection();

        const AudioDeviceInfo* outputDevice = findDeviceById(devices, savedSelection.outputDeviceId);
        if (!outputDevice || outputDevice->maxOutputChannels == 0) {
            AudioDeviceInfo defaultOutput = m_audioManager->getDefaultOutputDevice();
            outputDevice = findDeviceById(devices, static_cast<int>(defaultOutput.id));
        }
        if (!outputDevice) {
            for (const auto& device : devices) {
                if (device.maxOutputChannels > 0) {
                    outputDevice = &device;
                    break;
                }
            }
        }

        if (!outputDevice) {
            Log::warning("No output audio device found");
            return false;
        }

        Log::info("Using audio device: " + outputDevice->name);

        const AudioDeviceInfo* inputDevice = choosePreferredInputDevice(devices, savedSelection.inputDeviceId);
        if (inputDevice) {
            Log::info("Using input device: " + inputDevice->name + " (" +
                      std::to_string(inputDevice->maxInputChannels) + " channels)");
        } else {
            Log::warning("No dedicated input device found; recording inputs disabled.");
        }

        // Configure audio stream
        AudioStreamConfig config;
        config.deviceId = outputDevice->id;
        config.inputDeviceId = inputDevice ? inputDevice->id : outputDevice->id;
        config.sampleRate = 48000;
        config.bufferSize = 512;

        config.numInputChannels = inputDevice ? std::min<uint32_t>(inputDevice->maxInputChannels, 32u) : 0u;
        config.numOutputChannels = std::min<uint32_t>(2, std::max<uint32_t>(1, outputDevice->maxOutputChannels));
        Log::info("AestraAudioController: Initial stream config - Output Device: " + std::to_string(config.deviceId) +
                  ", Input Device: " + std::to_string(config.inputDeviceId) +
                  ", Inputs: " + std::to_string(config.numInputChannels) +
                  ", Outputs: " + std::to_string(config.numOutputChannels));

        if (m_audioEngine) {
            m_audioEngine->setSampleRate(config.sampleRate);
            m_audioEngine->setBufferConfig(config.bufferSize, config.numOutputChannels);
            config.telemetry = &m_audioEngine->telemetry();
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
    if (auto content = m_content.lock()) {
        m_rtTrackManager.store(content->getTrackManager().get(), std::memory_order_release);
        m_rtPreviewEngine.store(content->getPreviewEngine(), std::memory_order_release);
        m_rtContent.store(std::move(content), std::memory_order_release);
    }

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
                auto* trackManager = controller->m_rtTrackManager.load(std::memory_order_acquire);
                if (trackManager) {
                    trackManager->updateInputDiagnostics(input, n);
                    trackManager->processInput(input, n, &controller->m_audioEngine->telemetry());
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
            tm->setInputChannelCount(m_streamConfig.numInputChannels);
            tm->publishInputMonitoringSnapshot();
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
    m_isAudioRunning = false;
    m_rtTrackManager.store(nullptr, std::memory_order_release);
    m_rtPreviewEngine.store(nullptr, std::memory_order_release);
    m_rtContent.store(nullptr, std::memory_order_release);
}

void AestraAudioController::closeStream() {
    if (m_audioManager) m_audioManager->closeStream();
    m_rtTrackManager.store(nullptr, std::memory_order_release);
    m_rtPreviewEngine.store(nullptr, std::memory_order_release);
    m_rtContent.store(nullptr, std::memory_order_release);
}

bool AestraAudioController::setBufferSize(uint32_t bufferSize) {
    if (!m_initialized || !m_audioManager) {
        return false;
    }

    if (bufferSize == m_streamConfig.bufferSize) {
        return true;
    }

    // Update device (this will reopen the stream)
    if (!m_audioManager->setBufferSize(bufferSize)) {
        return false;
    }

    // IMPORTANT: Also update the engine's buffer config!
    // Without this, renderGraph will reject blocks larger than the old config
    if (m_audioEngine) {
        m_audioEngine->setBufferConfig(bufferSize, m_streamConfig.numOutputChannels);
    }

    // Update our local config
    m_streamConfig.bufferSize = bufferSize;

    return true;
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
        controller->m_audioEngine->processBlock(outputBuffer, inputBuffer, nFrames, streamTime);
    } else {
        const uint32_t outCh = std::max<uint32_t>(1, controller->m_streamConfig.numOutputChannels);
        std::fill(outputBuffer, outputBuffer + static_cast<size_t>(nFrames) * outCh, 0.0f);
    }

    auto* trackManager = controller->m_rtTrackManager.load(std::memory_order_acquire);
    if (inputBuffer && trackManager) {
        trackManager->mixInputMonitoring(inputBuffer, outputBuffer, nFrames, controller->m_streamConfig.numOutputChannels);
    }

    auto* previewEngine = controller->m_rtPreviewEngine.load(std::memory_order_acquire);
    if (previewEngine) {
        previewEngine->processRealtime(outputBuffer, nFrames, controller->m_streamConfig.numOutputChannels);
    }

    if (controller->m_audioEngine) {
        controller->m_audioEngine->captureWaveformHistory(outputBuffer, nFrames);
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
           tel.updateMaxCallbackNs(ns);
           const uint64_t deadlineNs =
               (static_cast<uint64_t>(nFrames) * 1000000000ull) / static_cast<uint64_t>(actualRate);
           if (deadlineNs > 0 && ns > deadlineNs) {
               tel.incrementOverruns();
           }
        }
    }

    return 0;
}
