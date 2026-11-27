// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

namespace Aestra {

/// Main-thread task queue (worker → UI hop). Heap-shared so DETACHED workers
/// never touch a half-destroyed app; workers hold a copy of this shared_ptr.
struct MainThreadQueue {
    std::mutex mutex;
    std::vector<std::function<void()>> tasks;
    bool shuttingDown{false};
    std::atomic<bool> relinkInFlight{false};
    std::atomic<bool> fileDialogInFlight{false};

    /// Queue `task`; false if null or shutting down. Workers hold the queue by shared_ptr.
    bool enqueue(std::function<void()> task) {
        if (!task)
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (shuttingDown)
            return false;
        tasks.push_back(std::move(task));
        return true;
    }
};

} // namespace Aestra
