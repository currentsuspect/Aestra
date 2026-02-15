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

class TimelineClock {
public:
    TimelineClock(double defaultBPM);

    void setTempo(double bpm);
    void setTempoMap(const std::vector<TempoChange>& tempoMap);

    double getTempoAtBeat(double beat) const;
    double secondsAtBeat(double beat) const;
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM;
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
