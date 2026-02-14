#pragma once
namespace Aestra { namespace Audio {
    struct MeterSnapshotBuffer {
        static constexpr int MAX_CHANNELS = 128; // Stubbed
        void writePeak(int slot, float peakL, float peakR) {}
        void writeLevels(int slot, float peakL, float peakR, float rmsL, float rmsR, float lowL, float lowR, float corr, float lufs = -144.0f) {}
        void setClip(int slot, bool clipL, bool clipR) {}
    };
}}
