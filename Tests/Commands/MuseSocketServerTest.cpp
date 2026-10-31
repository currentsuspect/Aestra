// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MuseSocketServer Tests — the localhost JSONL entry point for live sessions.
// Drives a real loopback TCP connection end to end: connect, send requests,
// pump processPending() on this (the "main") thread, read responses.

#include "Commands/CommandRegistry.h"
#include "Commands/MuseService.h"
#include "Commands/MuseSocketServer.h"
#include "Models/TrackManager.h"
#include "MuseSocketToken.h"

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
#include <sys/stat.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
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

#if defined(MSG_NOSIGNAL)
constexpr int kNoSignal = MSG_NOSIGNAL; // sending to a server-closed client must not kill the test
#else
constexpr int kNoSignal = 0;
#endif

int g_failures = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "PASS: " << label << "\n";
    } else {
        std::cout << "FAIL: " << label << "\n";
        ++g_failures;
    }
}

#if defined(_WIN32)
constexpr const char* kTokenDirEnv = "LOCALAPPDATA";
void setEnvVar(const char* name, const std::string& value) { _putenv_s(name, value.c_str()); }
#elif defined(__APPLE__)
constexpr const char* kTokenDirEnv = "HOME";
void setEnvVar(const char* name, const std::string& value) { ::setenv(name, value.c_str(), 1); }
#else
constexpr const char* kTokenDirEnv = "XDG_RUNTIME_DIR";
void setEnvVar(const char* name, const std::string& value) { ::setenv(name, value.c_str(), 1); }
#endif

MuseSocketServer* g_server = nullptr;
MuseService* g_service = nullptr;
std::string g_token;

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
                              kNoSignal);
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

// Present the token the way a real client does and expect the server to accept it.
bool authenticate(SocketHandle sock, const std::string& token) {
    sendAll(sock, Aestra::MuseToken::authLine(token) + "\n");
    const std::string reply = pumpAndRead(*g_server, *g_service, sock, 1);
    return reply.find("\"authenticated\":true") != std::string::npos ||
           reply.find("\"authenticated\": true") != std::string::npos;
}

SocketHandle connectAuthed(uint16_t port) {
    SocketHandle sock = connectLoopback(port);
    if (sock != kInvalidSocket && !authenticate(sock, g_token)) {
        std::cout << "FAIL: could not authenticate a test client\n";
        ++g_failures;
    }
    return sock;
}

void closeClient(SocketHandle sock) {
#ifdef _WIN32
    ::closesocket(sock);
#else
    ::close(sock);
#endif
}

// A client with a tiny receive buffer, so a handful of unread responses fill
// the kernel path and the server's send() has nowhere to go.
SocketHandle connectLoopbackSmallRcvBuf(uint16_t port) {
#ifdef _WIN32
    WSADATA data{};
    ::WSAStartup(MAKEWORD(2, 2), &data);
#endif
    SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) return kInvalidSocket;
    int rcvbuf = 4096;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf),
                 sizeof(rcvbuf));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeClient(sock);
        return kInvalidSocket;
    }
    return sock;
}

// Non-blocking send of as much as the kernel takes; never parks the test.
size_t trySend(SocketHandle sock, const std::string& data) {
#ifdef _WIN32
    u_long nonBlocking = 1;
    ::ioctlsocket(sock, FIONBIO, &nonBlocking);
    const auto n = ::send(sock, data.data(), static_cast<int>(data.size()), 0);
#else
    const auto n = ::send(sock, data.data(), data.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
#endif
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// True once the server has closed this client (recv returns 0 or an error
// other than "would block").
bool serverClosed(SocketHandle sock) {
    char chunk[4096];
#ifdef _WIN32
    u_long nonBlocking = 1;
    ::ioctlsocket(sock, FIONBIO, &nonBlocking);
    const auto n = ::recv(sock, chunk, sizeof(chunk), 0);
    if (n == 0) return true;
    return n < 0 && ::WSAGetLastError() != WSAEWOULDBLOCK;
#else
    const auto n = ::recv(sock, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (n == 0) return true;
    return n < 0 && errno != EAGAIN && errno != EWOULDBLOCK;
#endif
}

// Run stop() on a helper thread. A hang must fail the test, not hang it.
bool stopsWithin(MuseSocketServer& server, int timeoutMs) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([&server, done]() {
        server.stop();
        done->store(true);
    }).detach();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline && !done->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done->load();
}

} // namespace

int main() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->getUnitManager().setPatternManager(&trackManager->getPatternManager());
    CommandRegistry::initialize();
    MuseService service(trackManager.get(), nullptr);

    MuseSocketServer server;
    std::string error;
    g_server = &server;
    g_service = &service;

    // Point the token folder at a private temp dir so the test never touches (or trusts) a real one.
    namespace fs = std::filesystem;
    std::error_code fsError;
    const fs::path tokenRoot =
        fs::temp_directory_path(fsError) / ("aestra-muse-token-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(tokenRoot, fsError);
    fs::create_directories(tokenRoot, fsError);
    setEnvVar(kTokenDirEnv, tokenRoot.string());

    // Ephemeral port keeps the test parallel-safe.
    check(server.start(0, error), "server starts on an ephemeral port (" + error + ")");
    check(server.port() != 0, "bound port resolved");
    check(server.isRunning(), "server reports running");


    // --- FD-64 / #1266: token authentication -------------------------------------------------------------------
    const fs::path tokenFile =
        Aestra::MuseToken::candidateDirectories().front() / Aestra::MuseToken::tokenFileName(server.port());
    check(fs::exists(tokenFile, fsError), "token file exists while the server runs (" + tokenFile.string() + ")");
#ifndef _WIN32
    {
        struct stat st {};
        const bool statted = ::stat(tokenFile.c_str(), &st) == 0;
        check(statted && (st.st_mode & 0777) == 0600, "token file is mode 0600");
        struct stat dst {};
        const bool dirStatted = ::stat(tokenFile.parent_path().c_str(), &dst) == 0;
        check(dirStatted && (dst.st_mode & 0077) == 0, "token folder is closed to group and others");
    }
#endif
    std::string tokenError;
    check(Aestra::MuseToken::loadToken(server.port(), "", g_token, tokenError),
          "client helper reads the token file (" + tokenError + ")");
    check(Aestra::MuseToken::isWellFormedToken(g_token), "token is 256 bits of lowercase hex");

    {
        // No auth line at all: the request is refused and nothing executes.
        SocketHandle stranger = connectLoopback(server.port());
        sendAll(stranger, "{\"id\":77,\"verb\":\"add_track\",\"args\":{\"name\":\"Intruder\"}}\n");
        const std::string reply = pumpAndRead(server, service, stranger, 1);
        check(reply.find("\"unauthorized\"") != std::string::npos,
              "request without auth is answered unauthorized");
        check(trackManager->getTrackCount() == 0, "unauthenticated add_track did not run");
        closeClient(stranger);

        // Wrong token: refused, and a later valid-looking verb still does not run.
        SocketHandle guesser = connectLoopback(server.port());
        std::string wrong = g_token;
        wrong[0] = wrong[0] == '0' ? '1' : '0';
        sendAll(guesser, Aestra::MuseToken::authLine(wrong) + "\n");
        const std::string wrongReply = pumpAndRead(server, service, guesser, 1);
        check(wrongReply.find("\"unauthorized\"") != std::string::npos, "wrong token is refused");
        check(wrongReply.find("\"authenticated\"") == std::string::npos, "wrong token never reports authenticated");
        sendAll(guesser, "{\"id\":78,\"verb\":\"add_track\",\"args\":{\"name\":\"Guess\"}}\n");
        pumpAndRead(server, service, guesser, 1, 500);
        check(trackManager->getTrackCount() == 0, "add_track after a wrong token did not run");
        closeClient(guesser);

        // Repeated failures close the connection.
        SocketHandle hammer = connectLoopback(server.port());
        for (int i = 0; i < 6; ++i) sendAll(hammer, "{\"id\":1,\"verb\":\"list_tracks\"}\n");
        bool closed = false;
        const auto closeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < closeDeadline && !closed) {
            closed = serverClosed(hammer);
            if (!closed) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(closed, "server closes a client that keeps failing to authenticate");
        closeClient(hammer);

        // A malformed first line is a failure too, not a free pass.
        SocketHandle sloppy = connectLoopback(server.port());
        sendAll(sloppy, "not json at all\n");
        const std::string sloppyReply = pumpAndRead(server, service, sloppy, 1);
        check(sloppyReply.find("\"unauthorized\"") != std::string::npos, "garbage first line is unauthorized");
        closeClient(sloppy);
    }

    SocketHandle client = connectAuthed(server.port());
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
    SocketHandle client2 = connectAuthed(server.port());
    check(client2 != kInvalidSocket, "second client connects");
    sendAll(client2, "{\"id\":9,\"verb\":\"list_tracks\"}\n");
    const std::string second = pumpAndRead(server, service, client2, 1);
    check(second.find("\"id\":9") != std::string::npos ||
              second.find("\"id\": 9") != std::string::npos,
          "second client gets its own response");

    // --- Backpressure (#1271): a flood never runs in one frame, order holds. ---
    SocketHandle flooder = connectAuthed(server.port());
    constexpr int kFlood = 200;
    std::string flood;
    for (int i = 1; i <= kFlood; ++i) {
        flood += "{\"id\":" + std::to_string(1000 + i) + ",\"verb\":\"list_tracks\"}\n";
    }
    sendAll(flooder, flood);
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // let the IO thread queue it
    const size_t firstFrame = server.processPending(service);
    check(firstFrame > 0 && firstFrame <= 64, "one frame handles at most 64 requests (got " +
                                                  std::to_string(firstFrame) + ")");
    const std::string floodReplies = pumpAndRead(server, service, flooder, kFlood, 10000);
    {
        int next = 1001;
        bool ordered = true;
        size_t pos = 0;
        while (pos < floodReplies.size()) {
            const size_t nl = floodReplies.find('\n', pos);
            if (nl == std::string::npos) break;
            JSON line = JSON::parse(floodReplies.substr(pos, nl - pos));
            if (static_cast<int>(line["id"].asNumber()) != next++) ordered = false;
            pos = nl + 1;
        }
        check(ordered && next == 1001 + kFlood, "every flooded request answered, in order");
    }
    closeClient(flooder);

    // --- Backpressure (#1265): a client that never reads cannot wedge the server. ---
    // Make responses sizeable so unread output outgrows the 1 MiB backlog cap.
    for (int i = 0; i < 40; ++i) {
        sendAll(client2, "{\"id\":" + std::to_string(50 + i) +
                             ",\"verb\":\"add_track\",\"args\":{\"name\":\"Padding track " +
                             std::to_string(i) + "\"}}\n");
    }
    pumpAndRead(server, service, client2, 40);

    SocketHandle stuck = connectLoopbackSmallRcvBuf(server.port());
    if (stuck != kInvalidSocket) authenticate(stuck, g_token);
    check(stuck != kInvalidSocket, "non-reading client connects");
    std::string stuckRequest = "{\"id\":7,\"verb\":\"list_tracks\"}\n";
    bool stuckDropped = false;
    bool otherClientServed = true;
    const auto stuckDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    int probeId = 5000;
    while (std::chrono::steady_clock::now() < stuckDeadline && !stuckDropped) {
        for (int i = 0; i < 20; ++i) trySend(stuck, stuckRequest);
        server.processPending(service);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Probe: the well-behaved client must keep getting answers meanwhile.
        if (probeId % 5 == 0) {
            const std::string probe = "{\"id\":" + std::to_string(probeId) +
                                      ",\"verb\":\"get_session_state\"}\n";
            sendAll(client2, probe);
            const std::string reply = pumpAndRead(server, service, client2, 1, 3000);
            if (reply.find(std::to_string(probeId)) == std::string::npos) otherClientServed = false;
        }
        ++probeId;
        // serverClosed() reads, which would also drain the buffer; that is the
        // point: only a server-side close ends the stream with 0/error, and
        // the reader below only runs once the backlog has been given time to build.
        if (probeId > 5000 + 60) {
            // Stop sending, then drain: a capped-and-dropped client sees EOF.
            const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < drainDeadline) {
                server.processPending(service);
                if (serverClosed(stuck)) { stuckDropped = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            break;
        }
    }
    check(otherClientServed, "other client keeps being served while one never reads");
    check(stuckDropped, "client whose unread output exceeds the backlog cap is disconnected");

    // The stuck client is still connected (and not reading) when stop() runs.
    check(stopsWithin(server, 3000), "stop() returns promptly with a non-reading client attached");
    closeClient(stuck);
    closeClient(client);
    closeClient(client2);
    if (server.isRunning()) {
        std::cout << "stop() hung; aborting\n";
        std::_Exit(1);
    }
    check(!server.isRunning(), "server stops cleanly");
    check(!fs::exists(tokenFile, fsError), "token file is removed when the server stops");
    fs::remove_all(tokenRoot, fsError);

    std::cout << (g_failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(g_failures))
              << std::endl;
    return g_failures == 0 ? 0 : 1;
}
