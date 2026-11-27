// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// MainThreadQueue submission must report failure explicitly so blocking
// approval bridges (plugin first-load trust) fail closed.

#include "MainThreadQueue.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}
} // namespace

int main() {
    using Aestra::MainThreadQueue;

    MainThreadQueue queue;
    bool ran = false;

    require(queue.enqueue([&] { ran = true; }), "enqueue accepts task when running");
    require(queue.tasks.size() == 1, "task queued");
    require(!queue.enqueue(nullptr), "enqueue rejects null task");
    require(!queue.enqueue(std::function<void()>{}), "enqueue rejects empty task");

    // drain
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        for (auto& t : queue.tasks)
            if (t)
                t();
        queue.tasks.clear();
    }
    require(ran, "queued task runs");

    // shutdown rejects submissions (fail closed) and drops queued work
    queue.enqueue([&] { ran = false; });
    {
        std::lock_guard<std::mutex> lock(queue.mutex);
        queue.shuttingDown = true;
        queue.tasks.clear();
    }
    require(!queue.enqueue([&] { ran = true; }), "enqueue rejected during shutdown");
    require(queue.tasks.empty(), "no tasks queued during shutdown");

    std::cout << "[PASS] MainThreadQueueTest\n";
    return 0;
}
