// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <atomic>
#include <string>
#include <vector>

#include "AestraAudio.h"
#include "AudioEngine.h"
#include "AudioDeviceManager.h"

// Forward declarations
class AestraContent;

class AestraAudioController {
public:
    AestraAudioController();
    ~AestraAudioController();

    bool initialize();
    void shutdown();

    // Device Management
    bool openDefaultStream(void* userData);
    bool startStream();
    void stopStream();
    void closeStream();

    // Status
    bool isInitialized() const { return m_initialized; }
    uint32_t getSampleRate() const { return m_streamConfig.sampleRate; }
    uint32_t getBufferSize() const { return m_streamConfig.bufferSize; }

    // Accessors
    Aestra::Audio::AudioEngine* getEngine() { return m_audioEngine.get(); }
    Aestra::Audio::AudioDeviceManager* getDeviceManager() { return m_audioManager.get(); }
    const Aestra::Audio::AudioStreamConfig& getStreamConfig() const { return m_streamConfig; }

    // Helpers
    static int audioCallback(float* outputBuffer, const float* inputBuffer,
                             uint32_t nFrames, double streamTime, void* userData);

    // Dependencies
    void setContent(std::shared_ptr<AestraContent> content);

private:
    std::unique_ptr<Aestra::Audio::AudioDeviceManager> m_audioManager;
    std::unique_ptr<Aestra::Audio::AudioEngine> m_audioEngine;

    Aestra::Audio::AudioStreamConfig m_streamConfig;
    bool m_initialized{false};
    bool m_isAudioRunning{false};

    // Weak reference to content for callback routing
    std::weak_ptr<AestraContent> m_content;
};
