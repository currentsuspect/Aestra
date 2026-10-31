// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief The loop the note scheduler plans across, in the monotonic frame domain.
 *
 * A looping transport keeps a pass counter, and the scheduler is handed
 * `pass * length + position`: a frame that never jumps back at a wrap, so the
 * next pass can be queued before the wrap happens. Rewinding at the wrap and
 * re-queuing on the next UI tick made every loop top land late, or not at all,
 * by however long the main loop took.
 *
 * Pattern mode loops [0, length): pass p covers monotonic [p*length, (p+1)*length).
 * A timeline loop is [start, start + length) and the playhead reaches it from
 * the left: pass 0 covers [0, end) — the pre-roll plus the first time round — and
 * pass p >= 1 covers [end + (p-1)*length, end + p*length), timeline frames
 * [start, end). Either way monotonic = p * length + timeline position, so the
 * domain is continuous across every wrap.
 */
struct ScheduleLoop {
    uint64_t start{0};   ///< Loop start, timeline samples (0 in pattern mode).
    uint64_t length{0};  ///< Loop length in samples; 0 = no loop (linear playback).
    bool timeline{false}; ///< Timeline loop: notes are confined to the region, held notes chase into the loop top.

    bool active() const { return length > 0; }
    uint64_t end() const { return start + length; }

    /** @brief The pass containing monotonic frame @p frame. */
    uint64_t passOf(uint64_t frame) const {
        if (!active()) {
            return 0;
        }
        if (!timeline) {
            return frame / length;
        }
        return frame < end() ? 0 : 1 + (frame - end()) / length;
    }

    /** @brief Monotonic frame of timeline position 0 within @p pass. */
    uint64_t baseOf(uint64_t pass) const { return pass * length; }

    /**
     * @brief Confine a timeline-loop note to its pass (monotonic frames, in place).
     *
     * A note starting at or past the loop end belongs to no pass. A note-off past the
     * loop end is pulled back to it (the wrap releases it). From pass 1 on, a note that
     * began before the loop start but is still held there restarts AT the loop start —
     * the wrap used to re-trigger it on the first refill after the wrap; now it lands on
     * the wrap frame. Returns false when the note does not sound in this pass. Pattern
     * loops and linear playback pass through untouched.
     */
    bool placeInPass(uint64_t pass, uint64_t& onFrame, uint64_t& offFrame) const {
        if (!active() || !timeline) {
            return true;
        }
        const uint64_t passStart = baseOf(pass) + (pass == 0 ? 0 : start);
        const uint64_t passEnd = baseOf(pass) + end();
        if (onFrame >= passEnd) {
            return false;
        }
        if (onFrame < passStart) {
            if (offFrame <= passStart) {
                return false;
            }
            onFrame = passStart;
        }
        if (offFrame > passEnd) {
            offFrame = passEnd;
        }
        return true;
    }
};

} // namespace Audio
} // namespace Aestra
