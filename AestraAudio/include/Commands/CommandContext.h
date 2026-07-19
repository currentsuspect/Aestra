#pragma once

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

/**
 * @brief Live session dependencies a command factory needs to build a command.
 *
 * Threaded explicitly from each caller (the owner of the engine + track model)
 * through CommandParser into CommandRegistry::build, instead of being reached
 * ambiently through a process-wide pointer. Both members are borrowed, never
 * owned: the context outlives no build call and stores nothing.
 *
 * A member may be null when that dependency does not exist yet (e.g. the engine
 * before AudioEngine wiring, or a headless registry with no track model). The
 * factories that need a given member check it and refuse to build rather than
 * dereference null — the same refusal the old ambient path produced.
 */
struct CommandContext {
    AudioEngine* engine = nullptr;
    TrackManager* trackManager = nullptr;
};

} // namespace Audio
} // namespace Aestra
