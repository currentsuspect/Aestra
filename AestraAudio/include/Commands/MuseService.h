// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/HostVerbRegistry.h"
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

/**
 * @brief Muse's structured machine interface: one JSON request in, one JSON
 *        response out.
 *
 * This is the single surface every external driver (CLI, socket, local model,
 * tests, future in-app chat) talks through — nothing should reach past it to
 * command objects or engine internals.
 *
 * Request:  {"id": 42, "verb": "set_bpm", "args": {"value": 142}}
 * Response: {"id": 42, "status": "ok", "verb": "set_bpm",
 *            "result": {...}, "executionMs": 0.18}
 *
 * - Mutation verbs run through the MuseGrammar schema validation, the
 *   CommandRegistry factories, and CommandHistory — every mutation is
 *   undoable and identical to the equivalent UI edit.
 * - Query verbs (get_transport, list_tracks, list_clips, get_session_state)
 *   are read-only, never touch history, and return stable IDs so an agent
 *   never has to infer object identity across edits.
 * - handleRequest never throws; malformed input comes back as a
 *   status:"parse_error" response.
 */
class MuseService {
public:
    MuseService(TrackManager* trackManager, AudioEngine* engine);

    /** @brief Process one JSON request line and return the JSON response. */
    std::string handleRequest(const std::string& requestJson);

    /**
     * @brief Attach an engine to a track manager the way the app does.
     *
     * Headless hosts (MuseRepl, tests) must wire the same five links
     * AestraContent wires — unit manager, pattern playback engine, continuous
     * params, channel slot map, and the transport command sink — or units
     * render silence and transport commands never reach the engine.
     * Both objects must outlive the wiring (the sink captures raw pointers).
     */
    static void wireHeadlessEngine(const std::shared_ptr<TrackManager>& trackManager,
                                   AudioEngine& engine);

    /**
     * @brief The seam the application registers its capabilities into.
     *
     * MuseService cannot reach the UI layer — the dependency runs the other
     * way. The host calls this during startup to register `settings.*`,
     * `view.*`, `browser.*`, `dialog.*` and `project.*` verbs; MuseService
     * only ever sees a name, an argument schema and a callable. See
     * HostVerbRegistry.h for why this is a typed registry rather than a
     * generic (name, function) escape hatch.
     */
    HostVerbRegistry& hostVerbs() { return m_hostVerbs; }
    const HostVerbRegistry& hostVerbs() const { return m_hostVerbs; }

    /**
     * @brief Declare that requests run where the host's UI thread lives.
     *
     * Default false, so a headless process refuses UI-affine host verbs with
     * a reason instead of running host code on a thread that does not own the
     * state it touches. The in-app socket server executes requests from the
     * main-thread pump, so AestraApp sets this true.
     */
    void setHostUiThreadAvailable(bool available) { m_hostUiThreadAvailable = available; }
    bool hostUiThreadAvailable() const { return m_hostUiThreadAvailable; }

private:
    TrackManager* m_trackManager = nullptr;
    AudioEngine* m_engine = nullptr;
    HostVerbRegistry m_hostVerbs;
    bool m_hostUiThreadAvailable = false;
};

} // namespace Audio
} // namespace Aestra
