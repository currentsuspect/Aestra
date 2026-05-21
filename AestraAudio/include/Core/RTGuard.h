// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "RealtimeThreadGuard.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Aestra {
namespace Audio {

/**
 * Violation types for RT safety auditing.
 * 'N' = heap allocation (new/malloc)
 * 'D' = heap deallocation (delete/free)
 * 'A' = mutex acquisition attempt
 * 'B' = blocking I/O or system call
 */
enum class RTViolationType : char {
    Allocation = 'N',
    Deallocation = 'D',
    MutexAcquire = 'A',
    BlockingCall = 'B'
};

/**
 * A single RT violation event with forensic diagnostic epoch info.
 * Captures the return address, allocation size, thread ID,
 * and the engine state at the time of violation.
 */
struct RTViolationEvent {
    uint64_t timestamp{0};
    void* returnAddress{nullptr};
    size_t allocationSize{0};
    uint64_t threadId{0};
    uint64_t callbackId{0};
    uint64_t graphGeneration{0};
    RTViolationType type{RTViolationType::Allocation};
};

/// Maximum number of violation events stored per thread (circular buffer)
constexpr size_t MAX_LOCAL_VIOLATIONS = 128;

/**
 * Thread-local RT audit data.
 * Each audio thread gets its own circular buffer of violation events.
 * All operations are strictly thread-local: zero allocations,
 * zero reentrancy, zero lock contention.
 */
struct ThreadLocalRTAudit {
    uint64_t violationCount{0};
    uint64_t droppedCount{0};
    size_t eventIndex{0};
    RTViolationEvent lastEvents[MAX_LOCAL_VIOLATIONS];
};

/// Thread-local audit data. Always available for testing/diagnostics.
/// The allocation override that populates it is gated behind AESTRA_AUDIT_MODE.
extern thread_local ThreadLocalRTAudit g_rtAuditData;

// Reuse existing RT thread guard from RealtimeThreadGuard.h
// ScopedRealtimeAudioThread and isInRealtimeAudioThread are defined there

/**
 * Record an RT violation event.
 * Called from the allocation override or mutex guard.
 * Thread-safe: all operations are thread-local.
 */
inline void recordRTViolation(RTViolationType type, void* returnAddress = nullptr,
                               size_t allocSize = 0) noexcept {
    auto& audit = g_rtAuditData;
    auto& event = audit.lastEvents[audit.eventIndex % MAX_LOCAL_VIOLATIONS];
    event.type = type;
    event.returnAddress = returnAddress;
    event.allocationSize = allocSize;
    // Timestamp, threadId, callbackId, graphGeneration filled by caller if available
    audit.eventIndex++;
    audit.violationCount++;
    if (audit.eventIndex > MAX_LOCAL_VIOLATIONS) {
        audit.droppedCount++;
    }
}

/**
 * Get the total RT violation count for the current thread.
 * Thread-safe: thread-local read.
 */
inline uint64_t getRTViolationCount() noexcept { return g_rtAuditData.violationCount; }

/**
 * Get the dropped event count for the current thread.
 * Thread-safe: thread-local read.
 */
inline uint64_t getRTDroppedCount() noexcept { return g_rtAuditData.droppedCount; }

} // namespace Audio
} // namespace Aestra
