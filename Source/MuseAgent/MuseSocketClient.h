// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {
namespace MuseAgent {

/**
 * @brief Blocking JSONL client for a Muse socket (live app or MuseRepl --port).
 */
class MuseSocketClient {
public:
    MuseSocketClient();
    ~MuseSocketClient();

    MuseSocketClient(const MuseSocketClient&) = delete;
    MuseSocketClient& operator=(const MuseSocketClient&) = delete;

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MuseAgent
} // namespace Aestra
