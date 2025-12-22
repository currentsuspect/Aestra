// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NomadPlatform.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#include <iostream>

// Link against avrt.lib for MMCSS functions
#pragma comment(lib, "avrt.lib")

namespace Nomad {

// =============================================================================
// Platform Threading (Windows Implementation)
// =============================================================================

bool Platform::setCurrentThreadPriority(ThreadPriority priority) {
    int nPriority = THREAD_PRIORITY_NORMAL;

    switch (priority) {
        case ThreadPriority::Low:
            nPriority = THREAD_PRIORITY_BELOW_NORMAL;
            break;
        case ThreadPriority::Normal:
            nPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::High:
            nPriority = THREAD_PRIORITY_HIGHEST; // High priority, but not realtime/MMCSS
            break;
        case ThreadPriority::RealtimeAudio:
            // For standard API, we max out at TIME_CRITICAL, but MMCSS is preferred via AudioThreadScope
            nPriority = THREAD_PRIORITY_TIME_CRITICAL;
            break;
    }

    HANDLE hThread = GetCurrentThread();
    if (SetThreadPriority(hThread, nPriority)) {
        return true;
    }
    
    return false;
}

// =============================================================================
// AudioThreadScope (MMCSS Implementation)
// =============================================================================

Platform::AudioThreadScope::AudioThreadScope() {
    // Enable MMCSS "Pro Audio" profile
    DWORD taskIndex = 0;
    m_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    
    if (m_handle) {
        m_valid = true;
        // Optionally set priority to Critical within the MMCSS group
        // AvSetMmThreadPriority(m_handle, AVRT_PRIORITY_CRITICAL);
    } else {
        m_valid = false;
        // Fallback to strict priority if MMCSS fails
        Platform::setCurrentThreadPriority(ThreadPriority::RealtimeAudio);
        // std::cerr << "Failed to enable MMCSS Pro Audio profile. Error: " << GetLastError() << std::endl;
    }
}

Platform::AudioThreadScope::~AudioThreadScope() {
    if (m_handle) {
        AvRevertMmThreadCharacteristics(m_handle);
        m_handle = nullptr;
    }
    m_valid = false;
}

} // namespace Nomad

#endif // _WIN32
