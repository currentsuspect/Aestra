// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "EngineSupervisor.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

EngineSupervisor::EngineSupervisor() = default;
EngineSupervisor::~EngineSupervisor() = default;

void EngineSupervisor::reportPluginTimeout(uint64_t pluginId, uint64_t timestamp) noexcept {
    (void)timestamp;
    m_timeoutCount.fetch_add(1, std::memory_order_relaxed);

    auto profile = m_profile.load(std::memory_order_relaxed);
    uint32_t threshold;
    switch (profile) {
        case RecoveryProfile::LivePerformance: threshold = kLiveTimeoutThreshold; break;
        case RecoveryProfile::OfflineRender:   threshold = kOfflineTimeoutThreshold; break;
        default:                               threshold = kStudioTimeoutThreshold; break;
    }

    // Lock-free: update slot (only safe path on audio thread)
    getSlot(pluginId).incrementTimeout(threshold);

    // Note: m_pluginHealth (mutex-protected, heap-allocating) is NOT updated from the
    // audio thread. The slot array is the authoritative lock-free state. Non-RT threads
    // that need detailed health info can read the slot flags and call getPluginHealth()
    // which accesses m_pluginHealth under mutex.
}

void EngineSupervisor::reportDenormal(uint64_t pluginId, uint32_t severity, uint64_t timestamp) noexcept {
    (void)pluginId;
    (void)severity;
    (void)timestamp;
    m_denormalCount.fetch_add(1, std::memory_order_relaxed);
    // Denormals are counted atomically. Rolling score is updated by decayHealthScores()
    // on a non-RT thread. The slot array is the authoritative lock-free state.
}

bool EngineSupervisor::shouldBypassPlugin(uint64_t pluginId) const noexcept {
    // Lock-free: read from slot array
    return getSlot(pluginId).isBypassed();
}

bool EngineSupervisor::isPluginQuarantined(uint64_t pluginId) const noexcept {
    // Lock-free: read from slot array
    return getSlot(pluginId).isQuarantined();
}

void EngineSupervisor::quarantinePlugin(uint64_t pluginId) noexcept {
    // Lock-free: write to slot array
    getSlot(pluginId).setQuarantined();
}

PluginHealthState EngineSupervisor::getPluginHealth(uint64_t pluginId) const {
    // NOT lock-free: mutex-protected. Only call from non-RT threads.
    std::lock_guard lock(m_healthMutex);
    auto it = m_pluginHealth.find(pluginId);
    if (it == m_pluginHealth.end()) return {};
    return it->second;
}

void EngineSupervisor::decayHealthScores(double decayFactor) {
    // NOT lock-free: mutex-protected. Only call from non-RT threads.

    // Sanitize decayFactor: NaN/Inf → 0.0, clamp to [0.0, 1.0]
    if (!std::isfinite(decayFactor)) decayFactor = 0.0;
    if (decayFactor < 0.0) decayFactor = 0.0;
    if (decayFactor > 1.0) decayFactor = 1.0;

    std::lock_guard lock(m_healthMutex);
    for (auto& [id, state] : m_pluginHealth) {
        // Defensive: reset non-finite scores to zero
        if (!std::isfinite(state.rollingCpuScore)) {
            state.rollingCpuScore = 0.0;
        }
        state.rollingCpuScore *= decayFactor;
        if (state.rollingCpuScore < 0.01) {
            state.consecutiveTimeouts = 0;
            state.isBypassed = false;
            // Also clear the lock-free slot
            getSlot(id).reset();
        }
    }
}

void EngineSupervisor::reset() {
    // NOT lock-free: mutex-protected. Only call from non-RT threads.
    std::lock_guard lock(m_healthMutex);
    m_pluginHealth.clear();
    for (auto& slot : m_slots) slot.reset();
    m_xrunCount.store(0, std::memory_order_relaxed);
    m_timeoutCount.store(0, std::memory_order_relaxed);
    m_denormalCount.store(0, std::memory_order_relaxed);
}

} // namespace Audio
} // namespace Aestra
