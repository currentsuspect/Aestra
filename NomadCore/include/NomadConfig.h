// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// =============================================================================
// NOMAD Configuration
// =============================================================================

namespace Nomad {

// =============================================================================
// Build Configuration
// =============================================================================

// Build type detection
#if defined(_DEBUG) || defined(DEBUG)
    #define NOMAD_DEBUG 1
    #define NOMAD_RELEASE 0
#else
    #define NOMAD_DEBUG 0
    #define NOMAD_RELEASE 1
#endif

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
    #define NOMAD_PLATFORM_WINDOWS 1
    #define NOMAD_PLATFORM_LINUX 0
    #define NOMAD_PLATFORM_MACOS 0
#elif defined(__linux__)
    #define NOMAD_PLATFORM_WINDOWS 0
    #define NOMAD_PLATFORM_LINUX 1
    #define NOMAD_PLATFORM_MACOS 0
#elif defined(__APPLE__)
    #define NOMAD_PLATFORM_WINDOWS 0
    #define NOMAD_PLATFORM_LINUX 0
    #define NOMAD_PLATFORM_MACOS 1
#else
    #error "Unsupported platform"
#endif

// Compiler detection
#if defined(_MSC_VER)
    #define NOMAD_COMPILER_MSVC 1
    #define NOMAD_COMPILER_GCC 0
    #define NOMAD_COMPILER_CLANG 0
#elif defined(__clang__)
    #define NOMAD_COMPILER_MSVC 0
    #define NOMAD_COMPILER_GCC 0
    #define NOMAD_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define NOMAD_COMPILER_MSVC 0
    #define NOMAD_COMPILER_GCC 1
    #define NOMAD_COMPILER_CLANG 0
#endif

// Architecture detection
#if defined(_M_X64) || defined(__x86_64__)
    #define NOMAD_ARCH_X64 1
    #define NOMAD_ARCH_X86 0
    #define NOMAD_ARCH_ARM 0
#elif defined(_M_IX86) || defined(__i386__)
    #define NOMAD_ARCH_X64 0
    #define NOMAD_ARCH_X86 1
    #define NOMAD_ARCH_ARM 0
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define NOMAD_ARCH_X64 0
    #define NOMAD_ARCH_X86 0
    #define NOMAD_ARCH_ARM 1
#endif

// =============================================================================
// Feature Toggles
// =============================================================================

// Enable assertions (can be overridden)
#ifndef NOMAD_ENABLE_ASSERTS
    #if NOMAD_DEBUG
        #define NOMAD_ENABLE_ASSERTS 1
    #else
        #define NOMAD_ENABLE_ASSERTS 0
    #endif
#endif

// Enable logging (can be overridden)
#ifndef NOMAD_ENABLE_LOGGING
    #define NOMAD_ENABLE_LOGGING 1
#endif

// Enable profiling (can be overridden)
#ifndef NOMAD_ENABLE_PROFILING
    #define NOMAD_ENABLE_PROFILING 0
#endif

// =============================================================================
// SIMD Configuration
// =============================================================================

// SIMD support detection
#if NOMAD_ARCH_X64 || NOMAD_ARCH_X86
    #if defined(__AVX2__)
        #define NOMAD_SIMD_AVX2 1
        #define NOMAD_SIMD_AVX 1
        #define NOMAD_SIMD_SSE4 1
        #define NOMAD_SIMD_SSE2 1
    #elif defined(__AVX__)
        #define NOMAD_SIMD_AVX2 0
        #define NOMAD_SIMD_AVX 1
        #define NOMAD_SIMD_SSE4 1
        #define NOMAD_SIMD_SSE2 1
    #elif defined(__SSE4_1__)
        #define NOMAD_SIMD_AVX2 0
        #define NOMAD_SIMD_AVX 0
        #define NOMAD_SIMD_SSE4 1
        #define NOMAD_SIMD_SSE2 1
    #elif defined(__SSE2__) || NOMAD_ARCH_X64
        #define NOMAD_SIMD_AVX2 0
        #define NOMAD_SIMD_AVX 0
        #define NOMAD_SIMD_SSE4 0
        #define NOMAD_SIMD_SSE2 1
    #else
        #define NOMAD_SIMD_AVX2 0
        #define NOMAD_SIMD_AVX 0
        #define NOMAD_SIMD_SSE4 0
        #define NOMAD_SIMD_SSE2 0
    #endif
#elif NOMAD_ARCH_ARM
    #if defined(__ARM_NEON)
        #define NOMAD_SIMD_NEON 1
    #else
        #define NOMAD_SIMD_NEON 0
    #endif
#endif

// =============================================================================
// Audio Configuration
// =============================================================================

namespace Config {

// Default audio settings
constexpr int DEFAULT_SAMPLE_RATE = 48000;
constexpr int DEFAULT_BUFFER_SIZE = 512;
constexpr int DEFAULT_NUM_CHANNELS = 2;

// Real-time constraints
constexpr int MAX_AUDIO_LATENCY_MS = 10;
constexpr int AUDIO_THREAD_PRIORITY = 99; // Platform-specific

// DSP settings
constexpr float DENORMAL_THRESHOLD = 1e-15f;
constexpr float SILENCE_THRESHOLD = -96.0f; // dB

} // namespace Config

// =============================================================================
// Compiler Attributes
// =============================================================================

// Force inline
#if NOMAD_COMPILER_MSVC
    #define NOMAD_FORCE_INLINE __forceinline
#elif NOMAD_COMPILER_GCC || NOMAD_COMPILER_CLANG
    #define NOMAD_FORCE_INLINE inline __attribute__((always_inline))
#else
    #define NOMAD_FORCE_INLINE inline
#endif

// No inline
#if NOMAD_COMPILER_MSVC
    #define NOMAD_NO_INLINE __declspec(noinline)
#elif NOMAD_COMPILER_GCC || NOMAD_COMPILER_CLANG
    #define NOMAD_NO_INLINE __attribute__((noinline))
#else
    #define NOMAD_NO_INLINE
#endif

// Likely/Unlikely branch hints
#if NOMAD_COMPILER_GCC || NOMAD_COMPILER_CLANG
    #define NOMAD_LIKELY(x) __builtin_expect(!!(x), 1)
    #define NOMAD_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define NOMAD_LIKELY(x) (x)
    #define NOMAD_UNLIKELY(x) (x)
#endif

// Unreachable code hint
#if NOMAD_COMPILER_MSVC
    #define NOMAD_UNREACHABLE() __assume(0)
#elif NOMAD_COMPILER_GCC || NOMAD_COMPILER_CLANG
    #define NOMAD_UNREACHABLE() __builtin_unreachable()
#else
    #define NOMAD_UNREACHABLE()
#endif

// =============================================================================
// Utility Macros
// =============================================================================

// Stringify
#define NOMAD_STRINGIFY_IMPL(x) #x
#define NOMAD_STRINGIFY(x) NOMAD_STRINGIFY_IMPL(x)

// Concatenate
#define NOMAD_CONCAT_IMPL(x, y) x##y
#define NOMAD_CONCAT(x, y) NOMAD_CONCAT_IMPL(x, y)

// Unused variable
#define NOMAD_UNUSED(x) (void)(x)

// Array size
#define NOMAD_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// =============================================================================
// Version Information
// =============================================================================

#define NOMAD_VERSION_MAJOR 0
#define NOMAD_VERSION_MINOR 1
#define NOMAD_VERSION_PATCH 0
#define NOMAD_VERSION_STRING "0.1.0"

} // namespace Nomad
