// © 2025 Aestra Studios — All Rights Reserved.
#include "TimelineClock.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

TimelineClock::TimelineClock(double defaultBPM) : m_defaultBPM(defaultBPM) {}

void TimelineClock::setTempo(double bpm) {
    m_defaultBPM = bpm;
    m_tempoMap.clear(); // Single tempo mode
}

void TimelineClock::setTempoMap(const std::vector<TempoChange>& tempoMap) {
    m_tempoMap = tempoMap;
    std::sort(m_tempoMap.begin(), m_tempoMap.end(),
              [](const TempoChange& a, const TempoChange& b) { return a.beat < b.beat; });
}

double TimelineClock::getTempoAtBeat(double beat) const {
    if (m_tempoMap.empty()) {
        return m_defaultBPM;
    }

    // Find last tempo change before or at this beat
    for (auto it = m_tempoMap.rbegin(); it != m_tempoMap.rend(); ++it) {
        if (it->beat <= beat) {
            return it->bpm;
        }
    }

    return m_defaultBPM; // Before first tempo change
}

double TimelineClock::secondsAtBeat(double beat) const {
    if (m_tempoMap.empty()) {
        // Simple case: constant tempo
        // seconds = (beat / bpm) * 60
        return (beat / m_defaultBPM) * 60.0;
    }

    // Complex case: accumulate time through tempo changes
    double totalSeconds = 0.0;
    double lastBeat = 0.0;

    for (const auto& change : m_tempoMap) {
        if (change.beat >= beat) {
            // Add time from lastBeat to target beat at previous tempo
            double bpm = (totalSeconds == 0.0) ? m_defaultBPM : getTempoAtBeat(lastBeat);
            totalSeconds += ((beat - lastBeat) / bpm) * 60.0;
            return totalSeconds;
        }

        // Add time segment at current tempo
        double bpm = getTempoAtBeat(lastBeat);
        totalSeconds += ((change.beat - lastBeat) / bpm) * 60.0;
        lastBeat = change.beat;
    }

    // After last tempo change
    double bpm = getTempoAtBeat(lastBeat);
    totalSeconds += ((beat - lastBeat) / bpm) * 60.0;
    return totalSeconds;
}

uint64_t TimelineClock::sampleFrameAtBeat(double beat, int sampleRate) const {
    double seconds = secondsAtBeat(beat);
    return static_cast<uint64_t>(std::round(seconds * sampleRate));
}

double TimelineClock::beatAtSampleFrame(uint64_t frame, int sampleRate) const {
    double seconds = static_cast<double>(frame) / sampleRate;

    if (m_tempoMap.empty()) {
        return (seconds / 60.0) * m_defaultBPM;
    }

    // Walk through tempo segments to find which contains the target time
    double accumulatedSeconds = 0.0;
    double lastBeat = 0.0;

    for (size_t i = 0; i < m_tempoMap.size(); ++i) {
        double bpm = (i == 0) ? m_defaultBPM : m_tempoMap[i - 1].bpm;
        double segmentBeats = m_tempoMap[i].beat - lastBeat;
        double segmentSeconds = (segmentBeats / bpm) * 60.0;

        if (accumulatedSeconds + segmentSeconds >= seconds) {
            // Target is within this segment
            double remainingSeconds = seconds - accumulatedSeconds;
            double remainingBeats = (remainingSeconds / 60.0) * bpm;
            return lastBeat + remainingBeats;
        }

        accumulatedSeconds += segmentSeconds;
        lastBeat = m_tempoMap[i].beat;
    }

    // Target is after the last tempo change
    double lastBPM = m_tempoMap.back().bpm;
    double remainingSeconds = seconds - accumulatedSeconds;
    double remainingBeats = (remainingSeconds / 60.0) * lastBPM;
    return lastBeat + remainingBeats;
}

} // namespace Audio
} // namespace Aestra
