// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MuseSocketClient.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
static constexpr SocketHandle kInvalidSocket = -1;
#endif

#include <cstring>
#include <mutex>

namespace Aestra {
namespace MuseAgent {

struct MuseSocketClient::Impl {
    SocketHandle socket = kInvalidSocket;
    std::string readBuffer;
};

namespace {

// A peer that vanished must surface as a send() error, not a SIGPIPE kill.
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}
} // namespace

MuseSocketClient::MuseSocketClient() : m_impl(std::make_unique<Impl>()) {}

MuseSocketClient::~MuseSocketClient() {
    closeSocket(m_impl->socket);
}

bool MuseSocketClient::connect(const std::string& host, uint16_t port, std::string& outError) {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, []() {
        WSADATA data{};
        ::WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
    m_impl->socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_impl->socket == kInvalidSocket) {
        outError = "socket() failed";
        return false;
    }
#ifdef SO_NOSIGPIPE
    {
        const int enable = 1;
        ::setsockopt(m_impl->socket, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
    }
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        outError = "invalid host address: " + host;
        closeSocket(m_impl->socket);
        m_impl->socket = kInvalidSocket;
        return false;
    }
    if (::connect(m_impl->socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        outError = "connect to " + host + ":" + std::to_string(port) + " failed";
        closeSocket(m_impl->socket);
        m_impl->socket = kInvalidSocket;
        return false;
    }
    return true;
}

std::string MuseSocketClient::request(const std::string& line) {
    if (m_impl->socket == kInvalidSocket) {
        return "{\"status\": \"execution_error\", \"message\": \"not connected\"}";
    }

    std::string toSend = line;
    toSend += '\n';
    size_t sent = 0;
    while (sent < toSend.size()) {
        const auto n = ::send(m_impl->socket, toSend.data() + sent,
#ifdef _WIN32
                              static_cast<int>(toSend.size() - sent),
#else
                              toSend.size() - sent,
#endif
                              kSendFlags);
        if (n <= 0) {
            closeSocket(m_impl->socket);
            m_impl->socket = kInvalidSocket;
            return "{\"status\": \"execution_error\", \"message\": \"connection lost on send\"}";
        }
        sent += static_cast<size_t>(n);
    }

    size_t newline;
    while ((newline = m_impl->readBuffer.find('\n')) == std::string::npos) {
        char chunk[4096];
        const auto received = ::recv(m_impl->socket, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            closeSocket(m_impl->socket);
            m_impl->socket = kInvalidSocket;
            return "{\"status\": \"execution_error\", \"message\": \"connection lost on recv\"}";
        }
        m_impl->readBuffer.append(chunk, static_cast<size_t>(received));
    }
    std::string response = m_impl->readBuffer.substr(0, newline);
    m_impl->readBuffer.erase(0, newline + 1);
    if (!response.empty() && response.back() == '\r') response.pop_back();
    return response;
}

bool MuseSocketClient::isConnected() const {
    return m_impl->socket != kInvalidSocket;
}

} // namespace MuseAgent
} // namespace Aestra
