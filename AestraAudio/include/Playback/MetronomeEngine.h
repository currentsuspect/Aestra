// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Handles metronome click generation and mixing.
 * Extracted from AudioEngine to reduce God Class complexity.
 */
class MetronomeEngine {
public:
    MetronomeEngine();
    ~MetronomeEngine() = default;

    /**
     * @brief Mixes metronome click into the output buffer if enabled and playing.
     * Real-time safe.
     */
    void process(float* outputBuffer, uint32_t numFrames, uint32_t numChannels, uint64_t globalSamplePos,
                 uint32_t sampleRate, bool transportPlaying);

    // Configuration
    void setEnabled(bool enabled) { m_enabled.store(enabled, std::memory_order_relaxed); }
    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    void setVolume(float vol) { m_volume.store(vol, std::memory_order_relaxed); }
    float getVolume() const { return m_volume.load(std::memory_order_relaxed); }

    void setBPM(float bpm) { m_bpm.store(bpm, std::memory_order_relaxed); }
    float getBPM() const { return m_bpm.load(std::memory_order_relaxed); }

    void setBeatsPerBar(int beats);
    int getBeatsPerBar() const { return m_beatsPerBar.load(std::memory_order_relaxed); }

    /**
     * @brief Load custom click sounds from WAV files.
     * Not real-time safe (performs file I/O).
     */
    void loadClickSounds(const std::string& downbeatPath, const std::string& upbeatPath);

    // Reset internal state (e.g. on seek/loop)
    void reset(uint64_t globalSamplePos, uint32_t sampleRate);

private:
    void generateDefaultSounds();

    // State
    std::atomic<bool> m_enabled{false};
    std::atomic<float> m_volume{0.7f};
    std::atomic<float> m_bpm{120.0f};
    std::atomic<int> m_beatsPerBar{4};

    // Audio Data
    std::vector<float> m_synthClickLow;    // Generated default (Downbeat)
    std::vector<float> m_synthClickHigh;   // Generated default (Upbeat)
    std::vector<float> m_clickSamplesDown; // Custom (Downbeat)
    std::vector<float> m_clickSamplesUp;   // Custom (Upbeat)

    // Playback State
    const std::vector<float>* m_activeClickSamples{nullptr};
    size_t m_clickPlayhead{0};
    bool m_clickPlaying{false};
    uint64_t m_nextBeatSample{0};
    int m_currentBeat{0};
    float m_currentClickGain{1.0f};
};

} // namespace Audio
} // namespace Aestra
