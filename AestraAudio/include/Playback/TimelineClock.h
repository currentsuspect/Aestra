// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <cstdint>

namespace Aestra {
namespace Audio {

struct TempoChange {
    double beat;
    double bpm;
};

/**
 * @brief Handles tempo maps and time conversions.
 */
class TimelineClock {
public:
    /**
     * @brief Constructor
     * @param defaultBPM The initial tempo in Beats Per Minute.
     */
    explicit TimelineClock(double defaultBPM = 120.0);

    /**
     * @brief Set a constant tempo.
     * @param bpm The new tempo.
     */
    void setTempo(double bpm);

    /**
     * @brief Set a complex tempo map.
     * @param tempoMap Vector of tempo changes.
     */
    void setTempoMap(const std::vector<TempoChange>& tempoMap);

    /**
     * @brief Get the tempo at a specific beat.
     * @param beat The beat position.
     * @return The BPM at that beat.
     */
    double getTempoAtBeat(double beat) const;

    /**
     * @brief Convert beats to seconds.
     * @param beat The beat position.
     * @return Time in seconds.
     */
    double secondsAtBeat(double beat) const;

    /**
     * @brief Convert beat position to sample frame.
     * @param beat The beat position.
     * @param sampleRate The audio sample rate.
     * @return The sample frame index.
     */
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;

    /**
     * @brief Convert sample frame to beat position.
     * @param frame The sample frame index.
     * @param sampleRate The audio sample rate.
     * @return The beat position.
     */
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM;
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
