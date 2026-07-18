// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/RealtimeObjectPublisher.h"

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

struct DestructionRecord {
    std::mutex mutex;
    int firstDestroyed{0};
    std::thread::id firstDestructionThread;
};

struct Probe {
    Probe(int probeId, std::shared_ptr<DestructionRecord> destructionRecord)
        : id(probeId), record(std::move(destructionRecord)) {}

    int id{0};
    std::shared_ptr<DestructionRecord> record;

    ~Probe() {
        if (id == 1) {
            std::lock_guard<std::mutex> lock(record->mutex);
            ++record->firstDestroyed;
            record->firstDestructionThread = std::this_thread::get_id();
        }
    }
};

} // namespace

int main() {
    Aestra::Audio::RealtimeObjectPublisher<Probe> publisher;
    auto record = std::make_shared<DestructionRecord>();
    publisher.publish(std::make_shared<Probe>(1, record));

    std::mutex phaseMutex;
    std::condition_variable phaseCv;
    bool acquired = false;
    bool mayRelease = false;
    bool readerSawExpectedObject = false;
    std::thread::id readerThreadId;

    std::thread reader([&] {
        readerThreadId = std::this_thread::get_id();
        Probe* object = publisher.acquireRealtime();
        {
            std::lock_guard<std::mutex> lock(phaseMutex);
            readerSawExpectedObject = object && object->id == 1;
            acquired = true;
        }
        phaseCv.notify_one();

        {
            std::unique_lock<std::mutex> lock(phaseMutex);
            phaseCv.wait(lock, [&] { return mayRelease; });
        }
        publisher.releaseRealtime();
    });

    {
        std::unique_lock<std::mutex> lock(phaseMutex);
        phaseCv.wait(lock, [&] { return acquired; });
    }
    require(readerSawExpectedObject, "real-time reader did not acquire the published object");

    publisher.publish(std::make_shared<Probe>(2, record));
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        require(record->firstDestroyed == 0, "replaced object was reclaimed while the reader still held its hazard");
    }

    {
        std::lock_guard<std::mutex> lock(phaseMutex);
        mayRelease = true;
    }
    phaseCv.notify_one();
    reader.join();

    const std::thread::id controlThreadId = std::this_thread::get_id();
    publisher.collectRetired();
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        require(record->firstDestroyed == 1, "released object was not reclaimed exactly once");
        require(record->firstDestructionThread == controlThreadId,
                "last shared ownership was not released on the control thread");
        require(record->firstDestructionThread != readerThreadId,
                "last shared ownership was released on the real-time reader thread");
    }

    Probe* current = publisher.acquireRealtime();
    require(current && current->id == 2, "new publication was not visible to the real-time reader");
    publisher.releaseRealtime();
    publisher.publish(nullptr);
    publisher.collectRetired();

    std::cout << "[PASS] RealtimeObjectPublisherTest\n";
    return 0;
}
