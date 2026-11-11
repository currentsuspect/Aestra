// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IAudioDriver.h"
#include "RtAudio.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#ifdef __linux__
#include <pthread.h>
#endif

namespace Aestra {
namespace Audio {

class RtAudioDriver : public IAudioDriver {
public:
    RtAudioDriver();
    ~RtAudioDriver() override;

    std::string getDisplayName() const override;
    AudioDriverType getDriverType() const override;
    bool isAvailable() const override;

    std::vector<AudioDeviceInfo> getDevices() override;

    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override;
    void closeStream() override;
    bool startStream() override;
    void stopStream() override;

    bool isStreamRunning() const override;
    double getStreamLatency() const override;
    uint32_t getStreamSampleRate() const override;
    uint32_t getStreamBufferSize() const override;
    DriverStatistics getStatistics() const override { return m_stats; }
    std::string getErrorMessage() const override { return m_lastError; }

    void setDitheringEnabled(bool enabled) override { (void)enabled; }
    bool isDitheringEnabled() const override { return false; }

    /// Hands a wedged stream to a reaper thread and starts a fresh RtAudio instance (see IAudioDriver).
    void abandonStream() override;

private:
    // What RtAudio's callback receives as userData. One per opened stream, so a
    // stream that has been abandoned can be told apart from the current one:
    // when its stuck write finally returns, it must not reach the engine.
    struct StreamContext {
        RtAudioDriver* driver = nullptr;
        uint32_t outputChannels = 0;
        std::atomic<bool> live{true};
    };

    static int rtAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int numFrames, double streamTime,
                               RtAudioStreamStatus status, void* userData);

    bool tryInitializeBackend(const std::vector<RtAudio::Api>& candidates, bool keepWithoutDevices = false);
    /// The APIs an abandoned stream is restarted on, in order: the one that was
    /// serving it, then auto-detection. Empty-device handling lives in
    /// tryInitializeBackend(keepWithoutDevices).
    static std::vector<RtAudio::Api> restartCandidates(RtAudio::Api savedApi);
    static AudioDriverType apiToDriverType(RtAudio::Api api);
    static const char* apiName(RtAudio::Api api);
    std::string getDeviceName(unsigned int deviceId) const;
#ifdef __linux__
    void startRealtimePriorityWorker();
    void stopRealtimePriorityWorker();
    void applyLinuxRealtimePriority(pthread_t audioThreadId);
#endif

    std::unique_ptr<RtAudio> m_rtAudio;
    std::unique_ptr<StreamContext> m_streamContext;
    RtAudio::Api m_api{RtAudio::UNSPECIFIED};
    AudioDriverType m_driverType{AudioDriverType::UNKNOWN};
    std::atomic<AudioCallback> m_userCallback{nullptr};
    std::atomic<void*> m_userData{nullptr};
    std::atomic<uint32_t> m_sampleRate{0};
    std::atomic<uint32_t> m_bufferSize{0};
    std::atomic<bool> m_callbackRtPriorityAttempted{false};
    // Client callbacks currently running. RtAudio's PulseAudio backend calls
    // the client callback outside its stream lock, so RtAudio::stopStream()
    // can return while a callback is still pumping the engine; stopStream()
    // waits for this to reach zero.
    std::atomic<int> m_callbacksInFlight{0};
#ifdef __linux__
    std::atomic<uintptr_t> m_audioThreadToken{0};
    std::atomic<bool> m_rtPriorityWorkerStop{false};
    std::thread m_rtPriorityWorker;
    // mlockall() locks every resident page + pre-faults future ones (0.3-4s on a
    // large process). Run it off the UI thread that starts the stream; joined in
    // closeStream() so it never outlives the driver.
    std::thread m_memLockWorker;
#endif
    DriverStatistics m_stats;
    std::string m_lastError;
    struct AudioTelemetry* m_telemetry = nullptr; // RT-thread telemetry (atomic, lock-free)
};

} // namespace Audio
} // namespace Aestra
