// © 2025 Aestra Studios — All Rights Reserved.
#pragma once

#include "../Models/PatternManager.h"
#include "../Models/PatternSource.h"
#include "../Plugin/PluginHost.h" // For MidiBuffer
#include "TimelineClock.h"
#include "../Models/UnitManager.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declaration
class MixerChannel;

/**
 * @brief Compact, cache-aligned scheduled MIDI event (32 bytes)
 */
struct ScheduledEvent {
    /** @brief Absolute sample frame at which the event should fire. */
    uint64_t sampleFrame; // 8 bytes
    /** @brief Destination unit identifier. */
    UnitID unitId;        // 8 bytes (Moved)
    /** @brief Pattern-instance identifier used for cancellation. */
    uint32_t instanceId;  // 4 bytes
    /** @brief Resolved MIDI channel index. */
    uint16_t channelIdx;  // 2 bytes
    /** @brief MIDI status byte. */
    uint8_t statusByte;   // 1 byte (MIDI status)
    /** @brief First MIDI data byte, usually note number. */
    uint8_t data1;        // 1 byte (note number)
    /** @brief Second MIDI data byte, usually velocity. */
    uint8_t data2;        // 1 byte (velocity)
    /** @brief Event priority inside a frame. */
    uint8_t priority;     // 1 byte (0=note-off, 1=per-note pan, 2=note-on)
    uint8_t _padding[6];  // 6 bytes -> Total 32.
};
static_assert(sizeof(ScheduledEvent) == 32, "ScheduledEvent must be 32 bytes");

/**
 * @brief Lock-free SPSC (single-producer, single-consumer) ring buffer
 *
 * Simple bounded queue for RT-safe event transfer
 */
template <typename T, size_t Capacity> class LockFreeSPSCQueue {
public:
    /**
     * @brief Create an empty single-producer/single-consumer queue.
     */
    LockFreeSPSCQueue() : m_head(0), m_tail(0) {}

    /**
     * @brief Push an item into the queue.
     * @param item Item to enqueue.
     * @return False when the queue is full.
     */
    bool push(const T& item) {
        uint32_t head = m_head.load(std::memory_order_relaxed);
        uint32_t nextHead = (head + 1) % Capacity;

        if (nextHead == m_tail.load(std::memory_order_acquire)) {
            return false; // Queue full
        }

        m_buffer[head] = item;
        m_head.store(nextHead, std::memory_order_release);
        return true;
    }

    /**
     * @brief Read the next queued item without removing it.
     * @param item Output slot for the queued item.
     * @return False when the queue is empty.
     */
    bool peek(T& item) const {
        uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        item = m_buffer[tail];
        return true;
    }

    /**
     * @brief Remove the current front item.
     */
    void pop() {
        uint32_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store((tail + 1) % Capacity, std::memory_order_release);
    }

    /**
     * @brief Drain all items from the producer side (RT-safe).
     *
     * Resets the write head to the current read tail, discarding all queued
     * items.  Only safe to call from the producer thread when the consumer
     * thread is known to be idle or tolerant of an empty queue.
     */
    void forceDrain() {
        uint32_t tail = m_tail.load(std::memory_order_acquire);
        m_head.store(tail, std::memory_order_release);
    }

    /**
     * @brief Get the current queue occupancy.
     * @return Number of queued items.
     */
    size_t size() const {
        uint32_t head = m_head.load(std::memory_order_acquire);
        uint32_t tail = m_tail.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : (Capacity - tail + head);
    }

private:
    std::array<T, Capacity> m_buffer;
    std::atomic<uint32_t> m_head;
    std::atomic<uint32_t> m_tail;
};

/**
 * @brief Pattern playback engine with lookahead scheduling
 *
 * Scheduler thread: schedulePatternInstance() → refillWindow()
 * Audio thread: processAudio() (RT-safe)
 */
class PatternPlaybackEngine {
public:
    /**
     * @brief Create the pattern scheduler.
     * @param clock Timeline clock used for beat/frame conversion.
     * @param patternMgr Pattern manager containing the source note data.
     * @param unitMgr Unit manager used for routing and channel lookup.
     */
    PatternPlaybackEngine(TimelineClock* clock, PatternManager* patternMgr, UnitManager* unitMgr);

    /**
     * @brief Route descriptor for allocation-free MIDI fanout on the audio thread.
     */
    struct UnitMidiRoute {
        /** @brief Destination unit identifier. */
        UnitID unitId{0};
        /** @brief Destination MIDI buffer for that unit. */
        MidiBuffer* midiBuffer{nullptr};

        UnitMidiRoute() = default;
        /**
         * @brief Create a route entry.
         * @param id Destination unit identifier.
         * @param buf Destination MIDI buffer.
         */
        UnitMidiRoute(UnitID id, MidiBuffer* buf) : unitId(id), midiBuffer(buf) {}
    };

    /**
     * Schedule new pattern instance (non-RT thread)
     * @param pid Pattern identifier to schedule.
     * @param startBeat Beat at which the pattern starts.
     * @param instanceId Caller-supplied instance identifier.
     */
    void schedulePatternInstance(PatternID pid, double startBeat, uint32_t instanceId,
                                 double sourceStartBeat = 0.0, double durationBeats = -1.0);

    /**
     * Cancel pattern instance via atomic flag (RT-safe)
     * @param instanceId Pattern-instance identifier to cancel.
     */
    void cancelPatternInstance(uint32_t instanceId);

    /**
     * Refill lookahead window with events (non-RT thread)
     * Called periodically by scheduler
     * @param currentFrame Current transport frame. In looped pattern mode this
     *        is MONOTONIC (iteration * loopLengthSamples + wrapped position) so
     *        the window can pre-schedule the next iteration's events before the
     *        wrap — rewinding at the wrap and rescheduling on the next UI tick
     *        made every loop's downbeat land late by the maintenance latency.
     * @param sampleRate Active sample rate.
     * @param lookaheadSamples Number of frames to schedule ahead.
     * @param loopLengthSamples Loop length in samples for looped pattern mode
     *        (loop start == 0); 0 = no loop (timeline / offline bounce).
     */
    void refillWindow(uint64_t currentFrame, int sampleRate, int lookaheadSamples = 4096,
                      uint64_t loopLengthSamples = 0);

    /**
     * Process audio callback (RT-safe, audio thread only)
     * Allocation-free array-based MIDI routing.
     * @param currentFrame Current transport frame.
     * @param bufferSize Number of frames in the current audio callback.
     * @param routes Route array for unit MIDI fanout.
     * @param routeCount Number of route entries in @p routes.
     */
    void processAudio(uint64_t currentFrame, int bufferSize, const UnitMidiRoute* routes, size_t routeCount) noexcept;

    /**
     * @brief Rewind queued events and all scheduled instances.
     *
     * Rewind, NOT removal: instances stay scheduled and re-emit from the top. That is
     * what a loop restart needs. Use clearScheduledInstances() when the content must not
     * carry forward at all.
     */
    void rewindScheduledInstances();

    /**
     * @brief Pattern content changed while playing (RT-safe producer-side flag).
     *
     * Steps placed or removed in the Arsenal / Piano Roll call this instead of
     * rewindScheduledInstances(): the next refill re-queues from the playhead with
     * current note data — deletions silence immediately and additions enter at their
     * exact frame — WITHOUT resetting the entry state. A full rewind here re-fired
     * every currently-sounding note (audible flam on each edit) and made fresh
     * placements sound before the playhead reached them (#823).
     */
    void patternContentEdited();

    /**
     * @brief Remove every scheduled instance and queued event.
     *
     * For transitions that must not carry pattern content forward — leaving Arsenal, or
     * starting timeline playback. rewindScheduledInstances() alone was insufficient there:
     * it rewinds, so an Arsenal instance survived into timeline playback and kept sounding.
     * Control thread only (takes the scheduler mutex); the RT path reads m_rtQueue only.
     */
    void clearScheduledInstances();

    /**
     * @brief Number of scheduled pattern instances (control thread).
     * @return Count of live instances.
     */
    size_t getActiveInstanceCount() const;

    /**
     * @brief Get the number of scheduler overflows observed so far.
     * @return Overflow counter value.
     */
    uint32_t getOverflowCount() const { return m_overflowCounter.load(std::memory_order_relaxed); }
    /**
     * @brief Get the number of scheduled events processed so far.
     * @return Processed event counter value.
     */
    uint32_t getProcessedEventCount() const { return m_processedCounter.load(std::memory_order_relaxed); }

private:
    // Pattern instance in scheduler
    struct PatternInstance {
        PatternID patternId;
        double startBeat;
        uint32_t instanceId;
        double sourceStartBeat;
        double sourceEndBeat;
        uint64_t scheduledThroughFrame;
    };

    TimelineClock* m_clock;
    PatternManager* m_patternManager;
    UnitManager* m_unitManager;

    // Active instances (scheduler thread only - LOCK REQUIRED if called from RT)
    std::vector<PatternInstance> m_activeInstances;
    mutable std::mutex m_mutex;

    // RT event queue
    LockFreeSPSCQueue<ScheduledEvent, 8192> m_rtQueue;

    // Cancellation flags (atomic, max 256 instances)
    std::array<std::atomic<bool>, 256> m_instanceCancelled;

    // Diagnostics (atomic counters)
    std::atomic<uint32_t> m_overflowCounter;
    std::atomic<uint32_t> m_processedCounter;
    uint64_t m_lastRefillFrame{0}; // Detect loop wraps

    // RT-safe flush: audio thread sets this flag, non-RT maintenance drains the queue.
    std::atomic<bool> m_flushRequested{false};

    // RT-safe content-edit notification: set by editors (Arsenal grid, Piano Roll)
    // when pattern notes change during playback. refillWindow pulls each instance's
    // scheduling frontier back to the playhead and re-queues from live data, without
    // arming the entry catch-up.
    std::atomic<bool> m_contentEditRequested{false};

    // Pre-allocated scratch buffer for refillWindow (reserved at init, never reallocates)
    std::vector<ScheduledEvent> m_scratchEvents;

    // Helpers
    uint16_t getChannelForUnit(UnitID unitId) const;

    /**
     * @brief A note currently within its gate, as recorded when its ON was scheduled.
     *
     * Control-thread-only bookkeeping: lets patternContentEdited() dispatch
     * note-offs for notes that were deleted while still sounding (#823 review
     * round 1) — deleted notes vanish from PatternSource, so the refill loop
     * can no longer see them.
     */
    struct GatedNote {
        uint32_t instanceId;
        UnitID unitId;
        uint8_t noteNumber;
        uint16_t channelIdx;
        uint64_t offFrame;
    };

    // Gated-note registry (control thread only; refilled/pruned in refillWindow).
    std::vector<GatedNote> m_gatedNotes;
};

} // namespace Audio
} // namespace Aestra
