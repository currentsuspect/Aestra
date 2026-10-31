// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/MuseSocketServer.h"

#include "Commands/MuseService.h"
#include "AestraLog.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {

constexpr size_t kMaxRequestsPerFrame = 64;       // processPending() bound

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

// Every accepted socket is non-blocking so no send()/recv() can park the IO thread.
bool setNonBlocking(SocketHandle socket) {
#ifdef _WIN32
    u_long enable = 1;
    return ::ioctlsocket(socket, FIONBIO, &enable) == 0;
#else
    const int flags = ::fcntl(socket, F_GETFL, 0);
    return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

// True when the last socket call failed only because it would have blocked.
bool wouldBlock() {
#ifdef _WIN32
    return ::WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL; // a vanished client must not raise SIGPIPE
#else
constexpr int kSendFlags = 0;
#endif

constexpr size_t kMaxLineBytes = 1 << 20;         // one request line
constexpr size_t kMaxClientWriteBytes = 1 << 20;  // unsent responses per client
constexpr size_t kMaxInboxRequests = 256;         // queued, not yet executed

bool initSockets(std::string& outError) {
#ifdef _WIN32
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, []() {
        WSADATA data{};
        result = ::WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (result != 0) {
        outError = "WSAStartup failed: " + std::to_string(result);
        return false;
    }
#else
    (void)outError;
#endif
    return true;
}

} // namespace

struct MuseSocketServer::Impl {
    SocketHandle listenSocket = kInvalidSocket;
    uint16_t boundPort = 0;
    std::thread ioThread;
    std::atomic<bool> running{false};

    struct Client {
        SocketHandle socket = kInvalidSocket;
        std::string readBuffer;
        std::string writeBuffer; // IO-thread only
    };

    // IO-thread state
    std::unordered_map<uint64_t, Client> clients;
    uint64_t nextClientId = 1;

    // Cross-thread queues
    std::mutex mutex;
    std::deque<std::pair<uint64_t, std::string>> inbox;  // client -> request line
    std::deque<std::pair<uint64_t, std::string>> outbox; // client -> response line

    void ioLoop() {
        while (running.load(std::memory_order_relaxed)) {
            // Backpressure (#1271): while the inbox is full, stop reading from
            // every client. Unread bytes stay in the kernel receive buffer, so
            // a flooding client is slowed by TCP rather than by our memory.
            bool inboxHasRoom;
            {
                std::lock_guard<std::mutex> lock(mutex);
                inboxHasRoom = inbox.size() < kMaxInboxRequests;
            }

            fd_set readSet;
            fd_set writeSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_SET(listenSocket, &readSet);
            SocketHandle maxFd = listenSocket;
            for (const auto& [clientId, client] : clients) {
                if (inboxHasRoom) FD_SET(client.socket, &readSet);
                // Wake as soon as a client with pending output can take more.
                if (!client.writeBuffer.empty()) FD_SET(client.socket, &writeSet);
                if (client.socket > maxFd) maxFd = client.socket;
            }

            // Short timeout so queued responses and stop() are picked up
            // promptly even when no traffic arrives. Every socket is
            // non-blocking, so nothing below can park the thread and stop()
            // is always observed within one timeout.
            timeval timeout{};
            timeout.tv_usec = 50 * 1000;
            const int ready =
                ::select(static_cast<int>(maxFd + 1), &readSet, &writeSet, nullptr, &timeout);
            if (!running.load(std::memory_order_relaxed)) break;

            if (ready > 0 && FD_ISSET(listenSocket, &readSet)) {
                sockaddr_in remote{};
#ifdef _WIN32
                int remoteLen = sizeof(remote);
#else
                socklen_t remoteLen = sizeof(remote);
#endif
                SocketHandle accepted =
                    ::accept(listenSocket, reinterpret_cast<sockaddr*>(&remote), &remoteLen);
                if (accepted != kInvalidSocket) {
                    if (setNonBlocking(accepted)) {
                        Client client;
                        client.socket = accepted;
                        clients.emplace(nextClientId++, std::move(client));
                    } else {
                        closeSocket(accepted);
                    }
                }
            }

            std::vector<uint64_t> disconnected;
            for (auto& [clientId, client] : clients) {
                if (inboxHasRoom && ready > 0 && FD_ISSET(client.socket, &readSet)) {
                    char chunk[4096];
                    const auto received = ::recv(client.socket, chunk, sizeof(chunk), 0);
                    if (received == 0 || (received < 0 && !wouldBlock())) {
                        disconnected.push_back(clientId);
                        continue;
                    }
                    if (received > 0) client.readBuffer.append(chunk, static_cast<size_t>(received));
                }

                // Move complete lines to the inbox while it has room; any left
                // over stay in readBuffer (bounded below) for a later pass.
                size_t newline;
                while ((newline = client.readBuffer.find('\n')) != std::string::npos) {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (inbox.size() >= kMaxInboxRequests) break;
                        std::string line = client.readBuffer.substr(0, newline);
                        client.readBuffer.erase(0, newline + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (!line.empty()) inbox.emplace_back(clientId, std::move(line));
                    }
                }
                // A runaway line without a newline must not grow forever.
                if (client.readBuffer.size() > kMaxLineBytes) {
                    disconnected.push_back(clientId);
                }
            }

            // Deliver queued responses.
            {
                std::deque<std::pair<uint64_t, std::string>> toWrite;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    toWrite.swap(outbox);
                }
                for (auto& [clientId, response] : toWrite) {
                    auto it = clients.find(clientId);
                    if (it == clients.end()) continue; // client already gone
                    it->second.writeBuffer += response;
                    // A client that will not read must not grow memory forever.
                    if (it->second.writeBuffer.size() > kMaxClientWriteBytes) {
                        disconnected.push_back(clientId);
                    }
                }
            }
            for (auto& [clientId, client] : clients) {
                while (!client.writeBuffer.empty()) {
                    const auto sent = ::send(client.socket, client.writeBuffer.data(),
#ifdef _WIN32
                                             static_cast<int>(client.writeBuffer.size()),
#else
                                             client.writeBuffer.size(),
#endif
                                             kSendFlags);
                    if (sent < 0 && wouldBlock()) break; // keep the rest for next time
                    if (sent <= 0) {
                        disconnected.push_back(clientId);
                        break;
                    }
                    client.writeBuffer.erase(0, static_cast<size_t>(sent));
                }
            }

            for (uint64_t clientId : disconnected) {
                auto it = clients.find(clientId);
                if (it != clients.end()) {
                    closeSocket(it->second.socket);
                    clients.erase(it);
                }
            }
        }

        for (auto& [clientId, client] : clients) {
            closeSocket(client.socket);
        }
        clients.clear();
    }
};

MuseSocketServer::MuseSocketServer() : m_impl(std::make_unique<Impl>()) {}

MuseSocketServer::~MuseSocketServer() {
    stop();
}

bool MuseSocketServer::start(uint16_t port, std::string& outError) {
    if (m_impl->running.load(std::memory_order_relaxed)) {
        outError = "already running";
        return false;
    }
    if (!initSockets(outError)) return false;

    m_impl->listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_impl->listenSocket == kInvalidSocket) {
        outError = "socket() failed";
        return false;
    }

    // Loopback only: Muse is a local control surface, never a network service.
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int reuse = 1;
    ::setsockopt(m_impl->listenSocket, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (::bind(m_impl->listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
        0) {
        outError = "bind to 127.0.0.1:" + std::to_string(port) + " failed";
        closeSocket(m_impl->listenSocket);
        m_impl->listenSocket = kInvalidSocket;
        return false;
    }
    if (::listen(m_impl->listenSocket, 4) != 0) {
        outError = "listen failed";
        closeSocket(m_impl->listenSocket);
        m_impl->listenSocket = kInvalidSocket;
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int boundLen = sizeof(bound);
#else
    socklen_t boundLen = sizeof(bound);
#endif
    if (::getsockname(m_impl->listenSocket, reinterpret_cast<sockaddr*>(&bound), &boundLen) ==
        0) {
        m_impl->boundPort = ntohs(bound.sin_port);
    } else {
        m_impl->boundPort = port;
    }

    m_impl->running.store(true, std::memory_order_relaxed);
    m_impl->ioThread = std::thread([impl = m_impl.get()]() { impl->ioLoop(); });
    Log::info("[MuseSocket] listening on 127.0.0.1:" + std::to_string(m_impl->boundPort));
    return true;
}

void MuseSocketServer::stop() {
    if (!m_impl->running.exchange(false, std::memory_order_relaxed)) return;
    if (m_impl->ioThread.joinable()) m_impl->ioThread.join();
    closeSocket(m_impl->listenSocket);
    m_impl->listenSocket = kInvalidSocket;
}

uint16_t MuseSocketServer::port() const {
    return m_impl->boundPort;
}

bool MuseSocketServer::isRunning() const {
    return m_impl->running.load(std::memory_order_relaxed);
}

size_t MuseSocketServer::processPending(MuseService& service) {
    // At most kMaxRequestsPerFrame per call; the rest wait for the next frame,
    // so a backlog can never monopolise one frame. Order per client is kept.
    std::deque<std::pair<uint64_t, std::string>> requests;
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        while (!m_impl->inbox.empty() && requests.size() < kMaxRequestsPerFrame) {
            requests.push_back(std::move(m_impl->inbox.front()));
            m_impl->inbox.pop_front();
        }
    }
    for (auto& [clientId, line] : requests) {
        std::string response = service.handleRequest(line);
        response += '\n';
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->outbox.emplace_back(clientId, std::move(response));
    }
    return requests.size();
}

} // namespace Audio
} // namespace Aestra
