// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// HttpClient response cap — a local throwaway server on loopback stands in for
// the provider. No external network.

#include "HttpClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

using namespace Aestra::MuseAgent;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) ++g_failures;
}

// Serves exactly one request: reads it, then writes `reply` (header + body
// pieces), stopping early if the client hangs up.
class OneShotServer {
public:
    OneShotServer(std::string head, size_t bodyBytes) : m_head(std::move(head)), m_bodyBytes(bodyBytes) {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(m_fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        socklen_t len = sizeof(a);
        ::getsockname(m_fd, reinterpret_cast<sockaddr*>(&a), &len);
        m_port = ntohs(a.sin_port);
        ::listen(m_fd, 1);
        m_thread = std::thread([this] { serve(); });
    }
    ~OneShotServer() {
        ::shutdown(m_fd, SHUT_RDWR);
        m_thread.join();
        ::close(m_fd);
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(m_port) + "/v1/x"; }

private:
    void serve() {
        const int c = ::accept(m_fd, nullptr, nullptr);
        if (c < 0) return;
        char buf[4096];
        std::string req;
        while (req.find("\r\n\r\n") == std::string::npos) {
            const auto n = ::recv(c, buf, sizeof(buf), 0);
            if (n <= 0) break;
            req.append(buf, static_cast<size_t>(n));
        }
        // Drain the small JSON body (Content-Length is tiny in these tests).
        ::send(c, m_head.data(), m_head.size(), MSG_NOSIGNAL);
        const std::string chunk(16384, 'a');
        size_t sent = 0;
        while (sent < m_bodyBytes) {
            const size_t n = std::min(chunk.size(), m_bodyBytes - sent);
            if (::send(c, chunk.data(), n, MSG_NOSIGNAL) <= 0) break;
            sent += n;
        }
        ::close(c);
    }
    std::string m_head;
    size_t m_bodyBytes;
    int m_fd = -1;
    uint16_t m_port = 0;
    std::thread m_thread;
};

} // namespace

int main() {
    // --- pure helpers ---
    {
        std::string out;
        check(appendWithinCap(out, "abcd", 4, 8) && out == "abcd", "append within cap");
        check(appendWithinCap(out, "efgh", 4, 8) && out == "abcdefgh", "append up to exactly the cap");
        check(!appendWithinCap(out, "i", 1, 8) && out == "abcdefgh", "append past cap refused, buffer untouched");
        std::string big(10000, 'x');
        check(truncateForMessage("short") == "short", "short text unchanged");
        const std::string cut = truncateForMessage(big);
        check(cut.size() < 2200 && cut.find("truncated, 10000 bytes total") != std::string::npos,
              "long text truncated with a marker");
    }

    // --- a small response passes ---
    {
        OneShotServer server("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n", 5);
        const auto r = httpPostJson(server.url(), {}, "{}", 10, 64 * 1024);
        check(r.error.empty() && r.status == 200 && r.body.size() == 5 && !r.bodyTooLarge,
              "response under the cap is delivered intact");
    }

    // --- declared Content-Length over the cap ---
    {
        OneShotServer server("HTTP/1.1 200 OK\r\nContent-Length: 5000000\r\nConnection: close\r\n\r\n",
                             5000000);
        const auto r = httpPostJson(server.url(), {}, "{}", 20, 64 * 1024);
        check(r.bodyTooLarge && !r.error.empty(), "oversized declared length is refused with an error");
        check(r.body.size() <= 64 * 1024, "buffered body never exceeds the cap (declared length)");
    }

    // --- no Content-Length: streams until close ---
    {
        OneShotServer server("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", 5000000);
        const auto r = httpPostJson(server.url(), {}, "{}", 20, 64 * 1024);
        check(r.bodyTooLarge && !r.error.empty(), "oversized undeclared stream is cut off with an error");
        check(r.body.size() <= 64 * 1024, "buffered body never exceeds the cap (stream)");
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
