// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>

namespace Aestra {
namespace Audio {

/** @brief What happens at the edges of playback: how a play starts and what a stop does to sound. */
struct TransportStartState {
    std::atomic<bool> holdForSchedule{false}; ///< live app: park a play start until its notes are queued
    std::atomic<bool> stopCutsSound{false};   ///< preference: stop = hard stop (cut), not release
    std::atomic<bool> releaseRequested{false}; ///< audio thread: send note releases to instrument units
    uint32_t holdFramesLeft{0};                ///< audio thread: hold budget left for the current start
};

} // namespace Audio
} // namespace Aestra
