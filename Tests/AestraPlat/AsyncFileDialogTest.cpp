// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// AestraFileDialog.h: a native picker must never block the UI thread, and
// nothing may ever wait for one — not starting it, not polling it, and not
// destroying its owner (the old std::future-in-a-destructor hung window close
// until the user dismissed the picker).
//
// The "picker" here is a lambda that blocks on a latch, released from a helper
// thread after a delay, so a regression that runs the picker inline shows up as
// a slow start() instead of a deadlock.

#include "AestraFileDialog.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                                                              \
    do {                                                                                                              \
        if (!(cond)) {                                                                                                \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                        \
            ++g_failures;                                                                                             \
        }                                                                                                             \
    } while (0)

using Clock = std::chrono::steady_clock;
constexpr auto kPickerDelay = std::chrono::milliseconds(300);
constexpr auto kInstant = std::chrono::milliseconds(100);

// A picker the test controls: blocks until release().
struct Latch {
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    void release() {
        {
            std::lock_guard<std::mutex> lock(m);
            open = true;
        }
        cv.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return open; });
    }
};

void releaseLater(const std::shared_ptr<Latch>& latch) {
    std::thread([latch] {
        std::this_thread::sleep_for(kPickerDelay);
        latch->release();
    }).detach();
}

std::optional<std::string> waitForResult(Aestra::PendingFileDialog& dialog) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        if (auto r = dialog.takeResult()) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

void testStartDoesNotBlockAndDeliversOnce() {
    auto latch = std::make_shared<Latch>();
    releaseLater(latch);

    Aestra::PendingFileDialog dialog;
    const auto t0 = Clock::now();
    const bool started = dialog.start([latch](Aestra::IPlatformUtils&) {
        latch->wait();
        return std::string("/picked/folder");
    });
    const auto startCost = Clock::now() - t0;

    CHECK(started, "picker starts");
    CHECK(startCost < kInstant, "start() returns while the picker is still open");
    CHECK(dialog.isPending(), "pending while open");
    CHECK(!dialog.takeResult().has_value(), "no result before the picker closes");
    CHECK(!dialog.start([](Aestra::IPlatformUtils&) { return std::string(); }), "one picker at a time per owner");

    const auto result = waitForResult(dialog);
    CHECK(result.has_value() && *result == "/picked/folder", "result delivered");
    CHECK(!dialog.isPending(), "not pending after delivery");
    CHECK(!dialog.takeResult().has_value(), "delivered exactly once");
}

void testOwnerDestructionDoesNotWait() {
    auto latch = std::make_shared<Latch>();
    auto finished = std::make_shared<std::atomic<bool>>(false);
    // Released on a timer, not after the scope: a destructor that waits then
    // costs kPickerDelay and fails the check below instead of deadlocking.
    releaseLater(latch);
    const auto t0 = Clock::now();
    {
        Aestra::PendingFileDialog dialog;
        dialog.start([latch, finished](Aestra::IPlatformUtils&) {
            latch->wait();
            finished->store(true);
            return std::string("/late");
        });
    } // owner gone while the picker is open
    const auto destroyCost = Clock::now() - t0;
    CHECK(destroyCost < kInstant, "destroying the owner mid-picker returns immediately");

    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!finished->load() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(finished->load(), "the orphaned picker still completes (its late result is dropped)");
}

void testWorkerOutlivesPlatformShutdown() {
    auto latch = std::make_shared<Latch>();
    Aestra::PendingFileDialog dialog;
    dialog.start([latch](Aestra::IPlatformUtils& utils) {
        latch->wait();
        // After shutdown() the raw getUtils() pointer is gone; this reference
        // must still be alive because the worker co-owns it.
        return utils.getPlatformName();
    });
    Aestra::Platform::shutdown();
    CHECK(Aestra::Platform::getUtils() == nullptr, "platform shut down while the picker is open");
    latch->release();

    const auto result = waitForResult(dialog);
    CHECK(result.has_value() && !result->empty(), "worker used its shared platform utils after shutdown");
}

} // namespace

int main() {
#if !defined(_WIN32)
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    // SDL otherwise turns SIGTERM/SIGINT into a quit event, so a hung
    // regression could not be stopped by ctest's timeout.
    setenv("SDL_NO_SIGNAL_HANDLERS", "1", 1);
#endif
    if (!Aestra::Platform::initialize()) {
        std::fprintf(stderr, "AsyncFileDialogTest: platform layer unavailable\n");
        return 1;
    }
#if !defined(_WIN32) // Windows runs the picker inline by design (see AestraFileDialog.h)
    testStartDoesNotBlockAndDeliversOnce();
    testOwnerDestructionDoesNotWait();
    testWorkerOutlivesPlatformShutdown();
#endif

    if (g_failures == 0) {
        std::printf("AsyncFileDialogTest: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "AsyncFileDialogTest: %d check(s) failed\n", g_failures);
    return 1;
}
