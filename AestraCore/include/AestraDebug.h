// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file AestraDebug.h
 * @brief Central debug flag definitions for Aestra
 * 
 * Use these flags to guard debug-only code that should NOT compile in Release builds.
 * This prevents debug test signals from accidentally shipping to production.
 * 
 * Usage:
 * @code
 * #if AESTRA_DEBUG_METERS
 *     // This only compiles in Debug builds
 *     correlation = fakeSweepValue;
 * #endif
 * @endcode
 */
#pragma once

// =============================================================================
// Master Debug Switch
// =============================================================================
// Automatically enabled in Debug builds, disabled in Release
#ifdef _DEBUG
    #define AESTRA_DEBUG_ENABLED 1
#else
    #define AESTRA_DEBUG_ENABLED 0
#endif

// =============================================================================
// Category-Specific Debug Flags
// =============================================================================
// All require AESTRA_DEBUG_ENABLED to be on. Set individual flags to 0 to disable
// specific debug features even in Debug builds.

#if AESTRA_DEBUG_ENABLED
    /// Enable fake meter signals (sweeps, test values)
    #define AESTRA_DEBUG_METERS     0   // OFF by default - enable when needed
    
    /// Enable audio pipeline debug signals (test tones, etc.)
    #define AESTRA_DEBUG_AUDIO      0   // OFF by default - enable when needed
    
    /// Enable pointer validation logging at startup
    #define AESTRA_DEBUG_POINTERS   1   // ON by default - catches wiring bugs
    
    /// Enable verbose logging of real-time thread activity
    #define AESTRA_DEBUG_RT_VERBOSE 0   // OFF by default - very noisy
#else
    #define AESTRA_DEBUG_METERS     0
    #define AESTRA_DEBUG_AUDIO      0
    #define AESTRA_DEBUG_POINTERS   0
    #define AESTRA_DEBUG_RT_VERBOSE 0
#endif

// =============================================================================
// Helper Macros
// =============================================================================

/// Execute code only in debug builds
#if AESTRA_DEBUG_ENABLED
    #define AESTRA_DEBUG_ONLY(code) code
#else
    #define AESTRA_DEBUG_ONLY(code)
#endif

/// Log only if pointer debug is enabled
#if AESTRA_DEBUG_POINTERS
    #define AESTRA_PTR_LOG(msg) Aestra::Log::info(msg)
#else
    #define AESTRA_PTR_LOG(msg)
#endif
