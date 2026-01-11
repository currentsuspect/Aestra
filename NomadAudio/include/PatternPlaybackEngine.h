// © 2025 Nomad Studios — All Rights Reserved.
#pragma once

#include "PatternSource.h"
#include "TimelineClock.h"
#include "UnitManager.h"
#include "PatternManager.h"
#include <cstdint>
#include <atomic>
#include <vector>
#include <array>

namespace Nomad {
namespace Audio {

// Forward declaration
class MixerChannel;

/**
 * @brief Compact, cache-aligned scheduled MIDI event (32 bytes)
 */
struct ScheduledEvent {
    uint64_t sampleFrame;     // 8 bytes
    uint32_t instanceId;      // 4 bytes
    uint16_t channelIdx;      // 2 bytes
    uint8_t statusByte;       // 1 byte (MIDI status)
    uint8_t data1;            // 1 byte (note number)
    uint8_t data2;            // 1 byte (velocity)
    uint8_t priority;         // 1 byte (0=note-off first, 1=note-on second)
    uint8_t _padding[14];     // 14 bytes → total 32 bytes
};
static_assert(sizeof(ScheduledEvent) == 32, "ScheduledEvent must be 32 bytes");

/**
 * @brief Lock-free SPSC (single-producer, single-consumer) ring buffer
 * 
 * Simple bounded queue for RT-safe event transfer
 */
template<typename T, size_t Capacity>
class LockFreeSPSCQueue {
public:
    LockFreeSPSCQueue() : m_head(0), m_tail(0) {}
    
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
    
    bool peek(T& item) const {
        uint32_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        item = m_buffer[tail];
        return true;
    }
    
    void pop() {
        uint32_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store((tail + 1) % Capacity, std::memory_order_release);
    }
    
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
    PatternPlaybackEngine(TimelineClock* clock, PatternManager* patternMgr, UnitManager* unitMgr);
    
    /**
     * Schedule new pattern instance (non-RT thread)
     */
    void schedulePatternInstance(PatternID pid, double startBeat, uint32_t instanceId);
    
    /**
     * Cancel pattern instance via atomic flag (RT-safe)
     */
    void cancelPatternInstance(uint32_t instanceId);
    
    /**
     * Refill lookahead window with events (non-RT thread)
     * Called periodically by scheduler
     */
    void refillWindow(uint64_t currentFrame, int sampleRate, int lookaheadSamples = 4096);
    
    /**
     * Process audio callback (RT-safe, audio thread only)
     */
    void processAudio(uint64_t currentFrame, int bufferSize, MixerChannel* mixerChannels, int numChannels);
    
    // Diagnostics
    uint32_t getOverflowCount() const { return m_overflowCounter.load(std::memory_order_relaxed); }
    uint32_t getProcessedEventCount() const { return m_processedCounter.load(std::memory_order_relaxed); }
    
private:
    // Pattern instance in scheduler
    struct PatternInstance {
        PatternID patternId;
        double startBeat;
        uint32_t instanceId;
        size_t nextEventIdx; // Next note to schedule
    };
    
    TimelineClock* m_clock;
    PatternManager* m_patternManager;
    UnitManager* m_unitManager;
    
    // Active instances (scheduler thread only)
    std::vector<PatternInstance> m_activeInstances;
    
    // RT event queue
    LockFreeSPSCQueue<ScheduledEvent, 8192> m_rtQueue;
    
    // Cancellation flags (atomic, max 256 instances)
    std::array<std::atomic<bool>, 256> m_instanceCancelled;
    
    // Diagnostics (atomic counters)
    std::atomic<uint32_t> m_overflowCounter;
    std::atomic<uint32_t> m_processedCounter;
    
    // Helpers
    uint16_t getChannelForUnit(UnitID unitId) const;
};

} // namespace Audio
} // namespace Nomad
