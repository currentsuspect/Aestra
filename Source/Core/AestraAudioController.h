// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraAtomicSharedPtr.h"
#include "AestraAudio.h"
#include "AudioDeviceManager.h"
#include "AudioEngine.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class AestraContent;
namespace Aestra::Audio {
class PreviewEngine;
class TrackManager;
}

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

    // Buffer size change - updates both device and engine
    bool setBufferSize(uint32_t bufferSize);

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

    // Weak UI reference plus atomically published ownership for callback routing.
    std::weak_ptr<AestraContent> m_content;
    Aestra::AtomicSharedPtr<AestraContent> m_rtContent;
};
