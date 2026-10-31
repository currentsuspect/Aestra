// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The helper's command input: lines from the app on stdin, read in a way that lets the same thread also run a plugin's
// timers and editor between commands. A blocking std::getline would freeze the window while idle, and a window on a second
// thread would break plugins that insist their UI code runs on the thread that loaded them (CLAP's "main thread").

#ifndef _WIN32

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <vector>

namespace AestraHelper {

class LineReader {
public:
    /// Next command line (without its '\n'); false at end of input. @p addWaits(fds, timeoutMs) may append descriptors
    /// to wait on and lower the timeout; @p service() runs whatever became ready (a plugin editor, in practice) and is
    /// also run before every line is handed back, so an editor stays alive while commands keep arriving.
    template <class AddWaits, class Service>
    bool next(std::string& line, AddWaits&& addWaits, Service&& service) {
        for (;;) {
            service();
            const size_t newline = m_buffer.find('\n');
            if (newline != std::string::npos) {
                line.assign(m_buffer, 0, newline);
                m_buffer.erase(0, newline + 1);
                return true;
            }
            if (m_eof) {
                if (m_buffer.empty()) return false;
                line.swap(m_buffer); // a last line with no newline
                m_buffer.clear();
                return true;
            }
            std::vector<pollfd> fds;
            fds.push_back({STDIN_FILENO, POLLIN, 0});
            int timeoutMs = -1;
            addWaits(fds, timeoutMs);
            const int ready = poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
            if (ready < 0) {
                if (errno == EINTR) continue;
                m_eof = true;
                continue;
            }
            if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
                char chunk[65536];
                const ssize_t n = read(STDIN_FILENO, chunk, sizeof chunk);
                if (n > 0) m_buffer.append(chunk, static_cast<size_t>(n));
                else if (n == 0 || (errno != EINTR && errno != EAGAIN)) m_eof = true;
            }
        }
    }

private:
    std::string m_buffer;
    bool m_eof = false;
};

} // namespace AestraHelper

#else // _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace AestraHelper {

/// Windows (V82-H8): an anonymous pipe cannot be waited on, so a thread reads stdin and signals an event, and the
/// command thread waits on that event, the next plugin timer and its window messages at once
/// (MsgWaitForMultipleObjects), pumping the messages. Same contract as the POSIX reader: @p limitWait(timeoutMs) may
/// lower the timeout (-1: none), @p service() runs due timers and is also run before every line is handed back.
class LineReader {
public:
    LineReader() : m_shared(std::make_shared<Shared>()) {
        m_shared->ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        // Detached: at exit it may still be blocked in ReadFile, and a pipe read cannot be cancelled portably.
        // It holds its own reference to the shared state, so it never touches a destroyed reader.
        std::thread([shared = m_shared] {
            HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
            char chunk[65536];
            for (;;) {
                DWORD n = 0;
                const BOOL ok = ReadFile(in, chunk, sizeof chunk, &n, nullptr);
                {
                    std::lock_guard<std::mutex> lock(shared->mutex);
                    if (ok && n > 0)
                        shared->buffer.append(chunk, n);
                    else
                        shared->eof = true;
                }
                SetEvent(shared->ready);
                if (!ok || n == 0)
                    return;
            }
        }).detach();
    }

    template <class LimitWait, class Service>
    bool next(std::string& line, LimitWait&& limitWait, Service&& service) {
        for (;;) {
            service();
            {
                std::lock_guard<std::mutex> lock(m_shared->mutex);
                std::string& buffer = m_shared->buffer;
                const size_t newline = buffer.find('\n');
                if (newline != std::string::npos) {
                    line.assign(buffer, 0, newline);
                    buffer.erase(0, newline + 1);
                    return true;
                }
                if (m_shared->eof) {
                    if (buffer.empty())
                        return false;
                    line.swap(buffer); // a last line with no newline
                    buffer.clear();
                    return true;
                }
            }
            int timeoutMs = -1;
            limitWait(timeoutMs);
            MsgWaitForMultipleObjects(1, &m_shared->ready, FALSE,
                                      timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs), QS_ALLINPUT);
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

private:
    struct Shared {
        std::mutex mutex;
        std::string buffer;
        bool eof = false;
        HANDLE ready = nullptr; // auto-reset; left open for the process's life (the thread may outlive the reader)
    };
    std::shared_ptr<Shared> m_shared;
};

} // namespace AestraHelper

#endif
