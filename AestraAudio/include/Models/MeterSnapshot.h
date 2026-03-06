#pragma once
#include <cstdint>

namespace Aestra {
namespace Audio {
struct MeterSnapshotBuffer {
    static constexpr int MAX_CHANNELS = 128; // Stubbed

    // STUB: MeterReadout — Phase 2 will populate with real peak/RMS/LUFS data
    struct MeterReadout {
        float peakL = 0.0f;
        float peakR = 0.0f;
        float rmsL = 0.0f;
        float rmsR = 0.0f;
        float lufs = -144.0f;
        float correlation = 0.0f;
        bool clipL = false;
        bool clipR = false;
    };

    void writePeak(int slot, float peakL, float peakR) {}
    void writeLevels(int slot, float peakL, float peakR, float rmsL, float rmsR, float lowL, float lowR, float corr,
                     float lufs = -144.0f) {}
    void setClip(int slot, bool clipL, bool clipR) {}

    // STUB: clearClip — Phase 2 will reset clip indicators per slot
    void clearClip(int slot) {}

    // STUB: readMeter — Phase 2 will return actual metering data
    MeterReadout readMeter(int slot) const { return {}; }
};

// STUB: ContinuousParamBuffer — Phase 2 will hold real-time automation snapshots
struct ContinuousParamBuffer {
    // Placeholder for continuous parameter automation readback
};

} // namespace Audio
} // namespace Aestra
