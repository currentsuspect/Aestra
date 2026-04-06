// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>

namespace Aestra {

// =============================================================================
// Arena Allocator (Bump Allocator)
// =============================================================================

/**
 * @brief Thread-safe arena allocator for real-time audio buffer management.
 *
 * A bump allocator that pre-allocates a large block and hands out pointers
 * via atomic pointer bump. No per-allocation locks, no fragmentation,
 * O(1) allocation. Reset clears everything at once.
 *
 * Usage:
 *   AudioArena arena(1024 * 1024); // 1 MB
 *   float* buf = static_cast<float*>(arena.allocate(1024 * sizeof(float), alignof(float)));
 *   arena.reset(); // free all at once
 *
 * Thread safety:
 *   - allocate() is lock-free (atomic CAS loop)
 *   - reset() is NOT thread-safe — caller must ensure no concurrent allocate()
 *   - Designed for single-producer (audio thread allocate), single-consumer (reset on idle)
 */
class AudioArena {
public:
    explicit AudioArena(size_t capacityBytes)
        : m_capacity(capacityBytes) {
        // Allocate with max_align_t alignment (typically 16 bytes).
        // For stricter alignments, we add padding to guarantee alignment
        // up to 64 bytes regardless of the base pointer alignment.
        m_buffer = new (std::nothrow) uint8_t[capacityBytes + 63];
        m_rawBuffer = m_buffer;
        // Align the buffer start to 64 bytes (covers all common alignments)
        uintptr_t addr = reinterpret_cast<uintptr_t>(m_rawBuffer);
        uintptr_t aligned = (addr + 63) & ~static_cast<uintptr_t>(63);
        m_buffer = reinterpret_cast<uint8_t*>(aligned);
        m_bufferSize = capacityBytes + 63 - (aligned - addr);
        m_offset.store(0, std::memory_order_relaxed);
        m_peak.store(0, std::memory_order_relaxed);
        m_allocCount.store(0, std::memory_order_relaxed);
    }

    ~AudioArena() {
        delete[] m_rawBuffer;
    }

    // Non-copyable, non-movable
    AudioArena(const AudioArena&) = delete;
    AudioArena& operator=(const AudioArena&) = delete;
    AudioArena(AudioArena&&) = delete;
    AudioArena& operator=(AudioArena&&) = delete;

    /**
     * @brief Allocate memory from the arena.
     *
     * @param size      Number of bytes to allocate.
     * @param alignment Required alignment (must be power of 2, >= 1).
     * @return Pointer to allocated memory, or nullptr if arena is full.
     *
     * Thread-safe via atomic CAS loop. No locks. O(1) time.
     */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept {
        if (!m_buffer || size == 0)
            return nullptr;

        // Alignment must be power of 2
        if ((alignment & (alignment - 1)) != 0)
            return nullptr;

        size_t currentOffset;
        size_t newOffset;
        size_t alignedOffset;

        do {
            currentOffset = m_offset.load(std::memory_order_relaxed);

            // Align the current offset
            alignedOffset = (currentOffset + alignment - 1) & ~(alignment - 1);
            newOffset = alignedOffset + size;

            // Check if we have room
            if (newOffset > m_capacity)
                return nullptr;

            // Try to claim this space via atomic CAS
        } while (!m_offset.compare_exchange_weak(currentOffset, newOffset,
                                                  std::memory_order_relaxed,
                                                  std::memory_order_relaxed));

        // Update peak usage (best-effort, no CAS needed — monotonic is fine)
        size_t peak = m_peak.load(std::memory_order_relaxed);
        while (newOffset > peak) {
            if (m_peak.compare_exchange_weak(peak, newOffset,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed))
                break;
        }

        m_allocCount.fetch_add(1, std::memory_order_relaxed);

        return static_cast<void*>(m_buffer + alignedOffset);
    }

    /**
     * @brief Reset the arena, freeing all allocations.
     *
     * NOT thread-safe — caller must ensure no concurrent allocate() calls.
     * Typical use: reset on UI/idle thread after audio thread is done.
     */
    void reset() noexcept {
        m_offset.store(0, std::memory_order_relaxed);
        m_peak.store(0, std::memory_order_relaxed);
        m_allocCount.store(0, std::memory_order_relaxed);
        std::memset(m_buffer, 0, m_capacity);
    }

    /** @return Total capacity in bytes. */
    size_t capacity() const noexcept { return m_capacity; }

    /** @return Currently allocated bytes (excluding alignment padding). */
    size_t used() const noexcept { return m_offset.load(std::memory_order_relaxed); }

    /** @return Remaining free bytes. */
    size_t remaining() const noexcept { return m_capacity - m_offset.load(std::memory_order_relaxed); }

    /** @return Peak usage since last reset. */
    size_t peakUsage() const noexcept { return m_peak.load(std::memory_order_relaxed); }

    /** @return Total allocation count since last reset. */
    size_t allocationCount() const noexcept { return m_allocCount.load(std::memory_order_relaxed); }

    /** @return Whether the arena has any allocations. */
    bool isEmpty() const noexcept { return m_offset.load(std::memory_order_relaxed) == 0; }

private:
    uint8_t* m_rawBuffer{nullptr};
    uint8_t* m_buffer{nullptr};
    size_t m_capacity{0};
    size_t m_bufferSize{0};
    std::atomic<size_t> m_offset{0};
    std::atomic<size_t> m_peak{0};
    std::atomic<size_t> m_allocCount{0};
};

// =============================================================================
// Global Audio Arena (singleton, lazy-initialized)
// =============================================================================

/**
 * @brief Global audio arena for RT-safe buffer allocation.
 *
 * Default capacity: 4 MB (enough for typical audio buffer needs).
 * Initialize early in audio engine startup. Reset during idle periods.
 */
class GlobalAudioArena {
public:
    static GlobalAudioArena& instance() {
        static GlobalAudioArena inst(4 * 1024 * 1024); // 4 MB default
        return inst;
    }

    explicit GlobalAudioArena(size_t capacityBytes)
        : m_arena(capacityBytes) {}

    AudioArena& arena() noexcept { return m_arena; }
    const AudioArena& arena() const noexcept { return m_arena; }

    /** @brief Convenience: allocate from global arena. */
    void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) noexcept {
        return m_arena.allocate(size, alignment);
    }

    /** @brief Reset the global arena. Call from non-RT thread. */
    void reset() noexcept { m_arena.reset(); }

    /** @brief Current usage stats. */
    size_t used() const noexcept { return m_arena.used(); }
    size_t capacity() const noexcept { return m_arena.capacity(); }
    size_t peakUsage() const noexcept { return m_arena.peakUsage(); }

private:
    AudioArena m_arena;
};

} // namespace Aestra
