// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * @brief Lock-free single-producer / single-consumer queue for live MIDI events.
 *
 * The live note-input path: UI keyboard input (and later each hardware MIDI
 * callback thread) pushes raw events; the audio thread drains them once per
 * processBlock into the per-unit MidiBuffers, alongside pattern playback.
 *
 * SPSC means exactly ONE producer thread per queue instance — a second input
 * source (e.g. hardware MIDI) gets its own queue, drained by the same
 * consumer. No locks, no allocation, fixed power-of-two capacity. push()
 * returns false when full and the event is dropped: for live input, late is
 * worse than lost.
 *
 * Each event carries its target unit id, so note-offs posted after the UI
 * switches the active unit still reach the unit that received the note-on.
 */
class LiveMidiQueue {
public:
    struct Event {
        uint64_t unitId{0}; ///< Target Arsenal unit (UnitID)
        uint8_t status{0};  ///< MIDI status byte (e.g. 0x90 note-on ch1)
        uint8_t data1{0};   ///< First data byte (note number)
        uint8_t data2{0};   ///< Second data byte (velocity)
    };

    /// Producer side (one non-RT thread). Returns false if the queue is full.
    bool push(const Event& ev) noexcept {
        const uint32_t head = m_head.load(std::memory_order_relaxed);
        const uint32_t next = (head + 1) & kMask;
        if (next == m_tail.load(std::memory_order_acquire)) {
            return false;
        }
        m_events[head] = ev;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer side (audio thread). Returns false when empty.
    bool pop(Event& out) noexcept {
        const uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) {
            return false;
        }
        out = m_events[tail];
        m_tail.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    // Public so consumers can bound their drain loops: a producer pushing
    // concurrently can keep pop() succeeding past the queue's snapshot size,
    // so "drain until empty" is not inherently bounded work.
    static constexpr uint32_t kCapacity = 512; // power of two; ~= 5s of frantic playing

private:
    static constexpr uint32_t kMask = kCapacity - 1;

    std::array<Event, kCapacity> m_events{};
    std::atomic<uint32_t> m_head{0};
    std::atomic<uint32_t> m_tail{0};
};

} // namespace Audio
} // namespace Aestra
