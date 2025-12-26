// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AudioProcessor.h"
#include <vector>
#include <memory>
#include <atomic>

namespace Nomad {
namespace Audio {

/**
 * @brief Mixer bus for audio routing and mixing
 * 
 * A MixerBus represents a single audio channel strip with:
 * - Gain control (volume)
 * - Pan control (stereo positioning)
 * - Mute/solo functionality
 * - Simple mixing of multiple input sources
 * 
 * Thread-safe for real-time audio processing.
 */
class MixerBus {
public:
    /**
     * @brief Construct a mixer bus
     * 
     * @param name Bus name (e.g., "Master", "Track 1")
     * @param numChannels Number of channels (1=mono, 2=stereo)
     */
    explicit MixerBus(const char* name = "Bus", uint32_t numChannels = 2);
    ~MixerBus() = default;

    /**
     * @brief Process audio through the bus
     * 
     * Applies gain, pan, and mute to the input buffer.
     * 
     * @param buffer Audio buffer (interleaved)
     * @param numFrames Number of frames to process
     */
    void process(float* buffer, uint32_t numFrames);

    /**
     * @brief Mix input buffer into output buffer
     * 
     * Adds the input to the output with gain and pan applied.
     * 
     * @param output Output buffer (interleaved)
     * @param input Input buffer (interleaved)
     * @param numFrames Number of frames to process
     */
    void mixInto(float* output, const float* input, uint32_t numFrames);

    /**
     * @brief Clear the bus buffer to silence
     * 
     * @param buffer Buffer to clear
     * @param numFrames Number of frames to clear
     */
    void clear(float* buffer, uint32_t numFrames);

    // Parameter setters (thread-safe)
    void setGain(float gain);
    void setPan(float pan);
    void setMute(bool mute);
    void setSolo(bool solo);

    // Parameter getters (thread-safe)
    float getGain() const { return m_gain.load(std::memory_order_acquire); }
    float getPan() const { return m_pan.load(std::memory_order_acquire); }
    bool isMuted() const { return m_muted.load(std::memory_order_acquire); }
    bool isSoloed() const { return m_soloed.load(std::memory_order_acquire); }

    // Bus info
    const char* getName() const { return m_name; }
    uint32_t getNumChannels() const { return m_numChannels; }

private:
    /**
     * @brief Apply constant power panning
     * 
     * @param pan Pan value (-1.0 = left, 0.0 = center, 1.0 = right)
     * @param leftGain Output left channel gain
     * @param rightGain Output right channel gain
     */
    void calculatePanGains(float pan, float& leftGain, float& rightGain) const;

    const char* m_name;
    uint32_t m_numChannels;

    // Atomic parameters for thread-safe access
    std::atomic<float> m_gain;      // Linear gain (target)
    std::atomic<float> m_pan;       // Pan (target)
    std::atomic<bool> m_muted;      // Mute state
    std::atomic<bool> m_soloed;     // Solo state

    // Smoothing state
    float m_currentGain{1.0f};
    float m_currentPan{0.0f};
};

/**
 * @brief Simple mixer with multiple buses
 * 
 * Manages multiple MixerBus instances and routes audio between them.
 */
class SimpleMixer {
public:
    SimpleMixer();
    ~SimpleMixer() = default;

    /**
     * @brief Add a bus to the mixer
     * 
     * @param name Bus name
     * @param numChannels Number of channels
     * @return Index of the added bus
     */
    size_t addBus(const char* name, uint32_t numChannels = 2);

    /**
     * @brief Get a bus by index
     * 
     * @param index Bus index
     * @return Pointer to bus, or nullptr if invalid index
     */
    MixerBus* getBus(size_t index);

    /**
     * @brief Get number of buses
     */
    size_t getNumBuses() const { return m_buses.size(); }

    /**
     * @brief Process all buses and mix to master output
     * 
     * @param masterOutput Master output buffer (interleaved stereo)
     * @param inputs Array of input buffers (one per bus)
     * @param numFrames Number of frames to process
     */
    void process(float* masterOutput, const float** inputs, uint32_t numFrames);

    /**
     * @brief Clear all buses
     */
    void reset();

private:
    std::vector<std::unique_ptr<MixerBus>> m_buses;
};

} // namespace Audio
} // namespace Nomad
