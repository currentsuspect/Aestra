// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RTGuard.h"

namespace Aestra {
namespace Audio {

// Thread-local audit data
thread_local ThreadLocalRTAudit g_rtAuditData;

} // namespace Audio
} // namespace Aestra

// Allocation override for AESTRA_AUDIT_MODE builds
#if defined(AESTRA_AUDIT_MODE)

#include <cstdlib>
#include <new>

// Cross-platform return address intrinsic
#if defined(_MSC_VER)
#include <intrin.h>
#define AESTRA_RETURN_ADDRESS() _ReturnAddress()
#else
#define AESTRA_RETURN_ADDRESS() __builtin_return_address(0)
#endif

// Track if we're inside our own override to prevent reentrancy
static thread_local bool g_inOverride = false;

void* operator new(std::size_t size) {
    if (!g_inOverride && Aestra::Audio::isRealtimeAudioThread()) {
        g_inOverride = true;
        Aestra::Audio::recordRTViolation(Aestra::Audio::RTViolationType::Allocation,
                                          AESTRA_RETURN_ADDRESS(), size);
        g_inOverride = false;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (!g_inOverride && Aestra::Audio::isRealtimeAudioThread()) {
        g_inOverride = true;
        Aestra::Audio::recordRTViolation(Aestra::Audio::RTViolationType::Deallocation,
                                          AESTRA_RETURN_ADDRESS());
        g_inOverride = false;
    }
    std::free(ptr);
}

// C++14 sized deallocation
void operator delete(void* ptr, std::size_t) noexcept {
    operator delete(ptr);
}

#undef AESTRA_RETURN_ADDRESS

#endif // AESTRA_AUDIT_MODE
