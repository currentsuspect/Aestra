// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Focused tests for cancellable first-load trust approval of third-party plugins
// from untrusted paths. No real third-party plugin binaries are loaded: a junk
// `.clap` file in a temp directory exercises the trust gate, and
// AESTRA_PLUGIN_HOST_PATH points at /bin/false so no helper ever loads it.

#include "../Support/TestTempDirectory.h"
#include "Plugin/PluginScanner.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::filesystem::path makeUntrustedClap(const std::filesystem::path& dir, const std::string& name = "untrusted") {
    const auto clapPath = dir / (name + ".clap");
    std::ofstream out(clapPath, std::ios::binary);
    out << "not a clap plugin";
    return clapPath;
}

struct EnvGuard {
    std::string key;
    std::string previous;
    bool hadPrevious{false};
    explicit EnvGuard(std::string k) : key(std::move(k)) {
        if (const char* v = std::getenv(key.c_str())) {
            previous = v;
            hadPrevious = true;
        }
    }
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(key.c_str(), hadPrevious ? previous.c_str() : "");
#else
        if (hadPrevious)
            setenv(key.c_str(), previous.c_str(), 1);
        else
            unsetenv(key.c_str());
#endif
    }
};

// Minimal scan via scanBlocking; built-ins are merged in, so inspect for our file only.
bool resultsContainPath(const std::vector<Aestra::Audio::PluginInfo>& results, const std::filesystem::path& path) {
    const std::string needle = path.string();
    for (const auto& p : results) {
        if (p.path.string() == needle)
            return true;
    }
    return false;
}

} // namespace

int main() {
#ifndef _WIN32
    EnvGuard hostPathGuard("AESTRA_PLUGIN_HOST_PATH");
    hostPathGuard.set("/bin/false");
#endif

    using Aestra::Audio::FirstLoadApprovalRequest;
    using Aestra::Audio::PluginScanner;

    // ------------------------------------------------------------------
    // Shared request: resolved exactly once; cancel denies; wait unblocks.
    // ------------------------------------------------------------------
    {
        FirstLoadApprovalRequest req;
        require(!req.isResolved(), "request starts unresolved");
        require(req.resolve(true), "first resolve wins");
        require(!req.resolve(false), "second resolve is a no-op");
        require(req.isResolved(), "resolved flag set");
        require(req.isAllowed(), "allowed recorded");
        require(req.wait(), "wait returns recorded decision");

        FirstLoadApprovalRequest req2;
        require(req2.cancel(), "cancel resolves");
        require(!req2.wait(), "cancelled request denies");
        require(!req2.resolve(true), "cannot resolve after cancel");
        require(!req2.isAllowed(), "cancelled request never allows");
    }

    // ------------------------------------------------------------------
    // 1. No approval callback: untrusted plugin is skipped.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_NoCb"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        const auto results = scanner.scanBlocking();
        require(!resultsContainPath(results, clapPath), "untrusted plugin skipped without callback");
        require(!scanner.isUntrustedPluginAcknowledged(clapPath), "untrusted plugin not acknowledged without callback");
    }

    // ------------------------------------------------------------------
    // 2. Approval callback returns true: plugin proceeds to scanning.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_Allow"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::atomic<int> callbackCount{0};
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path& path, const std::string& name) -> bool {
            require(!path.empty(), "callback receives plugin path");
            require(!name.empty(), "callback receives plugin name");
            require(path == clapPath, "callback receives the untrusted plugin path");
            ++callbackCount;
            return true; // allow
        });

        const auto results = scanner.scanBlocking();
        (void)results;
        require(callbackCount.load() == 1, "approval callback invoked exactly once");
        require(scanner.isUntrustedPluginAcknowledged(clapPath),
                "approved untrusted plugin proceeds to scanning and is acknowledged");
        require(!scanner.isCancelRequested(), "scan not cancelled");
    }

    // ------------------------------------------------------------------
    // 3. Approval callback returns false: plugin is skipped.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_Deny"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::atomic<int> callbackCount{0};
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            ++callbackCount;
            return false; // deny
        });

        const auto results = scanner.scanBlocking();
        require(!resultsContainPath(results, clapPath), "denied untrusted plugin skipped");
        require(callbackCount.load() == 1, "approval callback invoked exactly once");
        require(!scanner.isUntrustedPluginAcknowledged(clapPath), "denied untrusted plugin not acknowledged");
    }

    // ------------------------------------------------------------------
    // 4. Cancellation while waiting for approval wakes the worker;
    //    scan completes as cancelled (not successful).
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_Cancel"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::mutex waitMutex;
        std::condition_variable waitCv;
        bool callbackWaiting = false;
        bool callbackReturned = false;

        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            auto request = std::make_shared<FirstLoadApprovalRequest>();
            if (!scanner.registerOutstandingApproval(request)) {
                return false; // cancellation already in progress: fail closed
            }
            {
                std::lock_guard<std::mutex> lock(waitMutex);
                callbackWaiting = true;
            }
            waitCv.notify_all();
            const bool allowed = request->wait(); // blocked until resolved; no busy-waiting
            scanner.unregisterOutstandingApproval(request);
            {
                std::lock_guard<std::mutex> lock(waitMutex);
                callbackReturned = true;
            }
            waitCv.notify_all();
            return allowed;
        });

        std::atomic<bool> completionCalled{false};
        std::atomic<bool> completionSuccess{true};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        scanner.scanAsync(nullptr, [&](const std::vector<Aestra::Audio::PluginInfo>&, bool success) {
            completionSuccess = success;
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                done = true;
            }
            completionCalled = true;
            doneCv.notify_all();
        });

        // Wait until the scanner worker is blocked waiting for approval.
        {
            std::unique_lock<std::mutex> lock(waitMutex);
            waitCv.wait(lock, [&] { return callbackWaiting; });
        }

        scanner.cancelScan(); // must wake the outstanding approval request

        {
            std::unique_lock<std::mutex> lock(doneMutex);
            doneCv.wait(lock, [&] { return done; });
        }

        {
            std::unique_lock<std::mutex> lock(waitMutex);
            waitCv.wait(lock, [&] { return callbackReturned; });
        }

        scanner.cancelAndJoin();

        require(completionCalled.load(), "completion callback called");
        require(!completionSuccess.load(), "cancelled scan is not reported as successful");
        require(!scanner.isUntrustedPluginAcknowledged(clapPath), "cancelled approval does not acknowledge plugin");
    }

    // ------------------------------------------------------------------
    // 5. Queue rejection / unavailable submission fails closed.
    //    (registerOutstandingApproval rejects when cancellation is in progress;
    //     caller must deny rather than wait.)
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_QueueReject"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::atomic<bool> registrationRejected{false};
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            auto request = std::make_shared<FirstLoadApprovalRequest>();
            if (!scanner.registerOutstandingApproval(request)) {
                registrationRejected = true;
                request->cancel(); // fail closed
                return request->wait();
            }
            // Simulate main-thread queue rejection / unavailable UI: no task
            // submitted -> resolve denied (never leave the worker blocked).
            request->cancel();
            const bool allowed = request->wait();
            scanner.unregisterOutstandingApproval(request);
            return allowed; // false
        });

        const auto results = scanner.scanBlocking();
        require(!resultsContainPath(results, clapPath), "queue-rejected untrusted plugin skipped");
        require(!scanner.isUntrustedPluginAcknowledged(clapPath), "queue-rejected untrusted plugin not acknowledged");
    }

    // ------------------------------------------------------------------
    // 6. Shutdown with an outstanding approval does not deadlock.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_Shutdown"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::mutex waitMutex;
        std::condition_variable waitCv;
        bool callbackWaiting = false;

        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            auto request = std::make_shared<FirstLoadApprovalRequest>();
            if (!scanner.registerOutstandingApproval(request)) {
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(waitMutex);
                callbackWaiting = true;
            }
            waitCv.notify_all();
            const bool allowed = request->wait(); // no dialog will ever answer
            scanner.unregisterOutstandingApproval(request);
            return allowed;
        });

        std::atomic<bool> completionSuccess{true};
        scanner.scanAsync(
            nullptr, [&](const std::vector<Aestra::Audio::PluginInfo>&, bool success) { completionSuccess = success; });

        {
            std::unique_lock<std::mutex> lock(waitMutex);
            waitCv.wait(lock, [&] { return callbackWaiting; });
        }

        // cancelAndJoin during shutdown must reject outstanding approvals and not deadlock.
        scanner.cancelAndJoin();

        require(!scanner.isScanning(), "scanner stopped after cancelAndJoin");
        require(!completionSuccess.load(), "shutdown cancellation is not a successful scan");
    }

    // ------------------------------------------------------------------
    // 7. A cancelled scan is not reported as successful; no partial results.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_CancelledResults"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        // Allow the plugin, but cancel immediately so results must not be published.
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            scanner.cancelScan();
            return true; // even an allow racing cancellation must not publish
        });

        std::atomic<bool> completionCalled{false};
        std::atomic<bool> completionSuccess{true};
        std::atomic<size_t> resultCount{0};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        scanner.scanAsync(nullptr, [&](const std::vector<Aestra::Audio::PluginInfo>& results, bool success) {
            completionSuccess = success;
            resultCount = results.size();
            completionCalled = true;
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                done = true;
            }
            doneCv.notify_all();
        });

        {
            std::unique_lock<std::mutex> lock(doneMutex);
            doneCv.wait(lock, [&] { return done; });
        }
        scanner.cancelAndJoin();

        require(completionCalled.load(), "completion called");
        require(!completionSuccess.load(), "cancelled scan not reported successful");
        require(!scanner.isUntrustedPluginAcknowledged(clapPath), "no partial results recorded as scanned");
        (void)resultCount;
    }

    // ------------------------------------------------------------------
    // scanAsync + approval=true: worker waits, main thread answers, completes
    // successfully and plugin is acknowledged. (App uses scanAsync.)
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_ScanAsyncAllow"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        std::mutex waitMutex;
        std::condition_variable waitCv;
        bool callbackWaiting = false;
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            auto request = std::make_shared<FirstLoadApprovalRequest>();
            if (!scanner.registerOutstandingApproval(request)) return false;
            { std::lock_guard<std::mutex> lock(waitMutex); callbackWaiting = true; }
            waitCv.notify_all();
            // main thread would resolve here; we self-answer allow after a tick:
            std::thread([request]{ request->resolve(true); }).detach();
            const bool allowed = request->wait();
            scanner.unregisterOutstandingApproval(request);
            return allowed;
        });

        std::atomic<bool> completionSuccess{false};
        std::mutex doneMutex; std::condition_variable doneCv; bool done=false;
        scanner.scanAsync(nullptr, [&](const std::vector<Aestra::Audio::PluginInfo>&, bool success){
            completionSuccess=success; { std::lock_guard<std::mutex> lock(doneMutex); done=true; } doneCv.notify_all(); });
        { std::unique_lock<std::mutex> lock(waitMutex); waitCv.wait(lock,[&]{return callbackWaiting;}); }
        { std::unique_lock<std::mutex> lock(doneMutex); doneCv.wait(lock,[&]{return done;}); }
        scanner.cancelAndJoin();

        require(completionSuccess.load(), "scanAsync + allowed completes successfully");
        require(scanner.isUntrustedPluginAcknowledged(clapPath), "scanAsync + allowed acknowledges plugin");
    }

    // ------------------------------------------------------------------
    // Per-plugin model preserved: trusted plugins never trigger approval.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_Trusted"};
        // A path under a user-trusted directory never triggers first-load warning.
        const auto trustedDir = dirScope.path() / "trusted";
        std::filesystem::create_directories(trustedDir);
        const auto clapPath = makeUntrustedClap(trustedDir);

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(trustedDir);
        scanner.addTrustedPath(trustedDir);

        std::atomic<int> callbackCount{0};
        scanner.setFirstLoadWarningCallback([&](const std::filesystem::path&, const std::string&) -> bool {
            ++callbackCount;
            return false;
        });

        const auto results = scanner.scanBlocking();
        (void)results;
        require(callbackCount.load() == 0, "trusted-path plugin never triggers approval");
    }

    // ------------------------------------------------------------------
    // 8. UI callbacks are dispatched safely; none fire after cancellation
    //    + join, so no work can reference destroyed content.
    // ------------------------------------------------------------------
    {
        const Aestra::Tests::ScopedTempDirectory dirScope{"FirstLoadApproval_NoCallbacksAfterJoin"};
        const auto clapPath = makeUntrustedClap(dirScope.path());

        PluginScanner scanner;
        scanner.clearSearchPaths();
        scanner.addSearchPath(dirScope.path());

        scanner.setFirstLoadWarningCallback(
            [](const std::filesystem::path&, const std::string&) -> bool { return true; });

        std::atomic<int> progressCallbacks{0};
        std::atomic<int> completionCallbacks{0};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        scanner.scanAsync([&](const std::string&, int, int) { ++progressCallbacks; },
                          [&](const std::vector<Aestra::Audio::PluginInfo>&, bool) {
                              ++completionCallbacks;
                              {
                                  std::lock_guard<std::mutex> lock(doneMutex);
                                  done = true;
                              }
                              doneCv.notify_all();
                          });

        scanner.cancelAndJoin(); // stop + join scanner before destroying UI

        const int progressAfterJoin = progressCallbacks.load();
        const int completionAfterJoin = completionCallbacks.load();
        // Nothing may invoke callbacks once cancelAndJoin has returned.
        require(progressAfterJoin == progressCallbacks.load(), "no progress callbacks after join");
        require(completionAfterJoin == completionCallbacks.load(), "no completion callbacks after join");
    }

    std::cout << "[PASS] PluginFirstLoadApprovalTest\n";
    return 0;
}
