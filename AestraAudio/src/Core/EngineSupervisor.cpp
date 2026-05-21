// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "EngineSupervisor.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

EngineSupervisor::EngineSupervisor() = default;
EngineSupervisor::~EngineSupervisor() = default;

void EngineSupervisor::reportPluginTimeout(uint64_t pluginId, uint64_t timestamp) noexcept {
    m_timeoutCount.fetch_add(1, std::memory_order_relaxed);

    auto profile = m_profile.load(std::memory_order_relaxed);
    uint32_t threshold = (profile == RecoveryProfile::LivePerformance)
                             ? kLiveTimeoutThreshold
                             : kStudioTimeoutThreshold;

    // Lock-free: update slot
    getSlot(pluginId).incrementTimeout(threshold);

    // Also update detailed state for non-RT access (best-effort, not critical path)
    if (m_healthMutex.try_lock()) {
        auto& state = m_pluginHealth[pluginId];
        state.consecutiveTimeouts++;
        state.lastTimeoutTimestamp = timestamp;
        state.isBypassed = getSlot(pluginId).isBypassed();
        m_healthMutex.unlock();
    }
}

void EngineSupervisor::reportDenormal(uint64_t pluginId, uint32_t severity, uint64_t timestamp) noexcept {
    (void)timestamp;
    m_denormalCount.fetch_add(1, std::memory_order_relaxed);
    // Denormals increase rolling score — only tracked in detailed state (non-RT)
    if (m_healthMutex.try_lock()) {
        auto& state = m_pluginHealth[pluginId];
        state.rollingCpuScore += static_cast<double>(severity + 1) * 0.1;
        m_healthMutex.unlock();
    }
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
    std::lock_guard lock(m_healthMutex);
    for (auto& [id, state] : m_pluginHealth) {
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
