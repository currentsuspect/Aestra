// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MuseSocketClient.h"

#include "AestraJSON.h"
#include "MuseSocketToken.h"

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

#include <cerrno>
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
    // Numeric addresses parse without touching the resolver; names like
    // "localhost" go through getaddrinfo. AF_UNSPEC so "::1" works too, and
    // every returned address is tried in order (localhost often lists ::1
    // before 127.0.0.1 while the server listens on only one of them).
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string portText = std::to_string(port);
    const int resolveStatus = ::getaddrinfo(host.c_str(), portText.c_str(), &hints, &results);
    if (resolveStatus != 0 || results == nullptr) {
        outError = "cannot resolve host '" + host + "': " + ::gai_strerror(resolveStatus);
        return false;
    }

    bool socketCreated = false;
    for (const addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
        const SocketHandle handle =
            ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (handle == kInvalidSocket) continue;
        socketCreated = true;
#ifdef SO_NOSIGPIPE
        {
            const int enable = 1;
            ::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
        }
#endif
        if (::connect(handle, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
            closeSocket(m_impl->socket);
            m_impl->socket = handle;
            ::freeaddrinfo(results);
            return true;
        }
        closeSocket(handle);
    }
    ::freeaddrinfo(results);
    outError = socketCreated ? "connect to " + host + ":" + std::to_string(port) + " failed"
                             : "socket() failed";
    return false;
}

void MuseSocketClient::setReadTimeoutMs(int milliseconds) {
    if (m_impl->socket == kInvalidSocket || milliseconds < 0) {
        return;
    }
#ifdef _WIN32
    const DWORD value = static_cast<DWORD>(milliseconds);
    ::setsockopt(m_impl->socket, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&value), sizeof(value));
#else
    timeval value{};
    value.tv_sec = milliseconds / 1000;
    value.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(m_impl->socket, SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value));
#endif
}

namespace {

// SO_RCVTIMEO expiry looks like any other recv failure, so ask the platform
// which one it was — a timeout is recoverable information for the caller,
// a dead peer is not.
bool lastRecvWasTimeout() {
#ifdef _WIN32
    const int code = ::WSAGetLastError();
    return code == WSAETIMEDOUT;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

} // namespace

std::string MuseSocketClient::request(const std::string& line, Outcome* outOutcome) {
    const auto report = [&](Outcome outcome) {
        if (outOutcome != nullptr) *outOutcome = outcome;
    };
    report(Outcome::Ok);

    if (m_impl->socket == kInvalidSocket) {
        report(Outcome::Disconnected);
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
            report(Outcome::Disconnected);
            return "{\"status\": \"execution_error\", \"message\": \"connection lost on send\"}";
        }
        sent += static_cast<size_t>(n);
    }

    size_t newline;
    while ((newline = m_impl->readBuffer.find('\n')) == std::string::npos) {
        char chunk[4096];
        const auto received = ::recv(m_impl->socket, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            // A timeout leaves the connection usable in principle, but this
            // request is unanswerable — close either way so a later request
            // cannot read this one's late response as its own.
            const bool timedOut = (received < 0) && lastRecvWasTimeout();
            closeSocket(m_impl->socket);
            m_impl->socket = kInvalidSocket;
            report(timedOut ? Outcome::TimedOut : Outcome::Disconnected);
            return timedOut
                       ? "{\"status\": \"execution_error\", \"message\": \"timed out waiting for a response\"}"
                       : "{\"status\": \"execution_error\", \"message\": \"connection lost on recv\"}";
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

bool MuseSocketClient::authenticate(const std::string& token, std::string& outError) {
    Outcome outcome{};
    const std::string reply = request(MuseToken::authLine(token), &outcome);
    if (outcome != Outcome::Ok) {
        outError = "the Muse socket closed during authentication";
        return false;
    }
    const JSON parsed = JSON::parse(reply);
    if (parsed.isObject() && parsed["status"].isString() && parsed["status"].asString() == "ok") return true;
    outError = "the Muse socket refused the token (is the token file from this session?)";
    return false;
}

} // namespace MuseAgent
} // namespace Aestra
