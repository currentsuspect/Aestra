// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Aestra {
namespace Audio {

/**
 * Recovery policy profiles for the engine supervisor.
 * Different profiles apply different thresholds and actions.
 */
enum class RecoveryProfile : uint32_t {
    Studio = 0,      ///< Conservative: warn, soft-limit, never auto-bypass
    LivePerformance,  ///< Aggressive: fast bypass, minimal latency impact
    OfflineRender     ///< Strict: halt on any anomaly, no fallback
};

/**
 * Health state for a single plugin, incorporating violation decay.
 * The rolling CPU score decays over time to avoid permanent penalization.
 */
struct PluginHealthState {
    double rollingCpuScore{0.0};
    uint32_t consecutiveTimeouts{0};
    uint64_t lastTimeoutTimestamp{0};
    bool isBypassed{false};
    bool isQuarantined{false};
};

/**
 * Decoupled engine supervisor that coordinates plugin health, recovery policies,
 * and asynchronous quarantine handling.
 *
 * Design principle: the audio thread emits facts (timeouts, xruns, denormals).
 * The supervisor makes decisions (bypass, quarantine, mute, soft-limit).
 * This separation allows distinct behaviors for Studio, Live, and Offline profiles
 * without touching DSP code.
 *
 * Thread safety: the supervisor is accessed from the audio thread (read-only,
 * lock-free) and from the UI/telemetry thread (read-write, mutex-protected).
 */
class EngineSupervisor {
public:
    EngineSupervisor();
    ~EngineSupervisor();

    /// Set the active recovery profile
    void setProfile(RecoveryProfile profile) noexcept { m_profile.store(profile, std::memory_order_relaxed); }

    /// Get the active recovery profile
    RecoveryProfile getProfile() const noexcept { return m_profile.load(std::memory_order_relaxed); }

    /**
     * Report a plugin timeout from the audio thread.
     * Called when a plugin's process() exceeds its time budget.
     * Thread-safe: lock-free, called from audio thread.
     */
    void reportPluginTimeout(uint64_t pluginId, uint64_t timestamp) noexcept;

    /**
     * Report an xrun event from the audio thread.
     * Thread-safe: lock-free, called from audio thread.
     */
    void reportXrun() noexcept { m_xrunCount.fetch_add(1, std::memory_order_relaxed); }

    /**
     * Report a denormal event from the audio thread.
     * Thread-safe: lock-free, called from audio thread.
     */
    void reportDenormal(uint64_t pluginId, uint32_t severity, uint64_t timestamp) noexcept;

    /**
     * Check if a plugin should be bypassed based on its health state.
     * Called from the audio thread during processBlock.
     * Thread-safe: lock-free read.
     */
    bool shouldBypassPlugin(uint64_t pluginId) const noexcept;

    /**
     * Check if a plugin is quarantined (logically detached from audio graph).
     * Thread-safe: lock-free read.
     */
    bool isPluginQuarantined(uint64_t pluginId) const noexcept;

    /**
     * Quarantine a plugin — logically detach from audio graph immediately.
     * Physical teardown is offloaded to AsyncCleanupManager.
     * Thread-safe: lock-free write to atomic flag.
     */
    void quarantinePlugin(uint64_t pluginId) noexcept;

    /**
     * Get a copy of a plugin's health state.
     * Thread-safe: mutex-protected read.
     */
    PluginHealthState getPluginHealth(uint64_t pluginId) const;

    /**
     * Decay rolling scores for all tracked plugins.
     * Called periodically from a non-RT thread.
     * Thread-safe: mutex-protected write.
     */
    void decayHealthScores(double decayFactor = 0.95);

    /**
     * Get the total xrun count since engine start.
     * Thread-safe: lock-free atomic read.
     */
    uint64_t getXrunCount() const noexcept { return m_xrunCount.load(std::memory_order_relaxed); }

    /**
     * Get the total plugin timeout count since engine start.
     * Thread-safe: lock-free atomic read.
     */
    uint64_t getTimeoutCount() const noexcept { return m_timeoutCount.load(std::memory_order_relaxed); }

    /// Reset all health state (e.g., on engine restart)
    void reset();

private:
    std::atomic<RecoveryProfile> m_profile{RecoveryProfile::Studio};
    std::atomic<uint64_t> m_xrunCount{0};
    std::atomic<uint64_t> m_timeoutCount{0};
    std::atomic<uint64_t> m_denormalCount{0};

    mutable std::mutex m_healthMutex;
    std::unordered_map<uint64_t, PluginHealthState> m_pluginHealth;

    /// Threshold for auto-bypass in Studio profile
    static constexpr uint32_t kStudioTimeoutThreshold = 5;
    /// Threshold for auto-bypass in Live profile
    static constexpr uint32_t kLiveTimeoutThreshold = 2;
    /// Decay factor applied per cycle (0.95 = 5% decay per cycle)
    static constexpr double kDefaultDecayFactor = 0.95;
};

} // namespace Audio
} // namespace Aestra
