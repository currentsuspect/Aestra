// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// DeviceManagerRaceTest — deterministic concurrency coverage for AudioDeviceManager
// (#391). Uses a fully controllable fake IAudioDriver so scenarios are reproducible
// rather than probabilistic: a barrier can freeze device enumeration while another
// thread drives a config transition, health-monitor writes race configuration
// reads, snapshots are asserted internally consistent, shutdown races an in-flight
// getter, callbacks are proven to run outside the manager lock, and a failed reopen
// is shown to leave a coherent state. Intended to run clean under ThreadSanitizer.

#include "Core/AudioDeviceManager.h"
#include "Drivers/IAudioDriver.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool cond, const std::string& label) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << label << "\n";
    if (!cond) {
        ++g_failures;
    }
}

constexpr uint32_t kDeviceA = 1;
constexpr uint32_t kDeviceB = 2;

AudioDeviceInfo makeDevice(uint32_t id, const char* name, bool defOut) {
    AudioDeviceInfo d;
    d.id = id;
    d.name = name;
    d.maxInputChannels = 2;
    d.maxOutputChannels = 2;
    d.supportedSampleRates = {44100, 48000, 96000};
    d.preferredSampleRate = 48000;
    d.isDefaultInput = defOut;
    d.isDefaultOutput = defOut;
    return d;
}

// A fully controllable fake driver. Every scenario knob is an atomic so the test
// can steer it from another thread without additional locking.
class FakeDriver : public IAudioDriver {
public:
    explicit FakeDriver(AudioDriverType type, bool available = true)
        : m_type(type), m_available(available) {}

    // --- test controls ---
    std::atomic<bool> m_openShouldFail{false};
    std::atomic<int64_t> m_failDeviceId{-1}; // fail openStream for this deviceId (-1 = none)
    std::atomic<int> m_openCount{0};

    // Enumeration barrier: when armed, getDevices() blocks until released, so a
    // transition on another thread runs while enumeration is mid-flight.
    void armEnumerationBarrier() {
        std::lock_guard<std::mutex> l(m_barrierMx);
        m_barrierArmed = true;
        m_barrierEntered = false;
    }
    void waitEnumerationEntered() {
        std::unique_lock<std::mutex> l(m_barrierMx);
        m_barrierCv.wait(l, [this] { return m_barrierEntered; });
    }
    void releaseEnumerationBarrier() {
        std::lock_guard<std::mutex> l(m_barrierMx);
        m_barrierArmed = false;
        m_barrierCv.notify_all();
    }

    // Freeze the reported callbackCount so the health monitor detects a "stall".
    void freezeCallbacks() { m_callbacksFrozen.store(true); }

    // --- IAudioDriver ---
    std::string getDisplayName() const override { return "FakeDriver"; }
    AudioDriverType getDriverType() const override { return m_type; }
    bool isAvailable() const override { return m_available; }

    std::vector<AudioDeviceInfo> getDevices() override {
        {
            std::unique_lock<std::mutex> l(m_barrierMx);
            if (m_barrierArmed) {
                m_barrierEntered = true;
                m_barrierCv.notify_all();
                m_barrierCv.wait(l, [this] { return !m_barrierArmed; });
            }
        }
        return {makeDevice(kDeviceA, "Device A", true), makeDevice(kDeviceB, "Device B", false)};
    }

    bool openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) override {
        m_openCount.fetch_add(1);
        // Deterministic, config-keyed failure: opening a specific "bad" device
        // fails while others succeed — lets a test fail a transition's reopen but
        // allow its rollback, with no timing involved.
        if (m_failDeviceId.load() == static_cast<int64_t>(config.deviceId)) {
            return false;
        }
        if (m_openShouldFail.load()) {
            return false;
        }
        m_config = config;
        m_callback = callback;
        m_userData = userData;
        m_open = true;
        return true;
    }
    void closeStream() override {
        m_open = false;
        m_running = false;
    }
    bool startStream() override {
        if (!m_open) {
            return false;
        }
        m_running = true;
        return true;
    }
    void stopStream() override { m_running = false; }

    bool isStreamRunning() const override { return m_running.load(); }
    double getStreamLatency() const override { return 0.01; }
    uint32_t getStreamSampleRate() const override { return m_config.sampleRate; }
    uint32_t getStreamBufferSize() const override { return m_config.bufferSize; }

    DriverStatistics getStatistics() const override {
        DriverStatistics s;
        // Advance callbackCount over time unless frozen — the health monitor uses
        // a change in this value as the liveness signal.
        s.callbackCount = m_callbacksFrozen.load() ? m_frozenCount
                                                   : static_cast<uint64_t>(std::chrono::duration_cast<
                                                         std::chrono::milliseconds>(
                                                         std::chrono::steady_clock::now() - m_start)
                                                         .count());
        s.underrunCount = 0;
        return s;
    }
    std::string getErrorMessage() const override { return "fake-open-failed"; }
    void setDitheringEnabled(bool enabled) override { m_dither = enabled; }
    bool isDitheringEnabled() const override { return m_dither; }

private:
    AudioDriverType m_type;
    bool m_available;
    AudioStreamConfig m_config;
    AudioCallback m_callback = nullptr;
    void* m_userData = nullptr;
    std::atomic<bool> m_open{false};
    std::atomic<bool> m_running{false};
    bool m_dither = false;

    std::atomic<bool> m_callbacksFrozen{false};
    uint64_t m_frozenCount = 1;
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();

    mutable std::mutex m_barrierMx;
    std::condition_variable m_barrierCv;
    bool m_barrierArmed = false;
    bool m_barrierEntered = false;
};

int silentCallback(float*, const float*, uint32_t, double, void*) { return 0; }

AudioStreamConfig baseConfig() {
    AudioStreamConfig c;
    c.deviceId = kDeviceA;
    c.sampleRate = 48000;
    c.bufferSize = 256;
    c.numOutputChannels = 2;
    c.numInputChannels = 0;
    return c;
}

// Build a manager with a primary fake + a DUMMY safety driver. Returns the raw
// primary pointer (owned by the manager) for test steering.
struct Harness {
    AudioDeviceManager mgr;
    FakeDriver* primary = nullptr;
};

std::unique_ptr<Harness> makeHarness(bool withDummy = true) {
    auto h = std::make_unique<Harness>();
    auto primary = std::make_unique<FakeDriver>(AudioDriverType::WASAPI_SHARED);
    h->primary = primary.get();
    h->mgr.addDriver(std::move(primary));
    // The DUMMY safety driver is present for most scenarios; the rollback test
    // omits it so an open failure is not silently absorbed by a fallback driver.
    if (withDummy) {
        h->mgr.addDriver(std::make_unique<FakeDriver>(AudioDriverType::DUMMY));
    }
    // Preferred = SHARED so the primary is not treated as a fallback.
    h->mgr.setPreferredDriverType(AudioDriverType::WASAPI_SHARED);
    // initialize(false): use ONLY the injected fakes, never real platform drivers.
    h->mgr.initialize(false);
    return h;
}

// 1. Enumeration barrier vs. a concurrent config transition.
void testEnumerationBarrierVsTransition() {
    auto h = makeHarness();
    check(h->mgr.openStream(baseConfig(), silentCallback, nullptr), "open under fake driver");
    h->mgr.startStream();

    // Freeze enumeration inside a getDevices() call on a worker thread.
    h->primary->armEnumerationBarrier();
    std::atomic<bool> enumDone{false};
    std::thread enumThread([&] {
        (void)h->mgr.getDevices();
        enumDone.store(true);
    });
    h->primary->waitEnumerationEntered(); // worker is now blocked mid-enumeration, holding the manager lock

    // Meanwhile another thread requests a snapshot that does NOT need the driver's
    // devices — getCurrentConfig — plus isStreamRunning. These take the same mutex,
    // so they serialize behind enumeration but must not corrupt or deadlock.
    std::atomic<bool> snapDone{false};
    std::thread snapThread([&] {
        for (int i = 0; i < 50; ++i) {
            auto cfg = h->mgr.getCurrentConfig();
            (void)cfg;
        }
        snapDone.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    check(!enumDone.load(), "enumeration stays blocked at the barrier");
    h->primary->releaseEnumerationBarrier();
    enumThread.join();
    snapThread.join();
    check(enumDone.load() && snapDone.load(), "enumeration and snapshots both complete after release");
    h->mgr.shutdown();
}

// 2. Snapshot coherence: switchDevice flips deviceId; concurrent getCurrentConfig
//    must never observe a torn/partial config.
void testSnapshotCoherenceUnderSwitch() {
    auto h = makeHarness();
    h->mgr.openStream(baseConfig(), silentCallback, nullptr);
    h->mgr.startStream();

    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::thread reader([&] {
        while (!stop.load()) {
            const auto cfg = h->mgr.getCurrentConfig();
            // Whatever device is published, its buffer/rate stay from a valid set.
            if ((cfg.deviceId != kDeviceA && cfg.deviceId != kDeviceB) || cfg.sampleRate != 48000 ||
                cfg.bufferSize != 256) {
                bad.fetch_add(1);
            }
        }
    });

    for (int i = 0; i < 40; ++i) {
        h->mgr.switchDevice((i % 2 == 0) ? kDeviceB : kDeviceA);
    }
    stop.store(true);
    reader.join();
    check(bad.load() == 0, "config snapshots stay internally consistent across switches");
    h->mgr.shutdown();
}

// 3. Health-monitor writes racing configuration reads. Freeze callbacks so the
//    monitor triggers a safety switch while readers hammer getters.
void testHealthMonitorVsReaders() {
    auto h = makeHarness();
    h->mgr.openStream(baseConfig(), silentCallback, nullptr);
    h->mgr.startStream(); // starts the real health monitor thread

    std::atomic<bool> stop{false};
    std::thread reader([&] {
        while (!stop.load()) {
            (void)h->mgr.getCurrentConfig();
            (void)h->mgr.getActiveDriverType();
            (void)h->mgr.getDriverStatistics();
            (void)h->mgr.isUsingFallbackDriver();
            (void)h->mgr.getFallbackReason();
        }
    });

    h->primary->freezeCallbacks(); // monitor will detect a stall (>2s) and switch to DUMMY
    // Poll for the switch rather than a fixed sleep: the stall threshold is ~2s of
    // wall time, but under ThreadSanitizer the monitor thread is scheduled far more
    // slowly, so a fixed wait is flaky. Give it a generous ceiling.
    bool switched = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (h->mgr.getActiveDriverType() == AudioDriverType::DUMMY) {
            switched = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    stop.store(true);
    reader.join();
    check(switched, "health monitor switched to the safety driver under concurrent reads");
    h->mgr.shutdown();
}

// 4. Callback fires OUTSIDE the manager lock: the callback re-enters a getter.
void testCallbackOutsideLock() {
    auto h = makeHarness();
    std::atomic<bool> callbackRan{false};
    std::atomic<bool> reentrantGetterOk{false};
    h->mgr.setDriverModeChangeCallback(
        [&](AudioDriverType, AudioDriverType actual, const std::string&) {
            callbackRan.store(true);
            // If the callback were fired while holding m_mutex, this getter would
            // deadlock. It returning proves the callback runs outside the lock.
            auto cfg = h->mgr.getCurrentConfig();
            (void)cfg;
            reentrantGetterOk.store(actual == AudioDriverType::DUMMY);
        });
    h->mgr.openStream(baseConfig(), silentCallback, nullptr);
    h->mgr.startStream();

    check(h->mgr.switchToSafetyDriver(), "manual safety switch succeeds");
    check(callbackRan.load(), "driver-mode-change callback fired");
    check(reentrantGetterOk.load(), "callback re-entered a manager getter without deadlock (fired outside lock)");
    h->mgr.shutdown();
}

// 5. Shutdown racing an in-flight getter: no use-after-free / crash.
void testShutdownVsGetter() {
    auto h = makeHarness();
    h->mgr.openStream(baseConfig(), silentCallback, nullptr);
    h->mgr.startStream();

    std::atomic<bool> stop{false};
    std::thread getter([&] {
        while (!stop.load()) {
            (void)h->mgr.getDevices();
            (void)h->mgr.getCurrentConfig();
            (void)h->mgr.isStreamRunning();
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    h->mgr.shutdown();      // races the getter thread
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.store(true);
    getter.join();
    check(true, "shutdown races an in-flight getter without crashing");
}

// 6. Failed reopen leaves a coherent, documented state (rollback). No DUMMY here,
//    so a device-B open failure is not absorbed by a fallback driver.
void testFailedReopenRollsBack() {
    auto h = makeHarness(/*withDummy=*/false);
    h->mgr.openStream(baseConfig(), silentCallback, nullptr);
    h->mgr.startStream();
    check(h->mgr.getCurrentConfig().deviceId == kDeviceA, "starts on device A");

    // Device B cannot open; device A (the rollback target) still can.
    h->primary->m_failDeviceId.store(static_cast<int64_t>(kDeviceB));
    const bool switched = h->mgr.switchDevice(kDeviceB);

    check(!switched, "switch reports failure when the new device cannot open");
    const auto cfg = h->mgr.getCurrentConfig();
    check(cfg.deviceId == kDeviceA, "config rolled back to the previous device after failed reopen");
    check(h->mgr.isStreamRunning(), "stream is running again after rollback");
    h->mgr.shutdown();
}

} // namespace

int main() {
    std::cout << "=== AudioDeviceManager race/coherence tests (#391) ===\n";
    testEnumerationBarrierVsTransition();
    testSnapshotCoherenceUnderSwitch();
    testHealthMonitorVsReaders();
    testCallbackOutsideLock();
    testShutdownVsGetter();
    testFailedReopenRollsBack();

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
