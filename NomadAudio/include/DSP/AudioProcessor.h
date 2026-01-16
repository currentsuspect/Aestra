// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioDriver.h"
#include "NomadThreading.h"
#include <atomic>
#include <cstring>

namespace Nomad {
namespace Audio {

/**
 * @brief Audio command for lock-free UIâ†’Audio communication
 */
enum class AudioCommand {
    None = 0,
    SetGain,
    SetPan,
    Mute,
    Unmute,
    Reset
};

/**
 * @brief Audio command message
 */
struct AudioCommandMessage {
    AudioCommand command = AudioCommand::None;
    float value1 = 0.0f;
    float value2 = 0.0f;
    
    AudioCommandMessage() = default;
    AudioCommandMessage(AudioCommand cmd, float v1 = 0.0f, float v2 = 0.0f)
        : command(cmd), value1(v1), value2(v2) {}
};

/**
 * @brief Base class for audio processors
 * 
 * Provides lock-free communication between UI and audio threads.
 * Uses a ring buffer for command passing.
 */
class AudioProcessor {
public:
    AudioProcessor();
    virtual ~AudioProcessor() = default;

    /**
     * @brief Process audio callback (called from audio thread)
     * 
     * @param outputBuffer Output audio buffer (interleaved)
     * @param inputBuffer Input audio buffer (interleaved, can be nullptr)
     * @param numFrames Number of frames to process
     * @param streamTime Current stream time in seconds
     */
    virtual void process(
        float* outputBuffer,
        const float* inputBuffer,
        uint32_t numFrames,
        double streamTime
    ) = 0;

    /**
     * @brief Send command from UI thread to audio thread
     * 
     * @param message Command message to send
     * @return true if command was queued successfully
     */
    bool sendCommand(const AudioCommandMessage& message);

    /**
     * @brief Get current gain value (thread-safe)
     */
    float getGain() const { return m_gain.load(std::memory_order_acquire); }

    /**
     * @brief Get current pan value (thread-safe)
     */
    float getPan() const { return m_pan.load(std::memory_order_acquire); }

    /**
     * @brief Check if muted (thread-safe)
     */
    bool isMuted() const { return m_muted.load(std::memory_order_acquire); }

protected:
    /**
     * @brief Process pending commands (call from audio thread)
     */
    void processCommands();

    /**
     * @brief Handle a command (override to add custom commands)
     */
    virtual void handleCommand(const AudioCommandMessage& message);

    // Atomic parameters (safe to read from any thread)
    std::atomic<float> m_gain;
    std::atomic<float> m_pan;
    std::atomic<bool> m_muted;

private:
    // Lock-free command queue (UI â†’ Audio)
    static constexpr size_t COMMAND_QUEUE_SIZE = 256;
    Nomad::LockFreeRingBuffer<AudioCommandMessage, COMMAND_QUEUE_SIZE> m_commandQueue;
};

/**
 * @brief Simple audio buffer manager
 * 
 * Manages temporary audio buffers for processing.
 */
class AudioBufferManager {
public:
    AudioBufferManager();
    ~AudioBufferManager();

    /**
     * @brief Allocate buffer for given size
     * 
     * @param numFrames Number of frames
     * @param numChannels Number of channels
     * @return Pointer to allocated buffer
     */
    float* allocate(uint32_t numFrames, uint32_t numChannels);

    /**
     * @brief Clear all buffers to zero
     */
    void clear();

    /**
     * @brief Get maximum buffer size
     */
    uint32_t getMaxBufferSize() const { return m_maxBufferSize; }

private:
    static constexpr uint32_t MAX_BUFFER_SIZE = 8192; // frames
    static constexpr uint32_t MAX_CHANNELS = 8;
    
    float* m_buffer;
    uint32_t m_maxBufferSize;
    uint32_t m_maxChannels;
};

/**
 * @brief Simple test tone generator
 * 
 * Generates sine waves for testing audio callback.
 */
class TestToneGenerator : public AudioProcessor {
public:
    TestToneGenerator(double sampleRate = 48000.0);
    ~TestToneGenerator() override = default;

    void process(
        float* outputBuffer,
        const float* inputBuffer,
        uint32_t numFrames,
        double streamTime
    ) override;

    /**
     * @brief Set frequency of test tone
     */
    void setFrequency(double frequency);

    /**
     * @brief Get current frequency
     */
    double getFrequency() const { return m_frequency.load(std::memory_order_acquire); }

private:
    std::atomic<double> m_frequency;
    std::atomic<double> m_phase;
    double m_sampleRate;
};

} // namespace Audio
} // namespace Nomad
