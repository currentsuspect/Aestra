// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// A line reader over a file descriptor that can give up at a deadline, so an
// unanswered question cannot hold the process past its time budget.
// POSIX only: on Windows the timeout is ignored and the read blocks.

#include <string>

#ifdef _WIN32
#include <iostream>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace Aestra {
namespace MuseAgent {

class LineReader {
public:
    enum class Result {
        Line,     ///< a full line (newline stripped) is in the output
        Eof,      ///< the stream ended (closed stdin, no TTY) before a full line
        TimedOut, ///< no full line arrived within the timeout
    };

    explicit LineReader(int fd = 0) : m_fd(fd) {}

    /**
     * @brief Read one line, waiting at most @p timeoutMs (< 0 waits forever).
     *
     * Bytes of a partial line are kept across a TimedOut result, so a later
     * call continues the same line.
     */
    Result readLine(std::string& out, long timeoutMs) {
#ifdef _WIN32
        (void)timeoutMs;
        if (std::getline(std::cin, out)) return Result::Line;
        return Result::Eof;
#else
        for (;;) {
            const auto newline = m_pending.find('\n');
            if (newline != std::string::npos) {
                out = m_pending.substr(0, newline);
                m_pending.erase(0, newline + 1);
                if (!out.empty() && out.back() == '\r') out.pop_back();
                return Result::Line;
            }
            pollfd descriptor{m_fd, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, timeoutMs < 0 ? -1 : static_cast<int>(timeoutMs));
            if (ready == 0) return Result::TimedOut;
            if (ready < 0) return Result::Eof;
            char chunk[512];
            const auto got = ::read(m_fd, chunk, sizeof(chunk));
            if (got <= 0) {
                if (!m_pending.empty()) { // final line without a trailing newline
                    out = m_pending;
                    m_pending.clear();
                    return Result::Line;
                }
                return Result::Eof;
            }
            m_pending.append(chunk, static_cast<size_t>(got));
        }
#endif
    }

private:
    int m_fd;
    std::string m_pending;
};

} // namespace MuseAgent
} // namespace Aestra
