// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraAtomicSharedPtr.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Plugins {

/**
 * @brief Pre-rendered, length-preserving versions of one sampler source, one per semitone.
 *
 * Keep-length pitch mode plays a note at its pitch WITHOUT the resampling speed change.
 * The sampler's own resampler still applies the pitch (so pitch stays exact); this cache
 * supplies a copy of the source that was time-stretched (never transposed) by the same
 * ratio beforehand, so playing it at that ratio lands back on the source's length.
 *
 * Buffers are rendered off the audio thread by one shared render service (a
 * time-stretch pre-rolls ~100 ms of FFT work per note, far too much for a callback).
 *
 * Threading:
 *   - tryGet() and request() are real-time safe: atomic loads, one CAS, no locks,
 *     no allocation, no deallocation.
 *   - Everything else is non-RT (UI, loader, export, tests).
 *   - Buffers the audio thread may still reference are retired through the
 *     GarbageCollector, never freed directly.
 */
class KeepLengthCache : public std::enable_shared_from_this<KeepLengthCache> {
public:
    /// Interleaved stereo frames at the source's sample rate.
    struct Buffer {
        std::vector<float> interleaved;
        size_t frames() const noexcept { return interleaved.size() / 2; }
    };

    static constexpr int MIN_SEMITONES = -48;
    static constexpr int MAX_SEMITONES = 48;
    /// Per-sampler memory budget for rendered buffers; least recently used are evicted first.
    static constexpr size_t MEMORY_BUDGET_BYTES = size_t{48} * 1024 * 1024;

    KeepLengthCache() = default;
    ~KeepLengthCache();
    KeepLengthCache(const KeepLengthCache&) = delete;
    KeepLengthCache& operator=(const KeepLengthCache&) = delete;

    // ---- non-RT ---------------------------------------------------------
    /// Replace the source (interleaved stereo at @p sourceRate). Invalidates every rendered buffer.
    void setSource(std::shared_ptr<const std::vector<float>> interleavedStereo, uint32_t sourceRate);
    /// Queue renders for these semitone offsets (0 and out-of-range offsets are ignored).
    void prewarm(const std::vector<int>& semitones);
    /// Wait until every listed offset is ready or unsupported. False on timeout.
    bool waitUntilSettled(const std::vector<int>& semitones, std::chrono::milliseconds timeout);
    /// Bytes currently held by rendered buffers.
    size_t memoryBytes() const noexcept { return m_bytes.load(std::memory_order_relaxed); }

    // ---- real-time safe ---------------------------------------------------
    /// The rendered buffer for @p semitones, or null if not ready (or 0 / out of range).
    std::shared_ptr<const Buffer> tryGet(int semitones) noexcept;
    /// Ask the render service for @p semitones if nobody has yet. Never blocks.
    void request(int semitones) noexcept;

    // ---- render service -----------------------------------------------------
    /// Render one pending offset if there is one. Returns false when nothing was pending.
    bool renderOnePending();

private:
    enum SlotState : uint8_t { Empty = 0, Pending, Rendering, Ready, Unsupported };
    static constexpr int SLOT_COUNT = MAX_SEMITONES - MIN_SEMITONES + 1;
    static bool inRange(int semitones) noexcept {
        return semitones != 0 && semitones >= MIN_SEMITONES && semitones <= MAX_SEMITONES;
    }
    static size_t slotIndex(int semitones) noexcept { return static_cast<size_t>(semitones - MIN_SEMITONES); }

    struct Slot {
        AtomicSharedPtr<const Buffer> buffer;
        std::atomic<uint8_t> state{Empty};
        std::atomic<uint64_t> lastUsed{0};
    };

    void ensureRegistered();
    void evictToBudget(size_t keepIndex);
    void retire(std::shared_ptr<const Buffer> buffer);

    std::array<Slot, SLOT_COUNT> m_slots;
    std::atomic<uint64_t> m_useClock{1};
    std::atomic<size_t> m_bytes{0};
    std::atomic<bool> m_registered{false};

    // Guards the source and generation (non-RT only).
    mutable std::mutex m_mutex;
    std::condition_variable m_settled;
    std::shared_ptr<const std::vector<float>> m_source;
    uint32_t m_sourceRate = 0;
    uint64_t m_generation = 0;
};

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
