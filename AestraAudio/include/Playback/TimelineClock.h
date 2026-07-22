// © 2025 Aestra Studios — All Rights Reserved.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

struct TempoChange {
    double beat{0.0};
    double bpm{120.0};
};

class TimelineClock {
public:
    explicit TimelineClock(double defaultBPM = 120.0);

    void setTempo(double bpm);
    void setTempoMap(const std::vector<TempoChange>& tempoMap);

    double getTempoAtBeat(double beat) const;
    double getCurrentTempo() const { return m_defaultBPM; }

    // Time signature numerator (beats per bar). The musical source of truth
    // for bar math everywhere: Arsenal grids, pattern lengths, metronome
    // accents, and piano-roll bar lines all derive from this.
    void setBeatsPerBar(int beats) { m_beatsPerBar = std::clamp(beats, 1, 16); }
    int getBeatsPerBar() const { return m_beatsPerBar; }
    double secondsAtBeat(double beat) const;
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM{120.0};
    int m_beatsPerBar{4};
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
