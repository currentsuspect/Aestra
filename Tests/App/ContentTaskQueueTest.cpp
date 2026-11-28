// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Content scanner callbacks dispatch to a heap-shared queue; queued work
// captures weak ownership and no-ops once the owner is destroyed.

#include "ContentTaskQueue.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}
} // namespace

int main() {
    using ::ContentTaskQueue;

    auto queue = std::make_shared<ContentTaskQueue>();
    bool ranForDestroyed = false;
    bool ranForLive = false;

    std::weak_ptr<int> destroyedOwner;
    {
        auto owner = std::make_shared<int>(1);
        destroyedOwner = owner;
        queue->tasks.push_back([destroyedOwner, &ranForDestroyed]() {
            if (destroyedOwner.lock()) ranForDestroyed = true; // must not run
        });
    } // owner destroyed before task executes

    auto liveOwner = std::make_shared<int>(2);
    std::weak_ptr<int> liveWeak = liveOwner;
    queue->tasks.push_back([liveWeak, &ranForLive]() {
        if (liveWeak.lock()) ranForLive = true; // owner still alive
    });

    // drain
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(queue->mutex);
        tasks.swap(queue->tasks);
    }
    for (auto& t : tasks) if (t) t();

    require(!ranForDestroyed, "task does not run against destroyed owner");
    require(ranForLive, "task runs for live owner");
    require(queue->tasks.empty(), "queue drained");

    // clear drops stale queued work
    queue->tasks.push_back([&] { ranForLive = false; });
    {
        std::lock_guard<std::mutex> lock(queue->mutex);
        queue->tasks.clear();
    }
    require(queue->tasks.empty(), "stale work cleared");

    std::cout << "[PASS] ContentTaskQueueTest\n";
    return 0;
}
