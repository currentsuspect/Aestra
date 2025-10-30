// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "../include/NomadConfig.h"
#include "../include/NomadAssert.h"
#include "../include/NomadLog.h"
#include <iostream>
#include <cassert>

using namespace Nomad;

#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
    }

// =============================================================================
// Config Tests
// =============================================================================
bool testConfig() {
    std::cout << "Testing Configuration..." << std::endl;

    // Test build configuration
    #if NOMAD_DEBUG
        std::cout << "  Build: DEBUG" << std::endl;
    #else
        std::cout << "  Build: RELEASE" << std::endl;
    #endif

    // Test platform detection
    #if NOMAD_PLATFORM_WINDOWS
        std::cout << "  Platform: Windows" << std::endl;
        TEST_ASSERT(NOMAD_PLATFORM_WINDOWS == 1, "Windows platform should be detected");
    #elif NOMAD_PLATFORM_LINUX
        std::cout << "  Platform: Linux" << std::endl;
        TEST_ASSERT(NOMAD_PLATFORM_LINUX == 1, "Linux platform should be detected");
    #elif NOMAD_PLATFORM_MACOS
        std::cout << "  Platform: macOS" << std::endl;
        TEST_ASSERT(NOMAD_PLATFORM_MACOS == 1, "macOS platform should be detected");
    #endif

    // Test compiler detection
    #if NOMAD_COMPILER_MSVC
        std::cout << "  Compiler: MSVC" << std::endl;
    #elif NOMAD_COMPILER_GCC
        std::cout << "  Compiler: GCC" << std::endl;
    #elif NOMAD_COMPILER_CLANG
        std::cout << "  Compiler: Clang" << std::endl;
    #endif

    // Test architecture detection
    #if NOMAD_ARCH_X64
        std::cout << "  Architecture: x64" << std::endl;
    #elif NOMAD_ARCH_X86
        std::cout << "  Architecture: x86" << std::endl;
    #elif NOMAD_ARCH_ARM
        std::cout << "  Architecture: ARM" << std::endl;
    #endif

    // Test SIMD detection
    std::cout << "  SIMD Support:" << std::endl;
    #if NOMAD_SIMD_AVX2
        std::cout << "    - AVX2: YES" << std::endl;
    #endif
    #if NOMAD_SIMD_AVX
        std::cout << "    - AVX: YES" << std::endl;
    #endif
    #if NOMAD_SIMD_SSE4
        std::cout << "    - SSE4: YES" << std::endl;
    #endif
    #if NOMAD_SIMD_SSE2
        std::cout << "    - SSE2: YES" << std::endl;
    #endif

    // Test audio configuration
    TEST_ASSERT(Config::DEFAULT_SAMPLE_RATE == 48000, "Default sample rate should be 48000");
    TEST_ASSERT(Config::DEFAULT_BUFFER_SIZE == 512, "Default buffer size should be 512");
    TEST_ASSERT(Config::DEFAULT_NUM_CHANNELS == 2, "Default channels should be 2");

    // Test version
    std::cout << "  Version: " << NOMAD_VERSION_STRING << std::endl;
    TEST_ASSERT(NOMAD_VERSION_MAJOR == 0, "Major version should be 0");
    TEST_ASSERT(NOMAD_VERSION_MINOR == 1, "Minor version should be 1");
    TEST_ASSERT(NOMAD_VERSION_PATCH == 0, "Patch version should be 0");

    // Test utility macros
    const char* test = NOMAD_STRINGIFY(NOMAD);
    TEST_ASSERT(std::string(test) == "NOMAD", "Stringify macro should work");

    int testArray[] = {1, 2, 3, 4, 5};
    TEST_ASSERT(NOMAD_ARRAY_SIZE(testArray) == 5, "Array size macro should work");

    std::cout << "  âœ“ Configuration tests passed" << std::endl;
    return true;
}

// =============================================================================
// Assert Tests (only test non-failing cases)
// =============================================================================
bool testAsserts() {
    std::cout << "\nTesting Assertions..." << std::endl;

    // Initialize logger to capture assert messages
    auto fileLogger = std::make_shared<FileLogger>("test_assert.log", LogLevel::Debug);
    Log::init(fileLogger);

    #if NOMAD_ENABLE_ASSERTS
        std::cout << "  Assertions: ENABLED" << std::endl;
        
        // Test passing assertions (should not trigger)
        NOMAD_ASSERT(true);
        NOMAD_ASSERT_MSG(1 + 1 == 2, "Math works");
        NOMAD_ASSERT_FMT(5 > 3, "Five is greater than three");
        
        // Test preconditions/postconditions/invariants
        NOMAD_PRECONDITION(true);
        NOMAD_POSTCONDITION(true);
        NOMAD_INVARIANT(true);
        
        // Test bounds checking
        NOMAD_ASSERT_RANGE(5, 0, 10);
        NOMAD_ASSERT_INDEX(3, 10);
        
        // Test null pointer check
        int value = 42;
        int* ptr = &value;
        NOMAD_ASSERT_NOT_NULL(ptr);
        
        std::cout << "  âœ“ All passing assertions work correctly" << std::endl;
    #else
        std::cout << "  Assertions: DISABLED (release build)" << std::endl;
        
        // Assertions should compile to nothing
        NOMAD_ASSERT(false); // This won't trigger in release
        NOMAD_ASSERT_MSG(false, "This won't trigger");
        
        std::cout << "  âœ“ Assertions disabled correctly" << std::endl;
    #endif

    // Test static assertions (compile-time)
    NOMAD_STATIC_ASSERT(sizeof(int) >= 4, "int must be at least 4 bytes");
    NOMAD_STATIC_ASSERT(true, "This should always pass");

    // Test verify (always enabled)
    NOMAD_VERIFY(true);
    NOMAD_VERIFY_MSG(1 == 1, "One equals one");

    // Test compiler hints
    int x = 10;
    if (NOMAD_LIKELY(x > 0)) {
        // Likely branch
    }
    if (NOMAD_UNLIKELY(x < 0)) {
        // Unlikely branch
    }

    // Cleanup
    std::remove("test_assert.log");

    std::cout << "  âœ“ Assertion tests passed" << std::endl;
    return true;
}

// =============================================================================
// Force Inline Test
// =============================================================================
NOMAD_FORCE_INLINE int forceInlinedFunction(int a, int b) {
    return a + b;
}

NOMAD_NO_INLINE int noInlineFunction(int a, int b) {
    return a * b;
}

bool testCompilerAttributes() {
    std::cout << "\nTesting Compiler Attributes..." << std::endl;

    // Test force inline
    int result1 = forceInlinedFunction(5, 3);
    TEST_ASSERT(result1 == 8, "Force inlined function should work");

    // Test no inline
    int result2 = noInlineFunction(5, 3);
    TEST_ASSERT(result2 == 15, "No inline function should work");

    // Test unused macro
    int unusedVar = 42;
    NOMAD_UNUSED(unusedVar);

    std::cout << "  âœ“ Compiler attribute tests passed" << std::endl;
    return true;
}

// =============================================================================
// Main Test Runner
// =============================================================================
int main() {
    std::cout << "\n==================================" << std::endl;
    std::cout << "  NomadCore Config & Assert Tests" << std::endl;
    std::cout << "==================================" << std::endl;

    bool allPassed = true;
    allPassed &= testConfig();
    allPassed &= testAsserts();
    allPassed &= testCompilerAttributes();

    std::cout << "\n==================================" << std::endl;
    if (allPassed) {
        std::cout << "  âœ“ ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "  âœ— SOME TESTS FAILED" << std::endl;
    }
    std::cout << "==================================" << std::endl;

    return allPassed ? 0 : 1;
}
