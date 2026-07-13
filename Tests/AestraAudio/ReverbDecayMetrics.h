// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace Aestra::Audio::Tests {

struct ReverbDecayMetrics {
    float t20 = 0.0f;
    float t40 = 0.0f;
    float t60 = 0.0f;
    float maxReboundDb = 0.0f;
    bool t20Reached = false;
    bool t40Reached = false;
    bool t60Reached = false;
    bool monotonic = true;
};

inline ReverbDecayMetrics computeReverbDecayMetrics(const std::vector<float>& energy, float sampleRate) {
    ReverbDecayMetrics metrics;
    if (energy.empty() || !(sampleRate > 0.0f)) {
        return metrics;
    }

    const size_t count = energy.size();
    std::vector<float> schroeder(count);
    double accumulatedEnergy = 0.0;
    for (size_t i = count; i > 0; --i) {
        accumulatedEnergy += std::max(0.0f, energy[i - 1]);
        schroeder[i - 1] = static_cast<float>(accumulatedEnergy);
    }

    const float peakDb = 10.0f * std::log10(std::max(schroeder[0], 1.0e-20f));
    float previousDb = peakDb;
    const float windowMs = 256.0f / sampleRate * 1000.0f;

    for (size_t i = 1; i < count; ++i) {
        const float db = 10.0f * std::log10(std::max(schroeder[i], 1.0e-20f));
        if (db > previousDb + 1.0f) {
            metrics.monotonic = false;
            metrics.maxReboundDb = std::max(metrics.maxReboundDb, db - previousDb);
        }
        previousDb = db;

        const float timeMs = static_cast<float>(i) * windowMs;
        if (!metrics.t20Reached && db <= peakDb - 20.0f) {
            metrics.t20 = timeMs;
            metrics.t20Reached = true;
        }
        if (!metrics.t40Reached && db <= peakDb - 40.0f) {
            metrics.t40 = timeMs;
            metrics.t40Reached = true;
        }
        if (!metrics.t60Reached && db <= peakDb - 60.0f) {
            metrics.t60 = timeMs;
            metrics.t60Reached = true;
        }
    }

    return metrics;
}

} // namespace Aestra::Audio::Tests
