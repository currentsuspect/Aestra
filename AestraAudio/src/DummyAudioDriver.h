// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include <atomic>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Silent dummy driver that simulates an audio interface.
 * Used as a fail-safe fallback when hardware is disconnected.
 */
class DummyAudioDriver : public IAudioDriver {
public:
    DummyAudioDriver();
    virtual ~DummyAudioDriver();

    std::string getDisplayName() const override { return "Dummy Audio (Safety)"; }
    AudioDriverType getDriverType() const override { return AudioDriverType::DUMMY; }
    bool isAvailable() const override { return true; }

    std::vector<AudioDeviceInfo> getDevices() override;

    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;

    bool isStreamRunning() const override { return m_running.load(); }
    double getStreamLatency() const override { return m_latency; }
    uint32_t getStreamSampleRate() const override { return m_sampleRate; }
    uint32_t getStreamBufferSize() const override { return m_bufferSize; }
    DriverStatistics getStatistics() const override { return m_stats; }
    std::string getErrorMessage() const override { return ""; }

    void setDitheringEnabled(bool enabled) override { m_dither = enabled; }
    bool isDitheringEnabled() const override { return m_dither; }

private:
    void threadFunc();

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    
    AudioStreamConfig m_config;
    AudioCallback m_callback{nullptr};
    void* m_userData{nullptr};
    
    uint32_t m_sampleRate{48000};
    uint32_t m_bufferSize{512};
    double m_latency{0.01};
    bool m_dither{false};
    
    DriverStatistics m_stats;
    std::vector<float> m_silentBuffer;
};

} // namespace Audio
} // namespace Aestra
