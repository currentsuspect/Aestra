// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Lock-free ring buffer for streaming audio between decode and playback threads
 * 
 * Single-producer (decode thread) / single-consumer (audio thread) design.
 * Uses atomic indices for thread safety without locks.
 */
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(size_t capacityFrames, uint32_t numChannels = 2);
    ~AudioRingBuffer() = default;
    
    // Non-copyable
    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;
    
    /**
     * @brief Write frames to the ring buffer (called by decode thread)
     * @return Number of frames actually written (may be less if buffer is full)
     */
    size_t write(const float* samples, size_t numFrames);
    
    /**
     * @brief Read frames from the ring buffer (called by audio thread)
     * @return Number of frames actually read (may be less if buffer is empty)
     */
    size_t read(float* samples, size_t numFrames);
    
    /**
     * @brief Peek at frames without consuming them (for interpolation lookahead)
     */
    size_t peek(float* samples, size_t numFrames) const;
    
    /**
     * @brief Get number of frames available to read
     */
    size_t availableRead() const;
    
    /**
     * @brief Get number of frames available to write
     */
    size_t availableWrite() const;
    
    /**
     * @brief Clear the buffer
     */
    void clear();
    
    /**
     * @brief Get buffer capacity in frames
     */
    size_t capacity() const { return m_capacityFrames; }
    
    uint32_t channels() const { return m_numChannels; }

private:
    std::vector<float> m_buffer;
    size_t m_capacityFrames;
    uint32_t m_numChannels;
    
    std::atomic<size_t> m_writeIndex{0};  // Next frame index to write to
    std::atomic<size_t> m_readIndex{0};   // Next frame index to read from
};

/**
 * @brief Progressive audio decoder for streaming playback
 * 
 * Decodes audio file in chunks, feeding a ring buffer that the audio thread
 * can consume. Enables "instant start" playback where audio begins playing
 * before the entire file is decoded.
 */
class StreamingDecoder {
public:
    enum class State {
        Idle,       // No file loaded
        Starting,   // Beginning decode
        Streaming,  // Actively decoding and streaming
        Complete,   // File fully decoded
        Error       // Decode error occurred
    };
    
    using OnStreamReady = std::function<void(uint32_t sampleRate, uint32_t channels, double durationSeconds)>;
    using OnStreamError = std::function<void(const std::string& error)>;
    using OnStreamComplete = std::function<void()>;
    
    StreamingDecoder();
    ~StreamingDecoder();
    
    // Non-copyable
    StreamingDecoder(const StreamingDecoder&) = delete;
    StreamingDecoder& operator=(const StreamingDecoder&) = delete;
    
    /**
     * @brief Start streaming a file
     * 
     * @param path File path
     * @param bufferSizeSeconds Ring buffer size in seconds (default 2.0s for 50ms latency)
     * @param targetLatencyMs Target time-to-first-sound in milliseconds
     * @return true if decode started successfully
     */
    bool start(const std::string& path, double bufferSizeSeconds = 2.0, double targetLatencyMs = 50.0);
    
    /**
     * @brief Stop streaming and release resources
     */
    void stop();
    
    /**
     * @brief Read available frames from the stream
     * 
     * Called by audio thread. Returns the number of frames read.
     * If fewer frames are available, outputs silence for the remainder.
     */
    size_t read(float* output, size_t numFrames);
    
    /**
     * @brief Get current stream state
     */
    State getState() const { return m_state.load(std::memory_order_acquire); }
    
    /**
     * @brief Check if stream is ready for playback
     */
    bool isReady() const { 
        State s = m_state.load(std::memory_order_acquire); 
        return s == State::Streaming || s == State::Complete;
    }
    
    /**
     * @brief Check if entire file has been decoded
     */
    bool isComplete() const { return m_state.load(std::memory_order_acquire) == State::Complete; }
    
    /**
     * @brief Get file sample rate (valid after onReady callback)
     */
    uint32_t getSampleRate() const { return m_sampleRate.load(std::memory_order_relaxed); }
    
    /**
     * @brief Get number of channels (valid after onReady callback)
     */
    uint32_t getChannels() const { return m_channels.load(std::memory_order_relaxed); }
    
    /**
     * @brief Get total duration in seconds (may be 0 until decode completes for some formats)
     */
    double getDuration() const { return m_duration.load(std::memory_order_relaxed); }
    
    /**
     * @brief Get total frames decoded so far
     */
    uint64_t getDecodedFrames() const { return m_decodedFrames.load(std::memory_order_relaxed); }
    
    // Callbacks
    void setOnReady(OnStreamReady callback) { m_onReady = std::move(callback); }
    void setOnError(OnStreamError callback) { m_onError = std::move(callback); }
    void setOnComplete(OnStreamComplete callback) { m_onComplete = std::move(callback); }

private:
    void decodeThreadFunc(const std::string& path, double targetLatencyMs);
    bool openDecoder(const std::string& path);
    void closeDecoder();
    size_t decodeChunk(size_t targetFrames);
    
    // Thread management
    std::thread m_decodeThread;
    std::atomic<bool> m_stopRequested{false};
    
    // State
    std::atomic<State> m_state{State::Idle};
    std::atomic<uint32_t> m_sampleRate{0};
    std::atomic<uint32_t> m_channels{0};
    std::atomic<double> m_duration{0.0};
    std::atomic<uint64_t> m_decodedFrames{0};
    std::atomic<uint64_t> m_totalFrames{0};
    
    // Ring buffer for streaming
    std::unique_ptr<AudioRingBuffer> m_ringBuffer;
    std::mutex m_bufferMutex;  // Protects ring buffer creation/destruction only
    
    // Decoder handle (platform-specific, opaque)
    void* m_decoderHandle{nullptr};
    
    // Callbacks
    OnStreamReady m_onReady;
    OnStreamError m_onError;
    OnStreamComplete m_onComplete;
    
    // Constants
    static constexpr size_t kDecodeChunkFrames = 4096;  // Decode 4096 frames at a time
};

} // namespace Audio
} // namespace Nomad
