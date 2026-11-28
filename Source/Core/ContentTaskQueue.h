// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <functional>
#include <mutex>
#include <vector>

/// Heap-shared main-thread task queue; workers push without touching the owner.
struct ContentTaskQueue {
    std::mutex mutex;
    std::vector<std::function<void()>> tasks;
};
