// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "PointerRegistry.h"
#include "NomadDebug.h"
#include "NomadLog.h"
#include <sstream>
#include <iomanip>

namespace Nomad {

std::vector<PointerRegistry::Expectation>& PointerRegistry::expectations() {
    static std::vector<Expectation> s_expectations;
    return s_expectations;
}

void PointerRegistry::expectSame(const char* name, const void* a, const void* b) {
#if NOMAD_DEBUG_POINTERS
    expectations().push_back({name, a, b, false});
#else
    (void)name; (void)a; (void)b; // Suppress unused warnings
#endif
}

void PointerRegistry::expectNotNull(const char* name, const void* ptr) {
#if NOMAD_DEBUG_POINTERS
    expectations().push_back({name, ptr, nullptr, true});
#else
    (void)name; (void)ptr;
#endif
}

bool PointerRegistry::validateAll() {
#if NOMAD_DEBUG_POINTERS
    bool allPassed = true;
    int passed = 0;
    int failed = 0;
    
    Log::info("[PointerRegistry] Validating " + std::to_string(expectations().size()) + " expectations...");
    
    for (const auto& exp : expectations()) {
        bool ok = false;
        std::ostringstream oss;
        
        if (exp.isNullCheck) {
            ok = (exp.ptrA != nullptr);
            oss << "  " << (ok ? "✓" : "✗") << " " << exp.name << ": ";
            if (ok) {
                oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(exp.ptrA);
            } else {
                oss << "NULL (FAILED)";
            }
        } else {
            ok = (exp.ptrA == exp.ptrB);
            oss << "  " << (ok ? "✓" : "✗") << " " << exp.name << ": ";
            oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(exp.ptrA);
            if (!ok) {
                oss << " != 0x" << reinterpret_cast<uintptr_t>(exp.ptrB) << " (MISMATCH!)";
            }
        }
        
        if (ok) {
            passed++;
            Log::info(oss.str());
        } else {
            failed++;
            allPassed = false;
            Log::warning(oss.str());
        }
    }
    
    Log::info("[PointerRegistry] Result: " + std::to_string(passed) + " passed, " 
              + std::to_string(failed) + " failed");
    
    return allPassed;
#else
    return true; // Always passes in Release
#endif
}

void PointerRegistry::clear() {
    expectations().clear();
}

} // namespace Nomad
