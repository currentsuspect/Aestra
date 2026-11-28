// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>

namespace Aestra {
namespace Audio {

/**
 * @brief Whether the audio graph is being rendered offline (a bounce, a freeze, an export) and not played live.
 *
 * A sandboxed plugin's process() is built for real time: it hands the block to the helper process and plays the result one chunk
 * later, or plays that block dry if the result is late. An offline render runs flat out, far faster than the helper can follow,
 * so it would bake dropped and dry blocks into the audio (crackle). While this is set, a sandboxed plugin waits for the helper's
 * answer to every block instead, with the same one-chunk delay, so delay compensation lines up and the result is exact.
 *
 * Set by the exporter around a render (AudioExporter), never by the audio callback.
 */
class OfflineRenderMode {
public:
    static bool active() { return flag().load(std::memory_order_acquire); }
    static void setActive(bool on) { flag().store(on, std::memory_order_release); }

private:
    static std::atomic<bool>& flag() {
        static std::atomic<bool> value{false};
        return value;
    }
};

} // namespace Audio
} // namespace Aestra
