// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Audio device information
 */
struct AudioDeviceInfo {
    uint32_t id;
    std::string name;
    uint32_t maxInputChannels;
    uint32_t maxOutputChannels;
    std::vector<uint32_t> supportedSampleRates;
    uint32_t preferredSampleRate;
    bool isDefaultInput;
    bool isDefaultOutput;
};

/**
 * @brief Audio stream configuration
 */
struct AudioStreamConfig {
    uint32_t deviceId = 0;
    uint32_t sampleRate = 48000;
    uint32_t bufferSize = 512;
    uint32_t numInputChannels = 0;
    uint32_t numOutputChannels = 2;
    
    // Latency compensation (in milliseconds)
    double inputLatencyMs = 0.0;   // Input device latency
    double outputLatencyMs = 0.0;  // Output device latency
};

/**
 * @brief Audio latency metrics
 * 
 * Distinguishes between buffer period (one-way) and round-trip latency (RTL).
 * RTL is what users actually experience during recording/monitoring.
 */
struct AudioLatencyInfo {
    double bufferPeriodMs;      // Single buffer period (output or input)
    double estimatedRTL_Ms;     // Estimated round-trip latency (3x buffer period typical)
    uint32_t actualBufferFrames; // Actual buffer size (may differ from requested)
    uint32_t sampleRate;        // Sample rate used
    
    // Calculate from buffer size
    static AudioLatencyInfo calculate(uint32_t bufferFrames, uint32_t sampleRate, double rtlMultiplier = 3.0) {
        AudioLatencyInfo info;
        info.actualBufferFrames = bufferFrames;
        info.sampleRate = sampleRate;
        info.bufferPeriodMs = (1000.0 * bufferFrames) / sampleRate;
        info.estimatedRTL_Ms = info.bufferPeriodMs * rtlMultiplier;
        return info;
    }
};

/**
 * @brief Audio callback function type
 * 
 * @param outputBuffer Output audio buffer (interleaved)
 * @param inputBuffer Input audio buffer (interleaved, can be nullptr)
 * @param numFrames Number of frames to process
 * @param streamTime Current stream time in seconds
 * @param userData User-provided data pointer
 * @return 0 to continue, non-zero to stop stream
 */
using AudioCallback = int (*)(
    float* outputBuffer,
    const float* inputBuffer,
    uint32_t numFrames,
    double streamTime,
    void* userData
);

/**
 * @brief Abstract audio driver interface
 */
class AudioDriver {
public:
    virtual ~AudioDriver() = default;

    /**
     * @brief Get list of available audio devices
     */
    virtual std::vector<AudioDeviceInfo> getDevices() = 0;

    /**
     * @brief Get default output device ID
     */
    virtual uint32_t getDefaultOutputDevice() = 0;

    /**
     * @brief Get default input device ID
     */
    virtual uint32_t getDefaultInputDevice() = 0;

    /**
     * @brief Open audio stream
     */
    virtual bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) = 0;

    /**
     * @brief Close audio stream
     */
    virtual void closeStream() = 0;

    /**
     * @brief Start audio stream
     */
    virtual bool startStream() = 0;

    /**
     * @brief Stop audio stream
     */
    virtual void stopStream() = 0;

    /**
     * @brief Check if stream is running
     */
    virtual bool isStreamRunning() const = 0;

    /**
     * @brief Get current stream latency in seconds
     */
    virtual double getStreamLatency() const = 0;
};

} // namespace Audio
} // namespace Nomad
