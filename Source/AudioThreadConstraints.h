// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AudioThreadConstraints.h
 * @brief B-005: Audio thread safety checks and documentation
 *        B-007: Allocation tracking in debug builds
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 * AESTRA THREADING MODEL (B-004)
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * THREAD TYPES:
 * 
 * 1. MAIN THREAD (UI Thread)
 *    - Runs the event loop and renders UI
 *    - Handles user input, window events
 *    - Safe for: allocations, locks, file I/O, logging
 *    - Entry point: main() -> AestraApp::run()
 * 
 * 2. AUDIO THREAD (Real-Time Thread)
 *    - Callback from OS audio subsystem (WASAPI/ASIO)
 *    - Highest priority, time-critical
 *    - FORBIDDEN: allocations, locks, file I/O, exceptions
 *    - Entry point: AudioDeviceManager callback -> AudioEngine::process()
 * 
 * 3. WORKER THREADS (Background)
 *    - Async tasks: autosave, waveform decoding, plugin scanning
 *    - Lower priority than audio
 *    - Safe for: allocations, locks, file I/O
 *    - Managed by: std::async, thread pools
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 * AUDIO THREAD CONSTRAINTS (B-005)
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * The audio callback MUST complete within the buffer period (e.g., 5.3ms at
 * 48kHz with 256 samples). Violating constraints causes audible glitches.
 * 
 * ❌ FORBIDDEN in audio callback:
 *    - Memory allocation (new, malloc, vector resize, string concat)
 *    - Mutex locks (std::mutex, std::lock_guard, critical sections)
 *    - File I/O (fopen, fread, std::ifstream)
 *    - System calls that may block (sleep, wait, network)
 *    - Logging (may allocate or lock)
 *    - Throwing exceptions
 *    - Virtual function calls through unknown code paths
 * 
 * ✅ ALLOWED in audio callback:
 *    - Atomic operations (std::atomic load/store/exchange)
 *    - Lock-free queues (SPSC for commands, MPSC for events)
 *    - Pre-allocated buffers and pools
 *    - Simple math and DSP operations
 *    - Reading from pre-loaded audio buffers
 * 
 * COMMUNICATION PATTERNS:
 *    UI -> Audio: Lock-free command queue (AudioCommandQueue)
 *    Audio -> UI: Lock-free event queue or atomic flags
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 * B-007: ALLOCATION TRACKING (DEBUG BUILDS ONLY)
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * In debug builds, AESTRA_TRACK_ALLOCATION() and AESTRA_TRACK_DEALLOCATION()
 * can be called to monitor memory usage patterns. When called from the audio
 * thread, these log violations and increment counters.
 * 
 * Integration with custom allocators:
 * 1. Wrap your allocator's allocate/deallocate with AESTRA_TRACK_ALLOCATION
 * 2. Use AESTRA_SCOPED_ALLOCATION_TRACKING for temporary tracking
 * 3. Check AudioThreadStats::instance().allocationViolations after testing
 * 
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include <atomic>
#include <cassert>
#include <cstddef>

// Compile-time configuration
#ifndef AESTRA_AUDIO_DEBUG
    #ifdef NDEBUG
        #define AESTRA_AUDIO_DEBUG 0
    #else
        #define AESTRA_AUDIO_DEBUG 1
    #endif
#endif

namespace Aestra {
namespace Audio {

/**
 * @brief Thread-local flag indicating we're on the audio thread
 * 
 * Set to true at the start of the audio callback, false at the end.
 * Used by debug checks to detect constraint violations.
 */
inline thread_local bool g_isAudioThread = false;

/**
 * @brief Check if current thread is the audio thread
 */
inline bool isAudioThread() {
    return g_isAudioThread;
}

/**
 * @brief RAII guard for marking audio thread scope
 */
class AudioThreadGuard {
public:
    AudioThreadGuard() { 
        g_isAudioThread = true; 
    }
    ~AudioThreadGuard() { 
        g_isAudioThread = false; 
    }
    
    // Non-copyable
    AudioThreadGuard(const AudioThreadGuard&) = delete;
    AudioThreadGuard& operator=(const AudioThreadGuard&) = delete;
};

/**
 * @brief Runtime violation counter (for diagnostics)
 */
struct AudioThreadStats {
    std::atomic<uint64_t> allocationViolations{0};
    std::atomic<uint64_t> lockViolations{0};
    std::atomic<uint64_t> ioViolations{0};
    std::atomic<uint64_t> totalCallbacks{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> totalAllocations{0};       // B-007: Total tracked allocations
    std::atomic<uint64_t> totalDeallocations{0};     // B-007: Total tracked deallocations
    std::atomic<size_t>   peakAllocationSize{0};     // B-007: Largest single allocation
    
    void reset() {
        allocationViolations.store(0);
        lockViolations.store(0);
        ioViolations.store(0);
        totalCallbacks.store(0);
        underruns.store(0);
        totalAllocations.store(0);
        totalDeallocations.store(0);
        peakAllocationSize.store(0);
    }
    
    // B-007: Update peak if this allocation is larger
    void updatePeak(size_t size) {
        size_t current = peakAllocationSize.load(std::memory_order_relaxed);
        while (size > current) {
            if (peakAllocationSize.compare_exchange_weak(current, size, 
                std::memory_order_release, std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    static AudioThreadStats& instance() {
        static AudioThreadStats s_instance;
        return s_instance;
    }
};

} // namespace Audio
} // namespace Aestra

// =============================================================================
// Debug Macros (B-005)
// =============================================================================

#if AESTRA_AUDIO_DEBUG

/**
 * @brief Assert that we're NOT on the audio thread
 * 
 * Use before operations that would violate audio thread constraints.
 * Example: AESTRA_ASSERT_NOT_AUDIO_THREAD(); // before new/malloc
 */
#define AESTRA_ASSERT_NOT_AUDIO_THREAD() \
    do { \
        if (Aestra::Audio::isAudioThread()) { \
            Aestra::Audio::AudioThreadStats::instance().allocationViolations.fetch_add(1); \
            assert(false && "Audio thread constraint violation: forbidden operation"); \
        } \
    } while(0)

/**
 * @brief Assert that we ARE on the audio thread
 * 
 * Use to verify code is running in the expected context.
 */
#define AESTRA_ASSERT_AUDIO_THREAD() \
    do { \
        assert(Aestra::Audio::isAudioThread() && "Expected to be on audio thread"); \
    } while(0)

/**
 * @brief Mark a function as audio-thread-safe
 * 
 * Documentation macro - no runtime effect, but signals intent.
 */
#define AESTRA_AUDIO_THREAD_SAFE /* function is safe to call from audio thread */

/**
 * @brief Mark a function as NOT audio-thread-safe
 */
#define AESTRA_NOT_AUDIO_THREAD_SAFE /* DO NOT call from audio thread */

/**
 * @brief B-007: Track an allocation (debug only)
 * 
 * Call this when memory is allocated. In debug builds, logs a violation
 * if called from the audio thread.
 * 
 * @param size Size of allocation in bytes
 * @param source Optional string describing the source (file:line or function)
 */
#define AESTRA_TRACK_ALLOCATION(size) \
    do { \
        auto& stats = Aestra::Audio::AudioThreadStats::instance(); \
        stats.totalAllocations.fetch_add(1, std::memory_order_relaxed); \
        stats.updatePeak(size); \
        if (Aestra::Audio::isAudioThread()) { \
            stats.allocationViolations.fetch_add(1, std::memory_order_relaxed); \
            /* Debug break in MSVC: __debugbreak(); */ \
        } \
    } while(0)

/**
 * @brief B-007: Track a deallocation (debug only)
 */
#define AESTRA_TRACK_DEALLOCATION() \
    do { \
        Aestra::Audio::AudioThreadStats::instance().totalDeallocations.fetch_add(1, std::memory_order_relaxed); \
    } while(0)

/**
 * @brief B-007: Track a lock acquisition attempt (debug only)
 * 
 * Call before acquiring a mutex. Logs violation if on audio thread.
 */
#define AESTRA_TRACK_LOCK() \
    do { \
        if (Aestra::Audio::isAudioThread()) { \
            Aestra::Audio::AudioThreadStats::instance().lockViolations.fetch_add(1, std::memory_order_relaxed); \
        } \
    } while(0)

/**
 * @brief B-007: Track a file I/O operation (debug only)
 */
#define AESTRA_TRACK_FILE_IO() \
    do { \
        if (Aestra::Audio::isAudioThread()) { \
            Aestra::Audio::AudioThreadStats::instance().ioViolations.fetch_add(1, std::memory_order_relaxed); \
        } \
    } while(0)

#else // !AESTRA_AUDIO_DEBUG

#define AESTRA_ASSERT_NOT_AUDIO_THREAD() ((void)0)
#define AESTRA_ASSERT_AUDIO_THREAD() ((void)0)
#define AESTRA_AUDIO_THREAD_SAFE
#define AESTRA_NOT_AUDIO_THREAD_SAFE
#define AESTRA_TRACK_ALLOCATION(size) ((void)0)
#define AESTRA_TRACK_DEALLOCATION() ((void)0)
#define AESTRA_TRACK_LOCK() ((void)0)
#define AESTRA_TRACK_FILE_IO() ((void)0)

#endif // AESTRA_AUDIO_DEBUG

// =============================================================================
// Lock-Free Utilities
// =============================================================================

namespace Aestra {
namespace Audio {

/**
 * @brief Atomic flag for signaling between threads without locks
 * 
 * Example usage:
 *   // UI thread sets flag
 *   flag.set();
 *   
 *   // Audio thread checks and clears
 *   if (flag.checkAndClear()) { ... }
 */
class AtomicFlag {
public:
    void set() { 
        m_flag.store(true, std::memory_order_release); 
    }
    
    void clear() { 
        m_flag.store(false, std::memory_order_release); 
    }
    
    bool isSet() const { 
        return m_flag.load(std::memory_order_acquire); 
    }
    
    bool checkAndClear() {
        return m_flag.exchange(false, std::memory_order_acq_rel);
    }
    
private:
    std::atomic<bool> m_flag{false};
};

/**
 * @brief Atomic value for lock-free parameter changes
 * 
 * Example usage:
 *   // UI thread
 *   volume.set(0.8);
 *   
 *   // Audio thread
 *   float v = volume.get();
 */
template<typename T>
class AtomicValue {
public:
    explicit AtomicValue(T initial = T{}) : m_value(initial) {}
    
    void set(T value) { 
        m_value.store(value, std::memory_order_release); 
    }
    
    T get() const { 
        return m_value.load(std::memory_order_acquire); 
    }
    
    T exchange(T newValue) {
        return m_value.exchange(newValue, std::memory_order_acq_rel);
    }
    
private:
    std::atomic<T> m_value;
};

} // namespace Audio
} // namespace Aestra
