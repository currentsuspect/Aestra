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
// MMCSS
// =============================================================================
MMCSS::MMCSS() {
#ifdef _WIN32
    // Static initialization of function pointer
    static auto pAvSetMmThreadCharacteristicsA = []() {
        HMODULE hAvrt = LoadLibraryA("Avrt.dll");
        if (hAvrt) {
            typedef HANDLE (WINAPI *AvSetMmThreadCharacteristicsA_t)(LPCSTR, LPDWORD);
            return (AvSetMmThreadCharacteristicsA_t)GetProcAddress(hAvrt, "AvSetMmThreadCharacteristicsA");
        }
        return (HANDLE (WINAPI *)(LPCSTR, LPDWORD))nullptr;
    }();

    if (pAvSetMmThreadCharacteristicsA) {
        DWORD taskIndex = 0;
        m_taskHandle = pAvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    }

    // Also boost Win32 priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

MMCSS::~MMCSS() {
#ifdef _WIN32
    if (m_taskHandle) {
        static auto pAvRevertMmThreadCharacteristics = []() {
            // Avrt.dll should already be loaded
            HMODULE hAvrt = GetModuleHandleA("Avrt.dll");
            if (hAvrt) {
                 typedef BOOL (WINAPI *AvRevertMmThreadCharacteristics_t)(HANDLE);
                 return (AvRevertMmThreadCharacteristics_t)GetProcAddress(hAvrt, "AvRevertMmThreadCharacteristics");
            }
            return (BOOL (WINAPI *)(HANDLE))nullptr;
        }();

        if (pAvRevertMmThreadCharacteristics) {
            pAvRevertMmThreadCharacteristics(static_cast<HANDLE>(m_taskHandle));
        }
    }
#endif
}

// =============================================================================
// Thread Pool
// =============================================================================
ThreadPool::ThreadPool(size_t numThreads)
    : stop(false) {
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
        // by UI or background tasks.
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

// =============================================================================
// Real-Time Thread Pool
// =============================================================================
RealTimeThreadPool::RealTimeThreadPool(size_t numThreads) : m_stop(false), m_taskCount(0), m_activeTasks(0) {
    m_taskCounter.store(0);
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this, i] {
            // "Black Magic": Register as System Pro Audio Task
            MMCSS mmcssGuard;
            workerLoop(static_cast<uint32_t>(i));
        });
    }
}

RealTimeThreadPool::~RealTimeThreadPool() {
    m_stop = true;
    m_signal.notify_all();
    for (auto& w : m_workers) {
        if (w.joinable()) w.join();
    }
}

void RealTimeThreadPool::workerLoop(uint32_t threadIdx) {
    (void)threadIdx;
    while (!m_stop) {
        m_signal.wait([this] { return m_stop || m_taskCounter.load(std::memory_order_acquire) < m_taskCount; });

        if (m_stop) return;

        while (true) {
            int idx = m_taskCounter.fetch_add(1, std::memory_order_acq_rel);
            if (idx >= (int)m_taskCount) break;

            m_taskFunc(m_context, m_taskData[idx]);

            if (m_syncBarrier) {
                m_syncBarrier->signal();
            }

            m_activeTasks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
}

} // namespace Aestra
