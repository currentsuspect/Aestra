// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseSocketServer Tests — the localhost JSONL entry point for live sessions.
// Drives a real loopback TCP connection end to end: connect, send requests,
// pump processPending() on this (the "main") thread, read responses.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Commands/MuseSocketServer.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using Aestra::Audio::CommandRegistry;
using Aestra::Audio::MuseService;
using Aestra::Audio::MuseSocketServer;
using Aestra::Audio::TrackManager;
using Aestra::JSON;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

SocketHandle connectLoopback(uint16_t port) {
#ifdef _WIN32
    WSADATA data{};
    ::WSAStartup(MAKEWORD(2, 2), &data);
#endif
    SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) return kInvalidSocket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
#ifdef _WIN32
        ::closesocket(sock);
#else
        ::close(sock);
#endif
        return kInvalidSocket;
    }
    return sock;
}

void sendAll(SocketHandle sock, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto n = ::send(sock, data.data() + sent,
#ifdef _WIN32
                              static_cast<int>(data.size() - sent),
#else
                              data.size() - sent,
#endif
                              0);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

// Pump the server on this thread until `lines` responses arrived (or timeout).
std::string pumpAndRead(MuseSocketServer& server, MuseService& service, SocketHandle sock,
                        size_t lines, int timeoutMs = 5000) {
    std::string received;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t newlines = 0;
    while (std::chrono::steady_clock::now() < deadline && newlines < lines) {
        server.processPending(service);
        char chunk[4096];
#ifdef _WIN32
        u_long nonBlocking = 1;
        ::ioctlsocket(sock, FIONBIO, &nonBlocking);
        const auto n = ::recv(sock, chunk, sizeof(chunk), 0);
#else
        const auto n = ::recv(sock, chunk, sizeof(chunk), MSG_DONTWAIT);
#endif
        if (n > 0) {
            received.append(chunk, static_cast<size_t>(n));
            newlines = 0;
            for (char c : received) {
                if (c == '\n') ++newlines;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return received;
}

} // namespace

int main() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    CommandRegistry::initialize(trackManager.get());
    MuseService service(trackManager.get(), nullptr);

    MuseSocketServer server;
    std::string error;

    // Ephemeral port keeps the test parallel-safe.
    check(server.start(0, error), "server starts on an ephemeral port (" + error + ")");
    check(server.port() != 0, "bound port resolved");
    check(server.isRunning(), "server reports running");

    SocketHandle client = connectLoopback(server.port());
    check(client != kInvalidSocket, "loopback client connects");

    // Two requests in one write — line framing must split them.
    sendAll(client, "{\"id\":1,\"verb\":\"add_track\",\"args\":{\"name\":\"Live\"}}\n"
                    "{\"id\":2,\"verb\":\"list_tracks\"}\n");
    const std::string received = pumpAndRead(server, service, client, 2);

    // Split responses and check them.
    size_t split = received.find('\n');
    check(split != std::string::npos, "first response line received");
    if (split != std::string::npos) {
        JSON first = JSON::parse(received.substr(0, split));
        check(first["id"].asNumber() == 1.0 && first["status"].asString() == "ok",
              "add_track over the socket ok");
        std::string secondLine = received.substr(split + 1);
        if (!secondLine.empty() && secondLine.back() == '\n') secondLine.pop_back();
        JSON second = JSON::parse(secondLine);
        check(second["id"].asNumber() == 2.0, "responses correlate by id");
        check(second["result"]["tracks"].size() == 1 &&
                  second["result"]["tracks"][0]["name"].asString() == "Live",
              "socket edit visible in the same session");
    }

    // The socket edit went through the shared history: it is undoable.
    check(trackManager->getCommandHistory().canUndo(),
          "socket edits land in the shared undo history");

    // Malformed input never kills the connection.
    sendAll(client, "this is not json\n{\"id\":3,\"verb\":\"get_session_state\"}\n");
    const std::string errorRun = pumpAndRead(server, service, client, 2);
    check(errorRun.find("parse_error") != std::string::npos,
          "garbage line answered with parse_error");
    check(errorRun.find("\"id\":3") != std::string::npos ||
              errorRun.find("\"id\": 3") != std::string::npos,
          "connection survives garbage and answers the next request");

    // A second client works alongside the first.
    SocketHandle client2 = connectLoopback(server.port());
    check(client2 != kInvalidSocket, "second client connects");
    sendAll(client2, "{\"id\":9,\"verb\":\"list_tracks\"}\n");
    const std::string second = pumpAndRead(server, service, client2, 1);
    check(second.find("\"id\":9") != std::string::npos ||
              second.find("\"id\": 9") != std::string::npos,
          "second client gets its own response");

#ifdef _WIN32
    ::closesocket(client);
    ::closesocket(client2);
#else
    ::close(client);
    ::close(client2);
#endif
    server.stop();
    check(!server.isRunning(), "server stops cleanly");

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
