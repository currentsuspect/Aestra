// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Audio device information
 */
struct AudioDeviceInfo {
    /** @brief Backend-specific device identifier. */
    uint32_t id;
    /** @brief Human-readable device name. */
    std::string name;
    /** @brief Maximum available input channels. */
    uint32_t maxInputChannels;
    /** @brief Maximum available output channels. */
    uint32_t maxOutputChannels;
    /** @brief Sample rates reported as supported by the backend. */
    std::vector<uint32_t> supportedSampleRates;
    /** @brief Preferred sample rate reported by the backend. */
    uint32_t preferredSampleRate;
    /** @brief True when this device is the system default input. */
    bool isDefaultInput;
    /** @brief True when this device is the system default output. */
    bool isDefaultOutput;
};

/**
 * @brief Audio stream configuration
 */
struct AudioStreamConfig {
    /** @brief Selected output device identifier. */
    uint32_t deviceId = 0;
    /** @brief Selected input device identifier. */
    uint32_t inputDeviceId = 0;
    /** @brief Requested sample rate. */
    uint32_t sampleRate = 48000;
    /** @brief Requested buffer size in frames. */
    uint32_t bufferSize = 512;
    /** @brief Requested number of input channels. */
    uint32_t numInputChannels = 0;
    /** @brief Requested number of output channels. */
    uint32_t numOutputChannels = 2;

    /** @brief Measured or estimated input latency in milliseconds. */
    double inputLatencyMs = 0.0;  // Input device latency
    /** @brief Measured or estimated output latency in milliseconds. */
    double outputLatencyMs = 0.0; // Output device latency

    /** @brief Optional telemetry pointer for RT-thread counter updates. */
    struct AudioTelemetry* telemetry = nullptr; // Forward-declared, set by engine
};

/**
 * @brief Audio latency metrics
 *
 * Distinguishes between buffer period (one-way) and round-trip latency (RTL).
 * RTL is what users actually experience during recording/monitoring.
 */
struct AudioLatencyInfo {
    /** @brief One-way buffer period in milliseconds. */
    double bufferPeriodMs;       // Single buffer period (output or input)
    /** @brief Estimated round-trip latency in milliseconds. */
    double estimatedRTL_Ms;      // Estimated round-trip latency (3x buffer period typical)
    /** @brief Actual buffer size in frames. */
    uint32_t actualBufferFrames; // Actual buffer size (may differ from requested)
    /** @brief Active sample rate used for the calculation. */
    uint32_t sampleRate;         // Sample rate used

    /**
     * @brief Build latency metrics from buffer size and sample rate.
     * @param bufferFrames Actual buffer size in frames.
     * @param sampleRate Active sample rate.
     * @param rtlMultiplier Estimated round-trip multiplier.
     * @return Populated latency information structure.
     */
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
using AudioCallback = int (*)(float* outputBuffer, const float* inputBuffer, uint32_t numFrames, double streamTime,
                              void* userData);

/**
 * @brief Abstract audio driver interface
 */
class AudioDriver {
public:
    virtual ~AudioDriver() = default;

    /**
     * @brief Get list of available audio devices
     * @return Enumerated devices visible to the backend.
     */
    virtual std::vector<AudioDeviceInfo> getDevices() = 0;

    /**
     * @brief Get default output device ID
     * @return Backend-specific default output device identifier.
     */
    virtual uint32_t getDefaultOutputDevice() = 0;

    /**
     * @brief Get default input device ID
     * @return Backend-specific default input device identifier.
     */
    virtual uint32_t getDefaultInputDevice() = 0;

    /**
     * @brief Open audio stream
     * @param config Stream configuration to apply.
     * @param callback Audio callback invoked by the backend.
     * @param userData User data forwarded to the callback.
     * @return True when the stream opened successfully.
     */
    virtual bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) = 0;

    /**
     * @brief Close audio stream
     */
    virtual void closeStream() = 0;

    /**
     * @brief Start audio stream
     * @return True when the stream started successfully.
     */
    virtual bool startStream() = 0;

    /**
     * @brief Stop audio stream
     */
    virtual void stopStream() = 0;

    /**
     * @brief Check if stream is running
     * @return True while the backend stream is active.
     */
    virtual bool isStreamRunning() const = 0;

    /**
     * @brief Get current stream latency in seconds
     * @return Stream latency in seconds.
     */
    virtual double getStreamLatency() const = 0;

    /**
     * @brief Get the actual sample rate the stream is running at
     *
     * May differ from requested rate if the backend performs conversion (e.g., WASAPI Shared).
     * Return 0 if stream is not open.
     * @return Actual backend sample rate, or 0 when no stream is open.
     */
    virtual uint32_t getStreamSampleRate() const = 0;

    /**
     * @brief Get the actual buffer size the stream is using
     *
     * May differ from requested size based on driver constraints.
     * Return 0 if stream is not open.
     * @return Actual backend buffer size in frames, or 0 when no stream is open.
     */
    virtual uint32_t getStreamBufferSize() const = 0;
};

} // namespace Audio
} // namespace Aestra
