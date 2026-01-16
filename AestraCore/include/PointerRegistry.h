// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file PointerRegistry.h
 * @brief Runtime pointer validation for catching wiring bugs early
 * 
 * Use this during initialization to verify that critical pointers match
 * across subsystems. Logs warnings if mismatches are detected.
 * 
 * Usage in AestraApp::initialize():
 * @code
 * PointerRegistry::expectSame("MeterBuffer",
 *     m_audioEngine->getSnapshot(), m_trackManager->getMeterSnapshots().get());
 * PointerRegistry::validateAll();
 * @endcode
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace Aestra {

/**
 * @brief Simple pointer validation registry for catching wiring bugs
 * 
 * All methods are static. Validation happens during app initialization.
 * In Release builds, these calls compile to no-ops for zero overhead.
 */
class PointerRegistry {
public:
    /**
     * @brief Register an expectation that two pointers should be equal
     * 
     * @param name Human-readable name for logging
     * @param a First pointer
     * @param b Second pointer
     */
    static void expectSame(const char* name, const void* a, const void* b);
    
    /**
     * @brief Register an expectation that a pointer should not be null
     * 
     * @param name Human-readable name for logging
     * @param ptr Pointer to check
     */
    static void expectNotNull(const char* name, const void* ptr);
    
    /**
     * @brief Validate all registered expectations and log results
     * 
     * Call this once during initialization after all registrations.
     * Logs warnings for any failed expectations.
     * 
     * @return true if all expectations passed, false if any failed
     */
    static bool validateAll();
    
    /**
     * @brief Clear all registered expectations
     * 
     * Call this if you need to re-validate (e.g., after hot reload).
     */
    static void clear();

private:
    struct Expectation {
        std::string name;
        const void* ptrA;
        const void* ptrB;
        bool isNullCheck;
    };
    
    static std::vector<Expectation>& expectations();
};

} // namespace Aestra
