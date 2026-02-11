// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief Structure representing a tempo change event.
 */
struct TempoChange {
    double beat; ///< The beat position where the tempo changes
    double bpm;  ///< The new tempo in Beats Per Minute
};

/**
 * @brief Handles musical time conversions (Beats <-> Seconds <-> Samples).
 *
 * Supports constant tempo or a tempo map with multiple changes.
 */
class TimelineClock {
public:
    /**
     * @brief Constructor
     * @param defaultBPM Initial tempo in Beats Per Minute (default: 120.0)
     */
    explicit TimelineClock(double defaultBPM = 120.0);

    /**
     * @brief Set a constant tempo, clearing any existing tempo map.
     * @param bpm New tempo in BPM
     */
    void setTempo(double bpm);

    /**
     * @brief Set a complex tempo map.
     * @param tempoMap Vector of tempo changes (will be sorted by beat)
     */
    void setTempoMap(const std::vector<TempoChange>& tempoMap);

    /**
     * @brief Get the effective tempo at a specific beat position.
     * @param beat Beat position
     * @return Tempo in BPM
     */
    double getTempoAtBeat(double beat) const;

    /**
     * @brief Convert a beat position to seconds.
     * @param beat Beat position
     * @return Time in seconds from the start of the timeline
     */
    double secondsAtBeat(double beat) const;

    /**
     * @brief Convert a beat position to sample frame index.
     * @param beat Beat position
     * @param sampleRate Audio sample rate
     * @return Sample frame index
     */
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;

    /**
     * @brief Convert a sample frame index to beat position.
     * @param frame Sample frame index
     * @param sampleRate Audio sample rate
     * @return Beat position
     */
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM;
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
