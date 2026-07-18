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

    /** @brief Send one request line, block until the response line arrives. */
    std::string request(const std::string& line);

    bool isConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace MuseAgent
} // namespace Aestra
