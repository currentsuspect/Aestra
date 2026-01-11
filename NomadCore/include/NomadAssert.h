// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NomadConfig.h"
#include "NomadLog.h"
#include <sstream>

namespace Nomad {

// =============================================================================
// Assertion System
// =============================================================================

#if NOMAD_ENABLE_ASSERTS

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
    #if NOMAD_COMPILER_MSVC
        __debugbreak();
    #elif NOMAD_COMPILER_GCC || NOMAD_COMPILER_CLANG
        __builtin_trap();
    #else
        std::abort();
    #endif
}

// Basic assertion
#define NOMAD_ASSERT(expr) \
    do { \
        if (NOMAD_UNLIKELY(!(expr))) { \
            Nomad::assertHandler(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

// Assertion with message
#define NOMAD_ASSERT_MSG(expr, msg) \
    do { \
        if (NOMAD_UNLIKELY(!(expr))) { \
            Nomad::assertHandler(#expr, __FILE__, __LINE__, msg); \
        } \
    } while (0)

// Assertion with formatted message
#define NOMAD_ASSERT_FMT(expr, ...) \
    do { \
        if (NOMAD_UNLIKELY(!(expr))) { \
            std::stringstream ss; \
            ss << __VA_ARGS__; \
            Nomad::assertHandler(#expr, __FILE__, __LINE__, ss.str().c_str()); \
        } \
    } while (0)

#else

// Assertions disabled - compile to nothing
#define NOMAD_ASSERT(expr) ((void)0)
#define NOMAD_ASSERT_MSG(expr, msg) ((void)0)
#define NOMAD_ASSERT_FMT(expr, ...) ((void)0)

#endif // NOMAD_ENABLE_ASSERTS

// =============================================================================
// Static Assertions (always enabled)
// =============================================================================

#define NOMAD_STATIC_ASSERT(expr, msg) static_assert(expr, msg)

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

#define NOMAD_VERIFY(expr) \
    do { \
        if (NOMAD_UNLIKELY(!(expr))) { \
            Nomad::verifyHandler(#expr, __FILE__, __LINE__); \
        } \
    } while (0)

#define NOMAD_VERIFY_MSG(expr, msg) \
    do { \
        if (NOMAD_UNLIKELY(!(expr))) { \
            Nomad::verifyHandler(#expr, __FILE__, __LINE__, msg); \
        } \
    } while (0)

// =============================================================================
// Precondition/Postcondition Checks
// =============================================================================

#if NOMAD_ENABLE_ASSERTS

#define NOMAD_PRECONDITION(expr) NOMAD_ASSERT_MSG(expr, "Precondition violated")
#define NOMAD_POSTCONDITION(expr) NOMAD_ASSERT_MSG(expr, "Postcondition violated")
#define NOMAD_INVARIANT(expr) NOMAD_ASSERT_MSG(expr, "Invariant violated")

#else

#define NOMAD_PRECONDITION(expr) ((void)0)
#define NOMAD_POSTCONDITION(expr) ((void)0)
#define NOMAD_INVARIANT(expr) ((void)0)

#endif

// =============================================================================
// Bounds Checking
// =============================================================================

#if NOMAD_ENABLE_ASSERTS

#define NOMAD_ASSERT_RANGE(value, min, max) \
    NOMAD_ASSERT_FMT((value) >= (min) && (value) <= (max), \
        "Value " << (value) << " out of range [" << (min) << ", " << (max) << "]")

#define NOMAD_ASSERT_INDEX(index, size) \
    NOMAD_ASSERT_FMT((index) >= 0 && (index) < (size), \
        "Index " << (index) << " out of bounds (size: " << (size) << ")")

#else

#define NOMAD_ASSERT_RANGE(value, min, max) ((void)0)
#define NOMAD_ASSERT_INDEX(index, size) ((void)0)

#endif

// =============================================================================
// Null Pointer Checks
// =============================================================================

#if NOMAD_ENABLE_ASSERTS

#define NOMAD_ASSERT_NOT_NULL(ptr) \
    NOMAD_ASSERT_MSG((ptr) != nullptr, "Pointer is null")

#else

#define NOMAD_ASSERT_NOT_NULL(ptr) ((void)0)

#endif

// =============================================================================
// Unreachable Code
// =============================================================================

#if NOMAD_ENABLE_ASSERTS

#define NOMAD_ASSERT_UNREACHABLE() \
    do { \
        Nomad::assertHandler("Unreachable code reached", __FILE__, __LINE__); \
        NOMAD_UNREACHABLE(); \
    } while (0)

#else

#define NOMAD_ASSERT_UNREACHABLE() NOMAD_UNREACHABLE()

#endif

// =============================================================================
// Not Implemented
// =============================================================================

#define NOMAD_NOT_IMPLEMENTED() \
    do { \
        Nomad::Log::error("Not implemented: " __FILE__ ":" NOMAD_STRINGIFY(__LINE__)); \
        std::abort(); \
    } while (0)

} // namespace Nomad
