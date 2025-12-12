// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>

namespace Nomad {
namespace Audio {

/**
 * @brief Lightweight telemetry counters updated from the RT thread.
 *
 * All fields are atomics for lock-free access; UI/non-RT code should snapshot
 * these periodically and handle presentation/logging off the audio thread.
 */
struct AudioTelemetry {
    std::atomic<uint64_t> blocksProcessed{0};
    std::atomic<uint64_t> xruns{0};
    std::atomic<uint64_t> underruns{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> maxCallbackNs{0};
    std::atomic<uint64_t> lastCallbackNs{0};
};

} // namespace Audio
} // namespace Nomad
