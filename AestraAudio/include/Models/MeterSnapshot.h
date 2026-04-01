// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Aestra {
namespace Audio {

/**
 * @brief Lock-free meter snapshot buffer for audio→UI communication.
 *
 * Audio thread writes peak/RMS/clip data per channel slot.
 * UI thread reads at frame rate for meter display.
 *
 * Uses atomic stores with relaxed ordering (single-writer per slot on audio thread,
 * single-reader on UI thread — no cross-slot ordering needed).
 * Clip flags are sticky: audio sets them, UI clears them via clearClip().
 */
struct MeterSnapshotBuffer {
    static constexpr int MAX_CHANNELS = 128;

    struct MeterReadout {
        float peakL = 0.0f;
        float peakR = 0.0f;
        float rmsL = 0.0f;
        float rmsR = 0.0f;
        float lowL = 0.0f;
        float lowR = 0.0f;
        float lufs = -144.0f;
        float integratedLufs = -144.0f;
        float correlation = 0.0f;
        bool clipL = false;
        bool clipR = false;
    };

    // Per-slot atomic storage (audio writes, UI reads)
    struct Slot {
        std::atomic<float> peakL{0.0f};
        std::atomic<float> peakR{0.0f};
        std::atomic<float> rmsL{0.0f};
        std::atomic<float> rmsR{0.0f};
        std::atomic<float> lowL{0.0f};
        std::atomic<float> lowR{0.0f};
        std::atomic<float> correlation{0.0f};
        std::atomic<float> lufs{-144.0f};
        std::atomic<float> integratedLufs{-144.0f};
        std::atomic<int> clipL{0}; // 0 = no clip, 1 = clipping (sticky until cleared)
        std::atomic<int> clipR{0};
    };

    Slot slots[MAX_CHANNELS];

    void writePeak(int slot, float pL, float pR) {
        if (slot < 0 || slot >= MAX_CHANNELS) return;
        slots[slot].peakL.store(pL, std::memory_order_relaxed);
        slots[slot].peakR.store(pR, std::memory_order_relaxed);
    }

    void writeLevels(int slot, float pL, float pR, float rL, float rR,
                     float loL, float loR, float corr, float lufs = -144.0f) {
        if (slot < 0 || slot >= MAX_CHANNELS) return;
        auto& s = slots[slot];
        s.peakL.store(pL, std::memory_order_relaxed);
        s.peakR.store(pR, std::memory_order_relaxed);
        s.rmsL.store(rL, std::memory_order_relaxed);
        s.rmsR.store(rR, std::memory_order_relaxed);
        s.lowL.store(loL, std::memory_order_relaxed);
        s.lowR.store(loR, std::memory_order_relaxed);
        s.correlation.store(corr, std::memory_order_relaxed);
        s.lufs.store(lufs, std::memory_order_relaxed);
        // integratedLufs is updated separately by LUFS metering
    }

    void setClip(int slot, bool cL, bool cR) {
        if (slot < 0 || slot >= MAX_CHANNELS) return;
        if (cL) slots[slot].clipL.store(1, std::memory_order_relaxed);
        if (cR) slots[slot].clipR.store(1, std::memory_order_relaxed);
    }

    void clearClip(int slot) {
        if (slot < 0 || slot >= MAX_CHANNELS) return;
        slots[slot].clipL.store(0, std::memory_order_relaxed);
        slots[slot].clipR.store(0, std::memory_order_relaxed);
    }

    MeterReadout readMeter(int slot) const {
        if (slot < 0 || slot >= MAX_CHANNELS) return {};
        const auto& s = slots[slot];
        MeterReadout r;
        r.peakL = s.peakL.load(std::memory_order_relaxed);
        r.peakR = s.peakR.load(std::memory_order_relaxed);
        r.rmsL = s.rmsL.load(std::memory_order_relaxed);
        r.rmsR = s.rmsR.load(std::memory_order_relaxed);
        r.lowL = s.lowL.load(std::memory_order_relaxed);
        r.lowR = s.lowR.load(std::memory_order_relaxed);
        r.correlation = s.correlation.load(std::memory_order_relaxed);
        r.lufs = s.lufs.load(std::memory_order_relaxed);
        r.integratedLufs = s.integratedLufs.load(std::memory_order_relaxed);
        r.clipL = s.clipL.load(std::memory_order_relaxed) != 0;
        r.clipR = s.clipR.load(std::memory_order_relaxed) != 0;
        return r;
    }

    MeterReadout readSnapshot(int slot) const { return readMeter(slot); }
};

} // namespace Audio
} // namespace Aestra
