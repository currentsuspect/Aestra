// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file RealtimeThreadGuard.h
 * @brief The single source of truth for real-time thread state and misuse
 *        reporting (B-004 threading model, B-005 constraints).
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * AESTRA THREADING MODEL (B-004)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * 1. MAIN THREAD (UI)
 *    - Runs the event loop and renders UI; handles input and window events.
 *    - Safe for: allocations, locks, file I/O, logging.
 *    - Entry point: main() -> AestraApp::run()
 *
 * 2. AUDIO THREAD (real-time)
 *    - Callback from the OS audio subsystem (WASAPI/ASIO/ALSA).
 *    - Highest priority, time-critical.
 *    - FORBIDDEN: allocations, locks, file I/O, exceptions.
 *    - Entry point: AudioDeviceManager callback -> AudioEngine::process()
 *
 * 3. WORKER THREADS (background)
 *    - Async tasks: autosave, waveform decoding, plugin scanning.
 *    - Lower priority than audio. Safe for: allocations, locks, file I/O.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * AUDIO THREAD CONSTRAINTS (B-005)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The audio callback MUST complete within the buffer period (e.g. 5.3ms at
 * 48kHz with 256 samples). Violating constraints causes audible glitches.
 *
 * FORBIDDEN in the audio callback:
 *    - Memory allocation (new, malloc, vector resize, string concat)
 *    - Mutex locks (std::mutex, std::lock_guard, critical sections)
 *    - File I/O (fopen, fread, std::ifstream)
 *    - Blocking system calls (sleep, wait, network)
 *    - Logging (may allocate or lock)
 *    - Throwing exceptions
 *    - Virtual calls through unknown code paths
 *
 * ALLOWED in the audio callback:
 *    - Atomic operations, lock-free queues (SPSC commands, MPSC events)
 *    - Pre-allocated buffers and pools
 *    - Simple math and DSP; reads from pre-loaded audio buffers
 *
 * COMMUNICATION PATTERNS:
 *    UI -> Audio: lock-free command queue (AudioCommandQueue)
 *    Audio -> UI: lock-free event queue or atomic flags
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * ENFORCEMENT — ONE FLAG, ONE REPORTING CALL (R1 / T-2)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Real-time state is `g_realtimeAudioThreadDepth`, marked exclusively with
 * ScopedRealtimeAudioThread and read with isRealtimeAudioThread(). It is a
 * depth counter, not a bool, because the device callback guard and
 * AudioEngine::processBlock's inner guard nest.
 *
 * A non-real-time API reached from a real-time thread reports through
 * reportRealtimeMisuse(apiName) — the one reporting call. Callers use its
 * return value to refuse the operation:
 *
 *     void MixerChannel::setMute(bool m) {
 *         if (reportRealtimeMisuse("MixerChannel::setMute")) return;
 *         ...
 *     }
 *
 * This header previously had two companions carrying parallel machinery:
 * Source/AudioThreadConstraints.h (a second g_isAudioThread flag, then an
 * AudioThreadStats counter block and the AESTRA_TRACK_ and AESTRA_ASSERT_
 * macro families) and the forensic buffer in Core/RTGuard.h. The second flag was removed in
 * R1 (#553); the counter block and macros were removed with T-2 (#257) once
 * the audit showed nothing in production read them. Do not reintroduce a
 * parallel flag or a parallel counter: a detection surface that some call
 * sites consult and others do not is worse than no detection, because it
 * reports "clean" for thread state it never observed.
 */

#include <atomic>
#include <cassert>

namespace Aestra {
namespace Audio {

using RealtimeMisuseHandler = void (*)(const char* apiName) noexcept;

inline thread_local int g_realtimeAudioThreadDepth = 0;
inline std::atomic<RealtimeMisuseHandler> g_realtimeMisuseHandler{nullptr};

inline bool isRealtimeAudioThread() noexcept {
    return g_realtimeAudioThreadDepth > 0;
}

inline RealtimeMisuseHandler setRealtimeMisuseHandler(RealtimeMisuseHandler handler) noexcept {
    return g_realtimeMisuseHandler.exchange(handler, std::memory_order_acq_rel);
}

inline bool reportRealtimeMisuse(const char* apiName) noexcept {
    if (!isRealtimeAudioThread()) {
        return false;
    }

    if (auto handler = g_realtimeMisuseHandler.load(std::memory_order_acquire)) {
        handler(apiName);
    } else {
#ifndef NDEBUG
        assert(false && "Non-real-time API called from the audio thread");
#endif
    }

    return true;
}

class ScopedRealtimeAudioThread {
public:
    ScopedRealtimeAudioThread() noexcept { ++g_realtimeAudioThreadDepth; }
    ~ScopedRealtimeAudioThread() noexcept { --g_realtimeAudioThreadDepth; }

    ScopedRealtimeAudioThread(const ScopedRealtimeAudioThread&) = delete;
    ScopedRealtimeAudioThread& operator=(const ScopedRealtimeAudioThread&) = delete;
};

} // namespace Audio
} // namespace Aestra
