/**
 * @file assert.hpp  
 * @brief Assertion utilities - Migration wrapper
 * 
 * Wraps existing NomadAssert.h and adds new capabilities.
 */

#pragma once

// Include the existing implementation
#include "NomadCore/include/NomadAssert.h"

namespace nomad {

//=============================================================================
// Re-export existing macros work as-is (NOMAD_ASSERT, etc.)
// Add new namespace-aware utilities here
//=============================================================================

/// Check if we're in debug build
#ifdef NOMAD_DEBUG
    constexpr bool isDebugBuild = true;
#else
    constexpr bool isDebugBuild = false;
#endif

} // namespace nomad

//=============================================================================
// Extended assertion macros for new code
//=============================================================================

// Portable unreachable marker
#if defined(_MSC_VER)
    #define NOMAD_UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
    #define NOMAD_UNREACHABLE() __builtin_unreachable()
#else
    #define NOMAD_UNREACHABLE() ((void)0)
#endif

// Debug-only unreachable (asserts in debug, hints in release)
#ifdef NOMAD_DEBUG
    #define NOMAD_UNREACHABLE_DEBUG() NOMAD_ASSERT(false && "Unreachable code")
#else
    #define NOMAD_UNREACHABLE_DEBUG() NOMAD_UNREACHABLE()
#endif
