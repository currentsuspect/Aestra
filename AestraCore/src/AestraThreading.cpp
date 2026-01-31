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

// =============================================================================
// MMCSS (Multimedia Class Scheduler Service) - "Pro Audio" Priority
// =============================================================================
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

// =============================================================================
// Thread Pool Implementation
// =============================================================================
ThreadPool::ThreadPool(size_t numThreads) : stop(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    condition.wait(lock, [this] {
                        return stop || !tasks.empty();
                    });

                    if (stop && tasks.empty()) {
                        return;
                    }

                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        });

#ifdef _WIN32
        // Set thread priority to HIGHEST to ensure audio processing isn't starved
        // by UI or background tasks. Using extern defs to avoid windows.h pollution.
        // THREAD_PRIORITY_HIGHEST = 2
        SetThreadPriority(workers.back().native_handle(), 2);
#endif
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

} // namespace Aestra
