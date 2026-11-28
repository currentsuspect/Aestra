// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The hand-over between the helper's pipe thread and its main thread.
//
// The main thread owns the plugin and its editor (CLAP and VST3 want window code on the thread that loaded the plugin), so
// it also runs the plugin's timers and the window's events. A repaint can take longer than an audio block, and an audio block
// that waits behind it is late: the app then plays that block dry, which for a compressor is a click. So a second thread owns
// the pipe: it answers audio blocks itself, the moment they arrive, and passes every other command here for the main thread,
// waiting until the main thread has finished it. That keeps the order the app relies on (one command at a time, each answered
// before the next is read) while a repaint no longer holds an audio block back.
//
// One slot, because the app sends one command at a time. No plugin and no I/O here, so a test can drive it with two threads.

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace AestraHelper {

class CommandMailbox {
public:
    CommandMailbox() {
#ifndef _WIN32
        if (::pipe(m_wake) == 0) {
            for (const int fd : m_wake) {
                ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
                ::fcntl(fd, F_SETFD, FD_CLOEXEC);
            }
        } else {
            m_wake[0] = m_wake[1] = -1;
        }
#endif
    }
    ~CommandMailbox() {
#ifndef _WIN32
        for (const int fd : m_wake) {
            if (fd >= 0) ::close(fd);
        }
#endif
    }
    CommandMailbox(const CommandMailbox&) = delete;
    CommandMailbox& operator=(const CommandMailbox&) = delete;

    /// Pipe thread: hand @p line to the main thread and wait until it has finished it. False when the main thread is
    /// gone (the mailbox was closed), so the caller stops reading.
    bool postAndWait(const std::string& line) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_closed) return false;
        m_line = line;
        m_state = State::Posted;
        wake();
        m_cv.wait(lock, [&] { return m_state == State::Idle || m_closed; });
        return !m_closed;
    }

    /// Main thread: the posted command, if there is one. Call finish() once it has been answered.
    bool take(std::string& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Posted) return false;
        out = std::move(m_line);
        m_state = State::Running;
        return true;
    }

    /// Main thread: the command taken last has been answered; the pipe thread may read the next one.
    void finish() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state == State::Running) m_state = State::Idle;
        }
        m_cv.notify_all();
    }

    /// Pipe thread: no more commands (end of input). The main thread sees closed() once the slot is empty.
    void close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
        wake();
    }

    /// Main thread: input has ended and nothing is waiting to be taken.
    bool closed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed && m_state != State::Posted;
    }

#ifndef _WIN32
    /// A descriptor that becomes readable when a command is posted or the mailbox closes; add it to the main thread's poll.
    int wakeFd() const { return m_wake[0]; }
    /// True for the mailbox's own two descriptors: the helper made them itself, so a check for descriptors inherited from
    /// the app (TESTFDS, #710) must not count them.
    bool ownsFd(int fd) const { return fd >= 0 && (fd == m_wake[0] || fd == m_wake[1]); }
    void drainWake() const {
        char scratch[64];
        while (m_wake[0] >= 0 && ::read(m_wake[0], scratch, sizeof scratch) > 0) {
        }
    }
#else
    /// Windows has no pollable pipe here: the main thread waits on this with its own timers' limit.
    void waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wakeCv.wait_for(lock, timeout, [&] { return m_state == State::Posted || m_closed; });
    }
#endif

private:
    enum class State { Idle, Posted, Running };

    void wake() {
#ifndef _WIN32
        if (m_wake[1] >= 0) {
            const char byte = 1;
            const ssize_t ignored = ::write(m_wake[1], &byte, 1);
            (void)ignored; // a full pipe already means "wake up"
        }
#else
        m_wakeCv.notify_all();
#endif
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::string m_line;
    State m_state = State::Idle;
    bool m_closed = false;
#ifndef _WIN32
    int m_wake[2] = {-1, -1};
#else
    std::condition_variable m_wakeCv;
#endif
};

} // namespace AestraHelper
