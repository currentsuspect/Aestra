// © 2025 Nomad Studios — All Rights Reserved.
#pragma once

#include <cstdint>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Tempo change in timeline
 */
struct TempoChange {
    double beat;     // When tempo changes
    double bpm;      // New tempo
};

/**
 * @brief Sample-accurate beat↔sample conversion with tempo map support
 * 
 * Thread-safe for reading (tempo map changes require rebuild)
 */
class TimelineClock {
public:
    TimelineClock(double defaultBPM = 120.0);
    
    /**
     * Convert beat to absolute sample frame
     * @param beat Musical beat position (0.0 = start)
     * @param sampleRate Audio sample rate
     * @return Absolute sample frame
     */
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;
    
    /**
     * Convert sample frame to beat
     * @param frame Absolute sample frame
     * @param sampleRate Audio sample rate
     * @return Musical beat position
     */
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;
    
    /**
     * Convert beat to seconds (tempo-aware)
     */
    double secondsAtBeat(double beat) const;
    
    /**
     * Set single tempo (simple case)
     */
    void setTempo(double bpm);
    
    /**
     * Set tempo map (advanced: tempo changes over time)
     */
    void setTempoMap(const std::vector<TempoChange>& tempoMap);
    
    double getCurrentTempo() const { return m_defaultBPM; }
    
private:
    double m_defaultBPM;
    std::vector<TempoChange> m_tempoMap;
    
    // Helper: Find active tempo at beat
    double getTempoAtBeat(double beat) const;
};

} // namespace Audio
} // namespace Nomad
