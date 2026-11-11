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
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * COMPILE-TIME ENFORCEMENT — AESTRA_RT_NONBLOCKING (B-005, T-2 second half)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Everything above is runtime detection: it tells you a violation happened,
 * after it happened, on a machine that was running the code. The other half of
 * B-005 is refusing to compile the violation in the first place.
 *
 * Mark a function that runs on the audio thread:
 *
 *     void MyUnit::process(float* out, uint32_t frames) AESTRA_RT_NONBLOCKING;
 *
 * Under Clang this expands to [[clang::nonblocking]] and the compiler walks the
 * call graph: allocation, deallocation, locks, throws, atomic waits and calls to
 * functions it cannot prove non-blocking all become diagnostics. Under any other
 * compiler it expands to nothing, so the annotation is free to apply everywhere
 * and the gate is simply not enforced there.
 *
 * The attribute belongs on the *declaration* — a caller in another translation
 * unit only sees the header, and an unannotated declaration is treated as
 * possibly-blocking. Annotating only the definition therefore checks the body
 * but tells callers nothing.
 *
 * This is opt-in per function, which makes it adoptable incrementally: an
 * unannotated function is never checked, so turning the warning on repo-wide
 * cannot break code nobody has annotated yet. Coverage is exactly the set of
 * functions carrying the macro, and that set is the contract.
 *
 * Enable the check with -DAESTRA_RT_EFFECT_CHECK=ON (Clang only). CI runs it
 * through scripts/ci/check-rt-effects.sh.
 *
 * ── The two waivers in this header ──
 *
 * Clang refuses thread-local access inside a nonblocking function, because in
 * the general-dynamic TLS model the first access on a thread goes through
 * __tls_get_addr, which may allocate. That rule is a blanket one — Clang does
 * not look at the TLS model when applying it.
 *
 * g_realtimeAudioThreadDepth is therefore pinned to the initial-exec model,
 * which resolves to a fixed offset from the thread pointer with no call and no
 * lazy allocation, and it is a constant-initialized int, so there is no guard
 * variable either. That makes the access genuinely non-blocking, and only then
 * is the diagnostic waived for the three functions that touch it.
 *
 * initial-exec is valid because every Aestra library is STATIC and linked into
 * the executable. If a target ever becomes SHARED and is dlopen'd, initial-exec
 * can fail at load time with "cannot allocate memory in static TLS block" — at
 * that point drop the model pin and the waiver together, because the waiver's
 * justification goes with it.
 *
 * reportRealtimeMisuse carries the second waiver: it dispatches through a
 * function pointer, which Clang cannot follow. The obligation moves to the
 * handler — a RealtimeMisuseHandler must itself be non-blocking. AudioEngine's
 * handler only bumps atomics.
 *
 * The same waiver also covers that function's debug-only assert, which reaches
 * __assert_fail. That is deliberate and not a hole: it fires only in a debug
 * build with no handler installed, where the program is already in a state it
 * was built to abort on. Nothing about it is meant to survive to a release
 * build, and NDEBUG removes it there.
 */

#include <atomic>
#include <cassert>

/**
 * @def AESTRA_RT_NONBLOCKING
 * @brief Declares that a function must be safe to call on the audio thread.
 *
 * Goes after the parameter list (it is part of the function *type*, not a
 * declaration attribute), so it sits alongside noexcept:
 *
 *     bool tryPop(Command& out) noexcept AESTRA_RT_NONBLOCKING;
 */
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonblocking)
#    define AESTRA_RT_NONBLOCKING [[clang::nonblocking]]
#  endif
#endif
#ifndef AESTRA_RT_NONBLOCKING
#  define AESTRA_RT_NONBLOCKING
#endif

/**
 * @def AESTRA_RT_TLS_MODEL
 * @brief Pins RT thread-state thread-locals to initial-exec. See the waiver
 *        discussion above — the compile-time waiver is only honest with this.
 */
#if defined(__GNUC__) || defined(__clang__)
#  define AESTRA_RT_TLS_MODEL __attribute__((tls_model("initial-exec")))
#else
#  define AESTRA_RT_TLS_MODEL
#endif

namespace Aestra {
namespace Audio {

using RealtimeMisuseHandler = void (*)(const char* apiName) noexcept;

AESTRA_RT_TLS_MODEL inline thread_local int g_realtimeAudioThreadDepth = 0;
inline std::atomic<RealtimeMisuseHandler> g_realtimeMisuseHandler{nullptr};

// Who owns the real-time thread right now (#885). Dispatch above stays
// process-wide (one handler pointer), but the handler needs to know which
// engine's audio thread it was called from so it can account the violation to
// that engine. The owner publishes an opaque pointer for the duration of its
// real-time scope; the handler reads it. Same initial-exec TLS as the depth
// counter, so reading it on the audio thread never allocates.
AESTRA_RT_TLS_MODEL inline thread_local void* g_realtimeMisuseContext = nullptr;

// Installed once at startup from the main thread, never from the callback, so
// it stays outside both the annotation and the waiver below.
inline RealtimeMisuseHandler setRealtimeMisuseHandler(RealtimeMisuseHandler handler) noexcept {
    return g_realtimeMisuseHandler.exchange(handler, std::memory_order_acq_rel);
}

// ── Waiver region ──
// Everything from here to the matching pop touches g_realtimeAudioThreadDepth
// or dispatches through the handler pointer, the two things Clang's effect
// analysis cannot clear on its own. The justification is the initial-exec pin
// and the handler contract, both argued at the top of this file. Nothing else
// belongs in here: an unrelated function parked inside this region would have
// its diagnostics silenced by an argument that does not cover it.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wfunction-effects"
#endif

inline bool isRealtimeAudioThread() noexcept AESTRA_RT_NONBLOCKING {
    return g_realtimeAudioThreadDepth > 0;
}

inline bool reportRealtimeMisuse(const char* apiName) noexcept AESTRA_RT_NONBLOCKING {
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
    ScopedRealtimeAudioThread() noexcept AESTRA_RT_NONBLOCKING { ++g_realtimeAudioThreadDepth; }
    ~ScopedRealtimeAudioThread() noexcept AESTRA_RT_NONBLOCKING { --g_realtimeAudioThreadDepth; }

    ScopedRealtimeAudioThread(const ScopedRealtimeAudioThread&) = delete;
    ScopedRealtimeAudioThread& operator=(const ScopedRealtimeAudioThread&) = delete;
};

/// Marks `owner` as the accountable owner of misuse reported on this thread
/// while in scope. Nests: the previous owner is restored on exit. Pair it with
/// ScopedRealtimeAudioThread; it does not mark the thread real-time itself.
class ScopedRealtimeMisuseContext {
public:
    explicit ScopedRealtimeMisuseContext(void* owner) noexcept AESTRA_RT_NONBLOCKING
        : m_previous(g_realtimeMisuseContext) {
        g_realtimeMisuseContext = owner;
    }
    ~ScopedRealtimeMisuseContext() noexcept AESTRA_RT_NONBLOCKING { g_realtimeMisuseContext = m_previous; }

    ScopedRealtimeMisuseContext(const ScopedRealtimeMisuseContext&) = delete;
    ScopedRealtimeMisuseContext& operator=(const ScopedRealtimeMisuseContext&) = delete;

private:
    void* m_previous;
};

#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

} // namespace Audio
} // namespace Aestra
