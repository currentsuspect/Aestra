// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseSocketClient host handling — endpoint splitting (pure) and name/IPv6
// resolution against a throwaway loopback listener. No external network.

#include "MuseSocketClient.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

using namespace Aestra::MuseAgent;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) ++g_failures;
}

// Listens on loopback (v4 or v6), returns the port, or 0 if unavailable.
struct Listener {
    int fd = -1;
    uint16_t port = 0;
    explicit Listener(bool v6) {
        fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        int ok = 0;
        if (v6) {
            sockaddr_in6 a{};
            a.sin6_family = AF_INET6;
            a.sin6_addr = in6addr_loopback;
            ok = ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            socklen_t len = sizeof(a);
            if (ok == 0) { ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len); port = ntohs(a.sin6_port); }
        } else {
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            ok = ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
            socklen_t len = sizeof(a);
            if (ok == 0) { ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len); port = ntohs(a.sin_port); }
        }
        if (ok != 0 || ::listen(fd, 4) != 0) { ::close(fd); fd = -1; port = 0; }
    }
    ~Listener() { if (fd >= 0) ::close(fd); }
};

} // namespace

int main() {
    // --- splitHostPort ---
    {
        auto r = splitHostPort("41952", "127.0.0.1");
        check(r.ok && r.host == "127.0.0.1" && r.portText == "41952", "bare port keeps default host");
        r = splitHostPort("localhost:41952", "127.0.0.1");
        check(r.ok && r.host == "localhost" && r.portText == "41952", "host:port");
        r = splitHostPort("10.0.0.2:9", "127.0.0.1");
        check(r.ok && r.host == "10.0.0.2" && r.portText == "9", "ipv4:port");
        r = splitHostPort("[::1]:41952", "127.0.0.1");
        check(r.ok && r.host == "::1" && r.portText == "41952", "[v6]:port");
        r = splitHostPort("[fe80::1]:7", "127.0.0.1");
        check(r.ok && r.host == "fe80::1" && r.portText == "7", "[longer v6]:port");
        check(!splitHostPort("::1:41952", "127.0.0.1").ok, "unbracketed v6 rejected");
        check(!splitHostPort("[::1]", "127.0.0.1").ok, "bracketed v6 without port rejected");
        check(!splitHostPort("[::1", "127.0.0.1").ok, "unclosed bracket rejected");
        check(!splitHostPort("", "127.0.0.1").ok, "empty rejected");
        check(!splitHostPort(":41952", "127.0.0.1").ok, "empty host rejected");
        check(!splitHostPort("host:", "127.0.0.1").ok, "empty port rejected");
    }

    // --- connect resolution ---
    {
        Listener v4(false);
        check(v4.port != 0, "v4 loopback listener up");
        MuseSocketClient numeric;
        std::string error;
        check(numeric.connect("127.0.0.1", v4.port, error), "numeric IPv4 still connects");

        MuseSocketClient named;
        error.clear();
        check(named.connect("localhost", v4.port, error), "localhost connects: " + error);

        MuseSocketClient bogus;
        error.clear();
        check(!bogus.connect("no-such-host.invalid", v4.port, error) && !error.empty(),
              "unresolvable name fails with a message: " + error);
    }
    {
        Listener v6(true);
        if (v6.port == 0) {
            std::cout << "SKIP: no IPv6 loopback on this host\n";
        } else {
            MuseSocketClient client;
            std::string error;
            check(client.connect("::1", v6.port, error), "::1 connects: " + error);
        }
    }

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
