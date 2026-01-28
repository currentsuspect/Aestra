#include "AestraThreading.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // ALLOW_PLATFORM_INCLUDE
#endif

namespace Aestra {

void MMCSS::setProAudio() {
#ifdef _WIN32
    static auto impl = []() {
        HMODULE hAvrt = LoadLibraryA("Avrt.dll");
        if (hAvrt) {
            typedef HANDLE (WINAPI *AvSetMmThreadCharacteristicsA_t)(LPCSTR, LPDWORD);
            auto pFunc = (AvSetMmThreadCharacteristicsA_t)GetProcAddress(hAvrt, "AvSetMmThreadCharacteristicsA");
            if (pFunc) {
                DWORD taskIndex = 0;
                pFunc("Pro Audio", &taskIndex);
                // We knowingly leak the handle return/don't revert for this thread's lifetime
                // (typical for dedicated audio threads).
                // Also leak lib handle to keep function pointer valid.
            }
        }
        return 0;
    }();
    (void)impl;

    // Also boost Win32 priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

} // namespace Aestra
