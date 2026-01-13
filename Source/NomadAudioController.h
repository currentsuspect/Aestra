// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <atomic>
#include <string>
#include <vector>

#include "../NomadAudio/include/NomadAudio.h"
#include "../NomadAudio/include/AudioEngine.h"
#include "../NomadAudio/include/AudioDeviceManager.h"

// Forward declarations
namespace Nomad {
    class NomadApp; // For callback context
}
class NomadContent;

class NomadAudioController {
public:
    NomadAudioController();
    ~NomadAudioController();

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
    Nomad::Audio::AudioEngine* getEngine() { return m_audioEngine.get(); }
    Nomad::Audio::AudioDeviceManager* getDeviceManager() { return m_audioManager.get(); }
    const Nomad::Audio::AudioStreamConfig& getStreamConfig() const { return m_streamConfig; }

    // Helpers
    static int audioCallback(float* outputBuffer, const float* inputBuffer,
                             uint32_t nFrames, double streamTime, void* userData);

    // Dependencies
    void setContent(std::shared_ptr<NomadContent> content);

private:
    std::unique_ptr<Nomad::Audio::AudioDeviceManager> m_audioManager;
    std::unique_ptr<Nomad::Audio::AudioEngine> m_audioEngine;

    Nomad::Audio::AudioStreamConfig m_streamConfig;
    bool m_initialized{false};

    // Weak reference to content for callback routing
    std::weak_ptr<NomadContent> m_content;
};
