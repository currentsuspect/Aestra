// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The helper's command input (POSIX): lines from the app on stdin, read in a way that lets the same thread also run a
// plugin's editor between commands. A blocking std::getline would freeze the window while idle, and a window on a second
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

#endif
