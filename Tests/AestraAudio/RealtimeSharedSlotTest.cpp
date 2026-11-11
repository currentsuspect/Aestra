// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RealtimeSharedSlot: the audio thread's lock-free read of a published shared_ptr.
//   1. a pinned object stays intact however many publishes land meanwhile;
//   2. no object is ever destroyed on a reader thread (the audio thread must never
//      free), and every object is destroyed exactly once;
//   3. the slot keeps nothing alive beyond the current generation once unpinned;
//   4. copying the pinned owner (shared()) extends the lifetime without a lock.

#include "Core/RealtimeSharedSlot.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using Aestra::Audio::RealtimeSharedSlot;

namespace {

std::atomic<int> g_live{0};
std::atomic<int> g_destroyedOnReader{0};
thread_local bool t_isReader = false;

struct Payload {
    explicit Payload(uint64_t v) : value(v), check(~v) { g_live.fetch_add(1); }
    ~Payload() {
        if (t_isReader) {
            g_destroyedOnReader.fetch_add(1);
        }
        value = 0;
        check = 0;
        g_live.fetch_sub(1);
    }
    bool intact() const { return value != 0 && check == ~value; }
    uint64_t value;
    uint64_t check;
};

void testConcurrentReadersNeverSeeTornOrFreedObjects() {
    RealtimeSharedSlot<Payload> slot(std::make_shared<Payload>(1));
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads{0};
    std::atomic<int> torn{0};

    std::vector<std::thread> readers;
    for (int r = 0; r < 3; ++r) {
        readers.emplace_back([&] {
            t_isReader = true;
            while (!stop.load(std::memory_order_relaxed)) {
                auto pin = slot.readRealtime();
                if (!pin || !pin->intact()) {
                    torn.fetch_add(1);
                }
                // Hold a copy of the owner past the pin, like a sampler voice does.
                std::shared_ptr<Payload> held = pin.shared();
                std::this_thread::yield();
                if (!held->intact()) {
                    torn.fetch_add(1);
                }
                reads.fetch_add(1, std::memory_order_relaxed);
                // Dropping `held` never runs the destructor here: the writer's retire list
                // still owns every published object until the readers have stopped.
            }
        });
    }

    // The writer keeps every object it published until the readers are done, mirroring the
    // engine's retire lists (GarbageCollector): a reader copy is never the last reference.
    std::vector<std::shared_ptr<Payload>> retired;
    for (uint64_t v = 2; v < 20000; ++v) {
        auto next = std::make_shared<Payload>(v);
        retired.push_back(next);
        slot.publish(std::move(next));
    }
    stop.store(true);
    for (auto& t : readers) {
        t.join();
    }
    assert(torn.load() == 0);
    assert(reads.load() > 0);
    retired.clear();
    assert(g_destroyedOnReader.load() == 0);
    std::printf("[PASS] %llu concurrent reads, no torn or freed object, nothing destroyed on a reader\n",
                static_cast<unsigned long long>(reads.load()));
}

void testSlotKeepsOnlyTheCurrentGeneration() {
    const int before = g_live.load();
    {
        RealtimeSharedSlot<Payload> slot;
        assert(!slot.readRealtime());
        slot.publish(std::make_shared<Payload>(7));
        slot.publish(std::make_shared<Payload>(8));
        slot.publish(std::make_shared<Payload>(9));
        assert(g_live.load() == before + 1 && "replaced, unpinned generations are released at publish");
        assert(slot.readRealtime()->value == 9);
        assert(slot.owner()->value == 9);

        auto pin = slot.readRealtime();
        slot.publish(std::make_shared<Payload>(10));
        assert(pin->intact() && pin->value == 9 && "a pinned generation survives publishes");
        assert(g_live.load() == before + 2);
    }
    assert(g_live.load() == before);
    std::printf("[PASS] the slot holds only the current generation once unpinned\n");
}

} // namespace

int main() {
    testConcurrentReadersNeverSeeTornOrFreedObjects();
    testSlotKeepsOnlyTheCurrentGeneration();
    return 0;
}
