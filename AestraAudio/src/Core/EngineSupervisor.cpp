// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "EngineSupervisor.h"

#include <algorithm>

namespace Aestra {
namespace Audio {

EngineSupervisor::EngineSupervisor() = default;
EngineSupervisor::~EngineSupervisor() = default;

void EngineSupervisor::reportPluginTimeout(uint64_t pluginId, uint64_t timestamp) noexcept {
    m_timeoutCount.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard lock(m_healthMutex);
    auto& state = m_pluginHealth[pluginId];
    state.consecutiveTimeouts++;
    state.lastTimeoutTimestamp = timestamp;

    // Check if we should auto-bypass based on profile
    auto profile = m_profile.load(std::memory_order_relaxed);
    uint32_t threshold = (profile == RecoveryProfile::LivePerformance) ? kLiveTimeoutThreshold : kStudioTimeoutThreshold;

    if (state.consecutiveTimeouts >= threshold) {
        state.isBypassed = true;
    }
}

void EngineSupervisor::reportDenormal(uint64_t pluginId, uint32_t severity, uint64_t timestamp) noexcept {
    m_denormalCount.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard lock(m_healthMutex);
    auto& state = m_pluginHealth[pluginId];
    // Increase rolling score based on severity
    state.rollingCpuScore += static_cast<double>(severity + 1) * 0.1;
}

bool EngineSupervisor::shouldBypassPlugin(uint64_t pluginId) const noexcept {
    // Lock-free read: check the bypassed flag directly
    // The flag is set by reportPluginTimeout under lock
    std::lock_guard lock(m_healthMutex);
    auto it = m_pluginHealth.find(pluginId);
    if (it == m_pluginHealth.end()) return false;
    return it->second.isBypassed;
}

bool EngineSupervisor::isPluginQuarantined(uint64_t pluginId) const noexcept {
    std::lock_guard lock(m_healthMutex);
    auto it = m_pluginHealth.find(pluginId);
    if (it == m_pluginHealth.end()) return false;
    return it->second.isQuarantined;
}

void EngineSupervisor::quarantinePlugin(uint64_t pluginId) noexcept {
    std::lock_guard lock(m_healthMutex);
    auto& state = m_pluginHealth[pluginId];
    state.isQuarantined = true;
    state.isBypassed = true;
}

PluginHealthState EngineSupervisor::getPluginHealth(uint64_t pluginId) const {
    std::lock_guard lock(m_healthMutex);
    auto it = m_pluginHealth.find(pluginId);
    if (it == m_pluginHealth.end()) return {};
    return it->second;
}

void EngineSupervisor::decayHealthScores(double decayFactor) {
    std::lock_guard lock(m_healthMutex);
    for (auto& [id, state] : m_pluginHealth) {
        state.rollingCpuScore *= decayFactor;
        // Reset consecutive timeouts if score drops below threshold
        if (state.rollingCpuScore < 0.01) {
            state.consecutiveTimeouts = 0;
            state.isBypassed = false;
        }
    }
}

void EngineSupervisor::reset() {
    std::lock_guard lock(m_healthMutex);
    m_pluginHealth.clear();
    m_xrunCount.store(0, std::memory_order_relaxed);
    m_timeoutCount.store(0, std::memory_order_relaxed);
    m_denormalCount.store(0, std::memory_order_relaxed);
}

} // namespace Audio
} // namespace Aestra
