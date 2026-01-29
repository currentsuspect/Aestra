// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
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

void setThreadPriorityHigh(std::thread& t) {
#ifdef _WIN32
    HANDLE hThread = (HANDLE)t.native_handle();
    if (hThread) {
        // THREAD_PRIORITY_HIGHEST = 2
        SetThreadPriority(hThread, 2);
    }
#else
    (void)t;
#endif
}

} // namespace Aestra
