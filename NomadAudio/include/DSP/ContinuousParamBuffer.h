// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace Nomad {
namespace Audio {

namespace ParamBitcast {

inline uint32_t floatToU32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline float u32ToFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

} // namespace ParamBitcast

/**
 * @brief RT-safe continuous parameter buffer for mixer controls.
 *
 * Stores per-channel fader/pan/trim values in a dense slot-indexed array.
 * UI thread writes values via atomics, audio thread reads them without locking.
 *
 * Dirty bits allow the audio thread to avoid re-reading unchanged parameters.
 */
class ContinuousParamBuffer {
public:
    static constexpr size_t MAX_SLOTS = 128; // 0..126 = channels, 127 = master

    static constexpr uint8_t DIRTY_FADER = 0x01;
    static constexpr uint8_t DIRTY_PAN = 0x02;
    static constexpr uint8_t DIRTY_TRIM = 0x04;

    void setFaderDb(uint32_t slotIndex, float db) {
        if (slotIndex >= MAX_SLOTS) return;
        auto& slot = m_slots[slotIndex];
        slot.faderDbBits.store(ParamBitcast::floatToU32(db), std::memory_order_relaxed);
        slot.dirty.fetch_or(DIRTY_FADER, std::memory_order_release);
    }

    void setPan(uint32_t slotIndex, float pan) {
        if (slotIndex >= MAX_SLOTS) return;
        auto& slot = m_slots[slotIndex];
        slot.panBits.store(ParamBitcast::floatToU32(pan), std::memory_order_relaxed);
        slot.dirty.fetch_or(DIRTY_PAN, std::memory_order_release);
    }

    void setTrimDb(uint32_t slotIndex, float db) {
        if (slotIndex >= MAX_SLOTS) return;
        auto& slot = m_slots[slotIndex];
        slot.trimDbBits.store(ParamBitcast::floatToU32(db), std::memory_order_relaxed);
        slot.dirty.fetch_or(DIRTY_TRIM, std::memory_order_release);
    }

    /**
     * @brief Read current parameter values (does not clear dirty bits).
     */
    void read(uint32_t slotIndex, float& faderDb, float& pan, float& trimDb) const {
        if (slotIndex >= MAX_SLOTS) {
            faderDb = 0.0f;
            pan = 0.0f;
            trimDb = 0.0f;
            return;
        }
        const auto& slot = m_slots[slotIndex];
        faderDb = ParamBitcast::u32ToFloat(slot.faderDbBits.load(std::memory_order_relaxed));
        pan = ParamBitcast::u32ToFloat(slot.panBits.load(std::memory_order_relaxed));
        trimDb = ParamBitcast::u32ToFloat(slot.trimDbBits.load(std::memory_order_relaxed));
    }

    /**
     * @brief Consume changed params for a slot (clears dirty bits).
     *
     * @return Dirty mask (DIRTY_*) indicating which values changed.
     */
    uint8_t consumeIfDirty(uint32_t slotIndex, float& faderDb, float& pan, float& trimDb) {
        if (slotIndex >= MAX_SLOTS) {
            faderDb = 0.0f;
            pan = 0.0f;
            trimDb = 0.0f;
            return 0;
        }

        auto& slot = m_slots[slotIndex];
        const uint8_t dirtyMask = slot.dirty.exchange(0, std::memory_order_acq_rel);

        if (dirtyMask & DIRTY_FADER) {
            faderDb = ParamBitcast::u32ToFloat(slot.faderDbBits.load(std::memory_order_relaxed));
        }
        if (dirtyMask & DIRTY_PAN) {
            pan = ParamBitcast::u32ToFloat(slot.panBits.load(std::memory_order_relaxed));
        }
        if (dirtyMask & DIRTY_TRIM) {
            trimDb = ParamBitcast::u32ToFloat(slot.trimDbBits.load(std::memory_order_relaxed));
        }

        return dirtyMask;
    }

    void resetAll() {
        for (auto& slot : m_slots) {
            slot.faderDbBits.store(ParamBitcast::floatToU32(0.0f), std::memory_order_relaxed);
            slot.panBits.store(ParamBitcast::floatToU32(0.0f), std::memory_order_relaxed);
            slot.trimDbBits.store(ParamBitcast::floatToU32(0.0f), std::memory_order_relaxed);
            slot.dirty.store(0, std::memory_order_relaxed);
        }
    }

private:
    struct SlotParams {
        std::atomic<uint32_t> faderDbBits{ParamBitcast::floatToU32(0.0f)};
        std::atomic<uint32_t> panBits{ParamBitcast::floatToU32(0.0f)};
        std::atomic<uint32_t> trimDbBits{ParamBitcast::floatToU32(0.0f)};
        std::atomic<uint8_t> dirty{0};
    };

    std::array<SlotParams, MAX_SLOTS> m_slots{};
};

} // namespace Audio
} // namespace Nomad

