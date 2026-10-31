// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-H3: what a plugin's editor needs from the thread that owns its window. On Linux a plugin editor is not self
// driving: it asks the host to call it when a file descriptor becomes readable (X11 connection, a socket) and every N
// milliseconds (animation, meters). VST3 does that through Linux::IRunLoop, CLAP through its posix-fd-support and
// timer-support extensions. Both end up here: this class only remembers who asked for what; the thread that owns the
// window (the sandbox helper's GUI thread) asks it which descriptors to poll and when the next timer is due, and calls
// back into it. No SDK, no X11, so it is testable on its own.
//
// Registration may come from any thread; handlers are called OUTSIDE the lock, so a handler can add or remove
// registrations (a timer that cancels itself is routine).

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

namespace Aestra {
namespace Audio {

class EditorLoopPump {
public:
    using Clock = std::chrono::steady_clock;

    /// Call @p onReadable whenever @p fd is readable. Returns a token for removeFd(), always > 0.
    int addFd(int fd, std::function<void()> onReadable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int token = ++m_nextToken;
        m_fds.push_back({token, fd, std::move(onReadable)});
        return token;
    }
    void removeFd(int token) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fds.erase(std::remove_if(m_fds.begin(), m_fds.end(), [&](const FdEntry& e) { return e.token == token; }),
                    m_fds.end());
    }

    /// Call @p onTimer every @p interval, first one interval from @p now. Returns a token for removeTimer(), > 0.
    int addTimer(std::chrono::milliseconds interval, std::function<void()> onTimer, Clock::time_point now = Clock::now()) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const int token = ++m_nextToken;
        const auto step = std::max(interval, std::chrono::milliseconds(1));
        m_timers.push_back({token, step, now + step, std::move(onTimer)});
        return token;
    }
    void removeTimer(int token) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(), [&](const TimerEntry& e) { return e.token == token; }),
                       m_timers.end());
    }

    /// The descriptors to poll (each once, even when several handlers share it).
    std::vector<int> fds() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<int> out;
        for (const auto& entry : m_fds)
            if (std::find(out.begin(), out.end(), entry.fd) == out.end()) out.push_back(entry.fd);
        return out;
    }

    /// Call every handler registered for @p fd. Returns how many ran.
    int dispatchFd(int fd) {
        std::vector<std::function<void()>> due;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& entry : m_fds)
                if (entry.fd == fd) due.push_back(entry.handler);
        }
        for (auto& handler : due) handler();
        return static_cast<int>(due.size());
    }

    /// Milliseconds until the next timer is due (0 when one already is), or -1 when there are no timers: the poll timeout.
    int msUntilNextTimer(Clock::time_point now = Clock::now()) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_timers.empty()) return -1;
        auto soonest = m_timers.front().due;
        for (const auto& timer : m_timers) soonest = std::min(soonest, timer.due);
        if (soonest <= now) return 0;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(soonest - now).count();
        return static_cast<int>(ms) + 1; // round up: waking a hair early would spin instead of firing
    }

    /// Run every timer that is due at @p now, then schedule each one interval later (from now, so a long stall fires
    /// once, not once per missed interval). Returns how many ran.
    int fireDueTimers(Clock::time_point now = Clock::now()) {
        std::vector<std::pair<int, std::function<void()>>> due;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& timer : m_timers) {
                if (timer.due > now) continue;
                due.emplace_back(timer.token, timer.handler);
                timer.due = now + timer.interval;
            }
        }
        for (auto& entry : due) {
            {   // skip one that an earlier handler in this pass has just removed
                std::lock_guard<std::mutex> lock(m_mutex);
                const bool stillThere = std::any_of(m_timers.begin(), m_timers.end(),
                                                    [&](const TimerEntry& t) { return t.token == entry.first; });
                if (!stillThere) continue;
            }
            entry.second();
        }
        return static_cast<int>(due.size());
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_fds.empty() && m_timers.empty();
    }

private:
    struct FdEntry {
        int token;
        int fd;
        std::function<void()> handler;
    };
    struct TimerEntry {
        int token;
        std::chrono::milliseconds interval;
        Clock::time_point due;
        std::function<void()> handler;
    };
    mutable std::mutex m_mutex;
    std::vector<FdEntry> m_fds;
    std::vector<TimerEntry> m_timers;
    int m_nextToken = 0;
};

} // namespace Audio
} // namespace Aestra
