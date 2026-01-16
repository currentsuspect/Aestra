// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraConfig.h"
#include "AestraLog.h"
#include <sstream>

namespace Aestra {

// =============================================================================
// Assertion System
// =============================================================================

/**
 * @file AestraAssert.h
 * @brief Defines assertion macros and handlers for runtime checks.
 */

#if AESTRA_ENABLE_ASSERTS

// Internal assertion handler
inline void assertHandler(const char* expr, const char* file, int line, const char* msg = nullptr) {
    std::stringstream ss;
    ss << "Assertion failed: " << expr;
    if (msg) {
        ss << " - " << msg;
    }
    ss << "\n  File: " << file << "\n  Line: " << line;
    
    Log::error(ss.str());
    
    // Break into debugger if available
    #if AESTRA_COMPILER_MSVC
        __debugbreak();
    #elif AESTRA_COMPILER_GCC || AESTRA_COMPILER_CLANG
        __builtin_trap();
    #else
        std::abort();
    #endif
}

// Basic assertion
#define AESTRA_ASSERT(expr) \
    do { \
        if (AESTRA_UNLIKELY(!(expr))) { \
            Aestra::assertHandler(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

// Assertion with message
#define AESTRA_ASSERT_MSG(expr, msg) \
    do { \
        if (AESTRA_UNLIKELY(!(expr))) { \
            Aestra::assertHandler(#expr, __FILE__, __LINE__, msg); \
        } \
    } while (0)

// Assertion with formatted message
#define AESTRA_ASSERT_FMT(expr, ...) \
    do { \
        if (AESTRA_UNLIKELY(!(expr))) { \
            std::stringstream ss; \
            ss << __VA_ARGS__; \
            Aestra::assertHandler(#expr, __FILE__, __LINE__, ss.str().c_str()); \
        } \
    } while (0)

#else

// Assertions disabled - compile to nothing
#define AESTRA_ASSERT(expr) ((void)0)
#define AESTRA_ASSERT_MSG(expr, msg) ((void)0)
#define AESTRA_ASSERT_FMT(expr, ...) ((void)0)

#endif // AESTRA_ENABLE_ASSERTS

// =============================================================================
// Static Assertions (always enabled)
// =============================================================================

#define AESTRA_STATIC_ASSERT(expr, msg) static_assert(expr, msg)

// =============================================================================
// Verification (always enabled, even in release)
// =============================================================================

// Verify - like assert but always enabled
inline void verifyHandler(const char* expr, const char* file, int line, const char* msg = nullptr) {
    std::stringstream ss;
    ss << "Verification failed: " << expr;
    if (msg) {
        ss << " - " << msg;
    }
    ss << "\n  File: " << file << "\n  Line: " << line;
    
    Log::error(ss.str());
    std::abort();
}

#define AESTRA_VERIFY(expr) \
    do { \
        if (AESTRA_UNLIKELY(!(expr))) { \
            Aestra::verifyHandler(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

#define AESTRA_VERIFY_MSG(expr, msg) \
    do { \
        if (AESTRA_UNLIKELY(!(expr))) { \
            Aestra::verifyHandler(#expr, __FILE__, __LINE__, msg); \
        } \
    } while (0)

// =============================================================================
// Precondition/Postcondition Checks
// =============================================================================

#if AESTRA_ENABLE_ASSERTS

#define AESTRA_PRECONDITION(expr) AESTRA_ASSERT_MSG(expr, "Precondition violated")
#define AESTRA_POSTCONDITION(expr) AESTRA_ASSERT_MSG(expr, "Postcondition violated")
#define AESTRA_INVARIANT(expr) AESTRA_ASSERT_MSG(expr, "Invariant violated")

#else

#define AESTRA_PRECONDITION(expr) ((void)0)
#define AESTRA_POSTCONDITION(expr) ((void)0)
#define AESTRA_INVARIANT(expr) ((void)0)

#endif

// =============================================================================
// Bounds Checking
// =============================================================================

#if AESTRA_ENABLE_ASSERTS

#define AESTRA_ASSERT_RANGE(value, min, max) \
    AESTRA_ASSERT_FMT((value) >= (min) && (value) <= (max), \
        "Value " << (value) << " out of range [" << (min) << ", " << (max) << "]")

#define AESTRA_ASSERT_INDEX(index, size) \
    AESTRA_ASSERT_FMT((index) >= 0 && (index) < (size), \
        "Index " << (index) << " out of bounds (size: " << (size) << ")")

#else

#define AESTRA_ASSERT_RANGE(value, min, max) ((void)0)
#define AESTRA_ASSERT_INDEX(index, size) ((void)0)

#endif

// =============================================================================
// Null Pointer Checks
// =============================================================================

#if AESTRA_ENABLE_ASSERTS

#define AESTRA_ASSERT_NOT_NULL(ptr) \
    AESTRA_ASSERT_MSG((ptr) != nullptr, "Pointer is null")

#else

#define AESTRA_ASSERT_NOT_NULL(ptr) ((void)0)

#endif

// =============================================================================
// Unreachable Code
// =============================================================================

#if AESTRA_ENABLE_ASSERTS

#define AESTRA_ASSERT_UNREACHABLE() \
    do { \
        Aestra::assertHandler("Unreachable code reached", __FILE__, __LINE__); \
        AESTRA_UNREACHABLE(); \
    } while (0)

#else

#define AESTRA_ASSERT_UNREACHABLE() AESTRA_UNREACHABLE()

#endif

// =============================================================================
// Not Implemented
// =============================================================================

#define AESTRA_NOT_IMPLEMENTED() \
    do { \
        Aestra::Log::error("Not implemented: " __FILE__ ":" AESTRA_STRINGIFY(__LINE__)); \
        std::abort(); \
    } while (0)

} // namespace Aestra
