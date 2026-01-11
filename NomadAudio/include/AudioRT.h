// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #if defined(_MSC_VER)
        #include <intrin.h>
        #include <xmmintrin.h>
    #else
        #include <x86intrin.h>
    #endif
#endif

namespace Nomad {
namespace Audio {
namespace RT {

// Enable flush-to-zero (FTZ) + denormals-are-zero (DAZ) on x86/x64.
// Call once per audio thread (cheap, RT-safe).
inline void enableDenormalProtection() noexcept {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    // MXCSR bits: FTZ (bit 15), DAZ (bit 6).
    constexpr unsigned int kFTZ = 1u << 15;
    constexpr unsigned int kDAZ = 1u << 6;

    unsigned int csr = _mm_getcsr();
    csr |= (kFTZ | kDAZ);
    _mm_setcsr(csr);
#endif
}

inline void initAudioThread() noexcept {
    static thread_local bool initialized = false;
    if (!initialized) {
        enableDenormalProtection();
        initialized = true;
    }
}

// Read a fast cycle counter for callback timing (x86/x64 only).
inline uint64_t readCycleCounter() noexcept {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    return static_cast<uint64_t>(__rdtsc());
#else
    return 0;
#endif
}

} // namespace RT
} // namespace Audio
} // namespace Nomad
