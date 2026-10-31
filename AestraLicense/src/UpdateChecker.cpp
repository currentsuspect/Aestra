// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateChecker.h"

namespace Aestra {
namespace License {

UpdateChecker::UpdateChecker(std::unique_ptr<IHttpTransport> transport, ConfigProvider config, Callback onResult, Timing timing)
    : m_transport(std::move(transport)), m_config(std::move(config)), m_onResult(std::move(onResult)), m_timing(timing) {}

UpdateChecker::~UpdateChecker() { stop(); }

void UpdateChecker::setAutomaticAllowed(std::function<bool()> allowed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_automaticAllowed = std::move(allowed);
}

void UpdateChecker::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_thread.joinable()) return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
}

void UpdateChecker::stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void UpdateChecker::checkNow() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_checkRequested = true;
    }
    m_wake.notify_all();
}

UpdateCheckResult UpdateChecker::lastResult() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last;
}

uint64_t UpdateChecker::resultCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_resultCount;
}

bool UpdateChecker::hasResult() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_hasResult;
}

void UpdateChecker::run() {
    std::chrono::milliseconds wait = m_timing.initialDelay;
    for (;;) {
        bool manual = false;
        std::function<bool()> allowed;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, wait, [this] { return m_stop || m_checkRequested; });
            if (m_stop) return;
            manual = m_checkRequested;
            m_checkRequested = false;
            allowed = m_automaticAllowed;
        }
        if (!manual && allowed && !allowed()) { // automatic checks are off: look again at the usual time, touching nothing
            wait = m_timing.interval;
            continue;
        }

        const UpdateConfig config = m_config();
        const UpdateCheckResult result = checkForUpdate(*m_transport, config);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_last = result;
            m_hasResult = true;
            ++m_resultCount;
        }
        if (m_onResult) m_onResult(result);

        const bool failed = result.status == UpdateCheckStatus::NetworkError;
        wait = failed ? m_timing.retryAfterFailure : m_timing.interval;
    }
}

} // namespace License
} // namespace Aestra
