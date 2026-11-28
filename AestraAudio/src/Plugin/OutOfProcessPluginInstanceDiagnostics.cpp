// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// What a sandboxed plugin reports about itself to the log: kept apart from the hand-off (OutOfProcessPluginInstance.cpp, at its size cap).

#include "AestraLog.h"
#include "Plugin/OfflineRenderMode.h"
#include "Plugin/OutOfProcessPluginInstance.h"

#include <chrono>
#include <string>

namespace Aestra {
namespace Audio {

void OutOfProcessPluginInstance::reportLostBlocks() {
    // A late block plays dry and an unsent one is skipped: to the ear that is crackle on a quiet DSP meter. Nothing else says
    // which plugin it is or when it started, so say it here, off the audio thread, and only while it is happening.
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastLostReport < std::chrono::seconds(1)) {
        return;
    }
    m_lastLostReport = now;
    const uint64_t dry = m_dryBlocks.load(std::memory_order_relaxed);
    const uint64_t unsent = m_unsentBlocks.load(std::memory_order_relaxed);
    if (dry != m_reportedDry || unsent != m_reportedUnsent) {
        Log::warning("[PluginHost] " + m_info.name + ": " + std::to_string(dry - m_reportedDry) + " late (dry) and " +
                     std::to_string(unsent - m_reportedUnsent) + " unsent blocks in the last second (chunk " +
                     std::to_string(m_chunkFrames.load(std::memory_order_relaxed)) + " frames, " +
                     (OfflineRenderMode::active() ? "rendering" : "live") + ")");
        m_reportedDry = dry;
        m_reportedUnsent = unsent;
    }
}

} // namespace Audio
} // namespace Aestra
