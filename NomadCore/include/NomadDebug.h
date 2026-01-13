// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadDebug.h
 * @brief Central debug flag definitions for NOMAD DAW
 * 
 * Use these flags to guard debug-only code that should NOT compile in Release builds.
 * This prevents debug test signals from accidentally shipping to production.
 * 
 * Usage:
 * @code
 * #if NOMAD_DEBUG_METERS
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
    #define NOMAD_DEBUG_ENABLED 1
#else
    #define NOMAD_DEBUG_ENABLED 0
#endif

// =============================================================================
// Category-Specific Debug Flags
// =============================================================================
// All require NOMAD_DEBUG_ENABLED to be on. Set individual flags to 0 to disable
// specific debug features even in Debug builds.

#if NOMAD_DEBUG_ENABLED
    /// Enable fake meter signals (sweeps, test values)
    #define NOMAD_DEBUG_METERS     0   // OFF by default - enable when needed
    
    /// Enable audio pipeline debug signals (test tones, etc.)
    #define NOMAD_DEBUG_AUDIO      0   // OFF by default - enable when needed
    
    /// Enable pointer validation logging at startup
    #define NOMAD_DEBUG_POINTERS   1   // ON by default - catches wiring bugs
    
    /// Enable verbose logging of real-time thread activity
    #define NOMAD_DEBUG_RT_VERBOSE 0   // OFF by default - very noisy
#else
    #define NOMAD_DEBUG_METERS     0
    #define NOMAD_DEBUG_AUDIO      0
    #define NOMAD_DEBUG_POINTERS   0
    #define NOMAD_DEBUG_RT_VERBOSE 0
#endif

// =============================================================================
// Helper Macros
// =============================================================================

/// Execute code only in debug builds
#if NOMAD_DEBUG_ENABLED
    #define NOMAD_DEBUG_ONLY(code) code
#else
    #define NOMAD_DEBUG_ONLY(code)
#endif

/// Log only if pointer debug is enabled
#if NOMAD_DEBUG_POINTERS
    #define NOMAD_PTR_LOG(msg) Nomad::Log::info(msg)
#else
    #define NOMAD_PTR_LOG(msg)
#endif
