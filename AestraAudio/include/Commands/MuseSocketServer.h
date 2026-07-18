// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class MuseService;

/**
 * @brief Localhost JSONL socket entry point to Muse — agents inside a live session.
 *
 * Listens on 127.0.0.1 only (never exposed to the network) and speaks the
 * exact MuseRepl protocol: one JSON request per line in, one JSON response
 * per line out, over the same MuseService the UI's command system uses —
 * agent edits land in the user's undo stack.
 *
 * Threading contract:
 *  - A background IO thread accepts clients, reads lines, and writes queued
 *    responses. It never touches the service.
 *  - The owner calls processPending() from the MAIN thread (the app pumps it
 *    once per frame); that is the only place requests execute, so command
 *    execution is serialized with the UI exactly like user edits.
 *
 * Opt-in: the app only starts a server when AESTRA_MUSE_PORT is set.
 */
class MuseSocketServer {
public:
    MuseSocketServer();
    ~MuseSocketServer();

    MuseSocketServer(const MuseSocketServer&) = delete;
    MuseSocketServer& operator=(const MuseSocketServer&) = delete;

    /**
     * @brief Bind 127.0.0.1:port and start the IO thread.
     * @param port TCP port; 0 picks an ephemeral port (see port()).
     * @param outError Failure reason when returning false.
     */
    bool start(uint16_t port, std::string& outError);

    /** @brief Stop the IO thread and close every socket. Safe to call twice. */
    void stop();

    /** @brief The bound port (resolved when start() was given 0). */
    uint16_t port() const;

    /** @brief True between a successful start() and stop(). */
    bool isRunning() const;

    /**
     * @brief Execute every fully received request line on the caller's thread.
     *
     * Runs each through the service and queues the responses for the IO
     * thread to deliver. Call from the main thread only.
     * @return Number of requests processed.
     */
    size_t processPending(MuseService& service);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Audio
} // namespace Aestra
