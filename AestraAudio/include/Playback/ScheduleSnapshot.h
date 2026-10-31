// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ScheduleLoop.h"

#include <atomic>
#include <cstdint>

namespace Aestra {
namespace Audio {

/** @brief What the audio thread last played, as the note scheduler needs it. */
struct ScheduleSnapshot {
    uint64_t frame{0};  ///< Next block's start, in @ref loop's monotonic domain.
    ScheduleLoop loop;  ///< The loop that frame is expressed in.
    uint16_t epoch{0};  ///< Scheduler queue epoch current when the frame was published.
};

/**
 * @brief Single-writer seqlock carrying a ScheduleSnapshot from the audio thread.
 *
 * The scheduler needs the frame, the loop it is measured in and the queue epoch as
 * ONE tuple: a frame paired with another block's loop schedules a whole pass off,
 * and a frame paired with a newer epoch revives events planned from a stale
 * position. The writer never waits (publish is a handful of relaxed stores
 * bracketed by a sequence counter); readers retry a bounded number of times.
 */
class ScheduleSnapshotPublisher {
public:
    /** @brief Audio thread only. Wait-free. */
    void publish(const ScheduleSnapshot& snapshot) noexcept {
        const uint32_t seq = m_seq.load(std::memory_order_relaxed);
        m_seq.store(seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        m_frame.store(snapshot.frame, std::memory_order_relaxed);
        m_loopStart.store(snapshot.loop.start, std::memory_order_relaxed);
        m_loopLength.store(snapshot.loop.length, std::memory_order_relaxed);
        m_timeline.store(snapshot.loop.timeline, std::memory_order_relaxed);
        m_epoch.store(snapshot.epoch, std::memory_order_relaxed);
        m_seq.store(seq + 2, std::memory_order_release);
    }

    /** @brief Any thread: whether the audio thread has published at least once. */
    bool hasPublished() const noexcept { return m_seq.load(std::memory_order_acquire) != 0; }

    /** @brief Any thread. False when nothing was published yet or the writer kept racing the read. */
    bool read(ScheduleSnapshot& out) const noexcept {
        for (int attempt = 0; attempt < 64; ++attempt) {
            const uint32_t before = m_seq.load(std::memory_order_acquire);
            if (before == 0) {
                return false;
            }
            if ((before & 1u) != 0) {
                continue;
            }
            ScheduleSnapshot snapshot;
            snapshot.frame = m_frame.load(std::memory_order_relaxed);
            snapshot.loop.start = m_loopStart.load(std::memory_order_relaxed);
            snapshot.loop.length = m_loopLength.load(std::memory_order_relaxed);
            snapshot.loop.timeline = m_timeline.load(std::memory_order_relaxed);
            snapshot.epoch = m_epoch.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (m_seq.load(std::memory_order_relaxed) == before) {
                out = snapshot;
                return true;
            }
        }
        return false;
    }

private:
    std::atomic<uint32_t> m_seq{0};
    std::atomic<uint64_t> m_frame{0};
    std::atomic<uint64_t> m_loopStart{0};
    std::atomic<uint64_t> m_loopLength{0};
    std::atomic<bool> m_timeline{false};
    std::atomic<uint16_t> m_epoch{0};
};

inline bool operator==(const ScheduleLoop& a, const ScheduleLoop& b) {
    return a.start == b.start && a.length == b.length && a.timeline == b.timeline;
}
inline bool operator!=(const ScheduleLoop& a, const ScheduleLoop& b) { return !(a == b); }

} // namespace Audio
} // namespace Aestra
