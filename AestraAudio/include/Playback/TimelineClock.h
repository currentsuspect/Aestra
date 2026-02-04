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
    double beat; ///< Beat position of the change
    double bpm;  ///< New BPM value
};

/**
 * @brief Handles tempo maps, beat/time conversions, and sample frame calculations.
 *
 * This class is independent of the PatternPlaybackEngine and provides
 * musical time to sample time conversions.
 */
class TimelineClock {
public:
    /**
     * @brief Construct a new Timeline Clock.
     *
     * @param defaultBPM The initial tempo in Beats Per Minute.
     */
    TimelineClock(double defaultBPM = 120.0);

    /**
     * @brief Set the constant tempo (clears any tempo map).
     *
     * @param bpm The new tempo in Beats Per Minute.
     */
    void setTempo(double bpm);

    /**
     * @brief Set the tempo map for variable tempo playback.
     *
     * @param tempoMap A vector of TempoChange events.
     */
    void setTempoMap(const std::vector<TempoChange>& tempoMap);

    /**
     * @brief Get the effective tempo at a specific beat position.
     *
     * @param beat The beat position.
     * @return double The BPM at that position.
     */
    double getTempoAtBeat(double beat) const;

    /**
     * @brief Convert a beat position to seconds from the start.
     *
     * @param beat The beat position.
     * @return double Time in seconds.
     */
    double secondsAtBeat(double beat) const;

    /**
     * @brief Convert a beat position to sample frame index.
     *
     * @param beat The beat position.
     * @param sampleRate The audio sample rate.
     * @return uint64_t The frame index.
     */
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;

    /**
     * @brief Convert a sample frame index to beat position.
     *
     * @param frame The sample frame index.
     * @param sampleRate The audio sample rate.
     * @return double The beat position.
     */
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM;
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
