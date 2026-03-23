// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// =============================================================================
// AESTRA Configuration
// =============================================================================

/**
 * @file AestraConfig.h
 * @brief Global configuration, platform detection, and compiler macros.
 */

namespace Aestra {

// =============================================================================
// Build Configuration
// =============================================================================

// Build type detection
#if defined(_DEBUG) || defined(DEBUG)
#define AESTRA_DEBUG 1
#define AESTRA_RELEASE 0
#else
#define AESTRA_DEBUG 0
#define AESTRA_RELEASE 1
#endif

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#define AESTRA_PLATFORM_WINDOWS 1
#define AESTRA_PLATFORM_LINUX 0
#define AESTRA_PLATFORM_MACOS 0
#elif defined(__linux__)
#define AESTRA_PLATFORM_WINDOWS 0
#define AESTRA_PLATFORM_LINUX 1
#define AESTRA_PLATFORM_MACOS 0
#elif defined(__APPLE__)
#define AESTRA_PLATFORM_WINDOWS 0
#define AESTRA_PLATFORM_LINUX 0
#define AESTRA_PLATFORM_MACOS 1
#else
#error "Unsupported platform"
#endif

// Compiler detection
#if defined(_MSC_VER)
#define AESTRA_COMPILER_MSVC 1
#define AESTRA_COMPILER_GCC 0
#define AESTRA_COMPILER_CLANG 0
#elif defined(__clang__)
#define AESTRA_COMPILER_MSVC 0
#define AESTRA_COMPILER_GCC 0
#define AESTRA_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define AESTRA_COMPILER_MSVC 0
#define AESTRA_COMPILER_GCC 1
#define AESTRA_COMPILER_CLANG 0
#endif

// Architecture detection
#if defined(_M_X64) || defined(__x86_64__)
#define AESTRA_ARCH_X64 1
#define AESTRA_ARCH_X86 0
#define AESTRA_ARCH_ARM 0
#elif defined(_M_IX86) || defined(__i386__)
#define AESTRA_ARCH_X64 0
#define AESTRA_ARCH_X86 1
#define AESTRA_ARCH_ARM 0
#elif defined(_M_ARM64) || defined(__aarch64__)
#define AESTRA_ARCH_X64 0
#define AESTRA_ARCH_X86 0
#define AESTRA_ARCH_ARM 1
#endif

// =============================================================================
// Feature Toggles
// =============================================================================

// Enable assertions (can be overridden)
#ifndef AESTRA_ENABLE_ASSERTS
#if AESTRA_DEBUG
#define AESTRA_ENABLE_ASSERTS 1
#else
#define AESTRA_ENABLE_ASSERTS 0
#endif
#endif

// Enable logging (can be overridden)
#ifndef AESTRA_ENABLE_LOGGING
#define AESTRA_ENABLE_LOGGING 1
#endif

// Enable profiling (can be overridden)
#ifndef AESTRA_ENABLE_PROFILING
#define AESTRA_ENABLE_PROFILING 0
#endif

// =============================================================================
// SIMD Configuration
// =============================================================================

// SIMD support detection
#if AESTRA_ARCH_X64 || AESTRA_ARCH_X86
#if defined(__AVX2__)
#define AESTRA_SIMD_AVX2 1
#define AESTRA_SIMD_AVX 1
#define AESTRA_SIMD_SSE4 1
#define AESTRA_SIMD_SSE2 1
#elif defined(__AVX__)
#define AESTRA_SIMD_AVX2 0
#define AESTRA_SIMD_AVX 1
#define AESTRA_SIMD_SSE4 1
#define AESTRA_SIMD_SSE2 1
#elif defined(__SSE4_1__)
#define AESTRA_SIMD_AVX2 0
#define AESTRA_SIMD_AVX 0
#define AESTRA_SIMD_SSE4 1
#define AESTRA_SIMD_SSE2 1
#elif defined(__SSE2__) || AESTRA_ARCH_X64
#define AESTRA_SIMD_AVX2 0
#define AESTRA_SIMD_AVX 0
#define AESTRA_SIMD_SSE4 0
#define AESTRA_SIMD_SSE2 1
#else
#define AESTRA_SIMD_AVX2 0
#define AESTRA_SIMD_AVX 0
#define AESTRA_SIMD_SSE4 0
#define AESTRA_SIMD_SSE2 0
#endif
#elif AESTRA_ARCH_ARM
#if defined(__ARM_NEON)
#define AESTRA_SIMD_NEON 1
#else
#define AESTRA_SIMD_NEON 0
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
#if AESTRA_COMPILER_MSVC
#define AESTRA_FORCE_INLINE __forceinline
#elif AESTRA_COMPILER_GCC || AESTRA_COMPILER_CLANG
#define AESTRA_FORCE_INLINE inline __attribute__((always_inline))
#else
#define AESTRA_FORCE_INLINE inline
#endif

// No inline
#if AESTRA_COMPILER_MSVC
#define AESTRA_NO_INLINE __declspec(noinline)
#elif AESTRA_COMPILER_GCC || AESTRA_COMPILER_CLANG
#define AESTRA_NO_INLINE __attribute__((noinline))
#else
#define AESTRA_NO_INLINE
#endif

// Likely/Unlikely branch hints
#if AESTRA_COMPILER_GCC || AESTRA_COMPILER_CLANG
#define AESTRA_LIKELY(x) __builtin_expect(!!(x), 1)
#define AESTRA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define AESTRA_LIKELY(x) (x)
#define AESTRA_UNLIKELY(x) (x)
#endif

// Unreachable code hint
#if AESTRA_COMPILER_MSVC
#define AESTRA_UNREACHABLE() __assume(0)
#elif AESTRA_COMPILER_GCC || AESTRA_COMPILER_CLANG
#define AESTRA_UNREACHABLE() __builtin_unreachable()
#else
#define AESTRA_UNREACHABLE()
#endif

// =============================================================================
// Utility Macros
// =============================================================================

// Stringify
#define AESTRA_STRINGIFY_IMPL(x) #x
#define AESTRA_STRINGIFY(x) AESTRA_STRINGIFY_IMPL(x)

// Concatenate
#define AESTRA_CONCAT_IMPL(x, y) x##y
#define AESTRA_CONCAT(x, y) AESTRA_CONCAT_IMPL(x, y)

// Unused variable
#define AESTRA_UNUSED(x) (void)(x)

// Array size
#define AESTRA_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// =============================================================================
// Version Information
// =============================================================================

#define AESTRA_VERSION_MAJOR 0
#define AESTRA_VERSION_MINOR 1
#define AESTRA_VERSION_PATCH 0
#define AESTRA_VERSION_STRING "0.1.0"

} // namespace Aestra
