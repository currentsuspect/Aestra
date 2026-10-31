// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Aestra {
namespace MuseAgent {

/** @brief An endpoint spec split into host and port text (port is not validated here). */
struct HostPort {
    bool ok = false;
    std::string host;
    std::string portText;
};

/**
 * @brief Split "<port>", "<host>:<port>" or "[<ipv6>]:<port>".
 *
 * A bare port keeps @p defaultHost. An IPv6 literal must be bracketed, because
 * its colons are otherwise indistinguishable from the port separator; an
 * unbracketed token with more than one colon is rejected rather than guessed.
 */
inline HostPort splitHostPort(std::string_view spec, std::string_view defaultHost) {
    HostPort out;
    if (spec.empty()) return out;
    if (spec.front() == '[') {
        const auto close = spec.find(']');
        if (close == std::string_view::npos || close + 1 >= spec.size() || spec[close + 1] != ':') return out;
        out.host = std::string(spec.substr(1, close - 1));
        out.portText = std::string(spec.substr(close + 2));
    } else if (const auto colon = spec.find(':'); colon == std::string_view::npos) {
        out.host = std::string(defaultHost);
        out.portText = std::string(spec);
    } else {
        if (spec.find(':', colon + 1) != std::string_view::npos) return out;
        out.host = std::string(spec.substr(0, colon));
        out.portText = std::string(spec.substr(colon + 1));
    }
    out.ok = !out.host.empty() && !out.portText.empty();
    return out;
}

/**
 * @brief Blocking JSONL client for a Muse socket (live app or MuseRepl --port).
 */
class MuseSocketClient {
public:
    MuseSocketClient();
    ~MuseSocketClient();

    MuseSocketClient(const MuseSocketClient&) = delete;
    MuseSocketClient& operator=(const MuseSocketClient&) = delete;

    /** @brief Connect to a numeric IPv4/IPv6 address or a resolvable name (e.g. "localhost"). */
    bool connect(const std::string& host, uint16_t port, std::string& outError);

    /**
     * @brief Give recv a deadline, so a connected-but-silent peer cannot hang
     *        the caller forever. 0 restores blocking-until-answered.
     *
     * Call after connect(); it applies to the open socket. Requests that do
     * real work (render_song bounces a whole timeline) need a generous value —
     * this is a hang guard, not a latency budget.
     */
    void setReadTimeoutMs(int milliseconds);

    /** @brief Why a request stopped, for callers that must tell these apart. */
    enum class Outcome {
        Ok,           ///< a response line came back
        Disconnected, ///< the peer went away mid-request
        TimedOut      ///< the read deadline elapsed with no response line
    };

    /**
     * @brief Send one request line, block until the response line arrives.
     * @param outOutcome optional; distinguishes a timeout from a lost peer,
     *        which the returned JSON string alone cannot express structurally.
     */
    std::string request(const std::string& line, Outcome* outOutcome = nullptr);

    bool isConnected() const;

    /**
     * @brief Present the session token (FD-64): the first line every client must send.
     *
     * The server refuses everything else until it has seen the right token, so call this right
     * after connect() and before any request(). On failure the connection is spent.
     */
    bool authenticate(const std::string& token, std::string& outError);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MuseAgent
} // namespace Aestra
