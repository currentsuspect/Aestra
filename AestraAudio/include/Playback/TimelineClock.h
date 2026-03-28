// © 2025 Aestra Studios — All Rights Reserved.
#pragma once

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
    double getCurrentTempo() const { return getTempoAtBeat(0.0); }
    double secondsAtBeat(double beat) const;
    uint64_t sampleFrameAtBeat(double beat, int sampleRate) const;
    double beatAtSampleFrame(uint64_t frame, int sampleRate) const;

private:
    double m_defaultBPM{120.0};
    std::vector<TempoChange> m_tempoMap;
};

} // namespace Audio
} // namespace Aestra
