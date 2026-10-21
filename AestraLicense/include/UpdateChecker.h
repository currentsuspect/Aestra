// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Runs checkForUpdate() in the background: once shortly after startup (not while the app is still loading), then daily,
// then sooner after a failure so a dropped connection does not mean waiting a day. checkNow() asks for one right away
// (Settings > Updates > "Check now"). The configuration is asked for before every check, so a channel or key changed in
// Settings applies to the next one. The callback runs on the checker's own thread: hand the result to the UI thread.

#include "UpdateClient.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace Aestra {
namespace License {

struct UpdateCheckTiming {
    std::chrono::milliseconds initialDelay{std::chrono::seconds(20)};
    std::chrono::milliseconds interval{std::chrono::hours(24)};
    std::chrono::milliseconds retryAfterFailure{std::chrono::hours(1)};
};

class UpdateChecker {
public:
    using Timing = UpdateCheckTiming;
    using ConfigProvider = std::function<UpdateConfig()>;
    using Callback = std::function<void(const UpdateCheckResult&)>;

    UpdateChecker(std::unique_ptr<IHttpTransport> transport, ConfigProvider config, Callback onResult, Timing timing = {});
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    /// Scheduled checks run only while this says yes (the user can turn automatic checks off); checkNow() ignores it.
    void setAutomaticAllowed(std::function<bool()> allowed);

    void start();
    void stop();
    /// Check as soon as the thread is free (does nothing if not started).
    void checkNow();
    /// The last answer; `status` is NetworkError with an empty detail until the first check has run.
    UpdateCheckResult lastResult() const;
    bool hasResult() const;
    /// How many checks have finished: a page waiting on "Check now" watches this change.
    uint64_t resultCount() const;

private:
    void run();

    std::unique_ptr<IHttpTransport> m_transport;
    ConfigProvider m_config;
    Callback m_onResult;
    Timing m_timing;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::thread m_thread;
    bool m_stop = false;
    bool m_checkRequested = false;
    std::function<bool()> m_automaticAllowed;
    bool m_hasResult = false;
    uint64_t m_resultCount = 0;
    UpdateCheckResult m_last;
};

} // namespace License
} // namespace Aestra
