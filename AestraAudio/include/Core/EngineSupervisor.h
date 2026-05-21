// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace Aestra {
namespace Audio {

/**
 * Recovery policy profiles for the engine supervisor.
 * Different profiles apply different thresholds and actions.
 */
enum class RecoveryProfile : uint8_t {
    Studio = 0,      ///< Conservative: warn, soft-limit, never auto-bypass
    LivePerformance,  ///< Aggressive: fast bypass, minimal latency impact
    OfflineRender     ///< Strict: halt on any anomaly, no fallback
};

/**
 * Health state for a single plugin, incorporating violation decay.
 * The rolling CPU score decays over time to avoid permanent penalization.
 * Only accessed from non-RT threads (mutex-protected).
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
 * Thread safety:
 * - Audio-thread methods (reportPluginTimeout, shouldBypassPlugin, isPluginQuarantined,
 *   quarantinePlugin) use lock-free atomic operations on a fixed-size slot array.
 * - Non-RT methods (getPluginHealth, decayHealthScores, reset) use mutex-protected
 *   access to the detailed health map.
 * - The slot array is indexed by pluginId % kMaxTrackedPlugins. Collisions are
 *   acceptable — the worst case is a false bypass on an unrelated plugin, which
 *   is safe (audio continues, just bypassed).
 */
class EngineSupervisor {
public:
    /// Maximum number of plugins tracked in the lock-free slot array
    static constexpr size_t kMaxTrackedPlugins = 256;

    EngineSupervisor();
    ~EngineSupervisor();

    /// Set the active recovery profile
    void setProfile(RecoveryProfile profile) noexcept { m_profile.store(profile, std::memory_order_relaxed); }

    /// Get the active recovery profile
    RecoveryProfile getProfile() const noexcept { return m_profile.load(std::memory_order_relaxed); }

    /**
     * Report a plugin timeout from the audio thread.
     * Lock-free: uses atomic operations on the slot array.
     * Called when a plugin's process() exceeds its time budget.
     */
    void reportPluginTimeout(uint64_t pluginId, uint64_t timestamp) noexcept;

    /**
     * Report an xrun event from the audio thread.
     * Lock-free: atomic increment.
     */
    void reportXrun() noexcept { m_xrunCount.fetch_add(1, std::memory_order_relaxed); }

    /**
     * Report a denormal event from the audio thread.
     * Lock-free: atomic increment.
     */
    void reportDenormal(uint64_t pluginId, uint32_t severity, uint64_t timestamp) noexcept;

    /**
     * Check if a plugin should be bypassed based on its health state.
     * Called from the audio thread during processBlock.
     * Lock-free: atomic read from slot array.
     */
    bool shouldBypassPlugin(uint64_t pluginId) const noexcept;

    /**
     * Check if a plugin is quarantined (logically detached from audio graph).
     * Lock-free: atomic read from slot array.
     */
    bool isPluginQuarantined(uint64_t pluginId) const noexcept;

    /**
     * Quarantine a plugin — logically detach from audio graph immediately.
     * Physical teardown is offloaded to AsyncCleanupManager.
     * Lock-free: atomic write to slot array.
     */
    void quarantinePlugin(uint64_t pluginId) noexcept;

    /**
     * Get a copy of a plugin's health state.
     * NOT lock-free: mutex-protected read. Only call from non-RT threads.
     */
    PluginHealthState getPluginHealth(uint64_t pluginId) const;

    /**
     * Decay rolling scores for all tracked plugins.
     * NOT lock-free: mutex-protected write. Only call from non-RT threads.
     */
    void decayHealthScores(double decayFactor = 0.95);

    /**
     * Get the total xrun count since engine start.
     * Lock-free: atomic read.
     */
    uint64_t getXrunCount() const noexcept { return m_xrunCount.load(std::memory_order_relaxed); }

    /**
     * Get the total plugin timeout count since engine start.
     * Lock-free: atomic read.
     */
    uint64_t getTimeoutCount() const noexcept { return m_timeoutCount.load(std::memory_order_relaxed); }

    /// Reset all health state (e.g., on engine restart). NOT lock-free.
    void reset();

private:
    /**
     * Lock-free slot for audio-thread-facing plugin state.
     * Uses atomic uint32_t as a bitfield:
     *   bit 0: isBypassed
     *   bit 1: isQuarantined
     *   bits 2-31: consecutiveTimeouts (saturating at 255)
     */
    struct alignas(64) PluginSlot {  // cacheline-aligned to avoid false sharing
        std::atomic<uint32_t> flags{0};

        bool isBypassed() const noexcept { return flags.load(std::memory_order_relaxed) & 1u; }
        bool isQuarantined() const noexcept { return flags.load(std::memory_order_relaxed) & 2u; }
        uint32_t timeoutCount() const noexcept { return (flags.load(std::memory_order_relaxed) >> 2) & 0xFFu; }

        void setBypassed(bool v) noexcept {
            auto f = flags.load(std::memory_order_relaxed);
            do {
                f = v ? (f | 1u) : (f & ~1u);
            } while (!flags.compare_exchange_weak(f, f, std::memory_order_relaxed));
        }

        void setQuarantined() noexcept {
            flags.fetch_or(2u, std::memory_order_relaxed);
            flags.fetch_or(1u, std::memory_order_relaxed);  // quarantine implies bypass
        }

        uint32_t incrementTimeout(uint32_t threshold) noexcept {
            auto f = flags.load(std::memory_order_relaxed);
            uint32_t count;
            do {
                count = ((f >> 2) & 0xFFu) + 1;
                if (count > 255) count = 255;
                f = (f & ~(0xFFu << 2)) | (count << 2);
                if (count >= threshold) f |= 1u;  // auto-bypass
            } while (!flags.compare_exchange_weak(f, f, std::memory_order_relaxed));
            return count;
        }

        void reset() noexcept { flags.store(0, std::memory_order_relaxed); }
    };

    PluginSlot& getSlot(uint64_t pluginId) noexcept { return m_slots[pluginId % kMaxTrackedPlugins]; }
    const PluginSlot& getSlot(uint64_t pluginId) const noexcept { return m_slots[pluginId % kMaxTrackedPlugins]; }

    std::atomic<RecoveryProfile> m_profile{RecoveryProfile::Studio};
    std::atomic<uint64_t> m_xrunCount{0};
    std::atomic<uint64_t> m_timeoutCount{0};
    std::atomic<uint64_t> m_denormalCount{0};

    /// Lock-free slot array for audio-thread operations
    PluginSlot m_slots[kMaxTrackedPlugins];

    /// Mutex-protected detailed health state for non-RT access
    mutable std::mutex m_healthMutex;
    std::unordered_map<uint64_t, PluginHealthState> m_pluginHealth;

    static constexpr uint32_t kStudioTimeoutThreshold = 5;
    static constexpr uint32_t kLiveTimeoutThreshold = 2;
    static constexpr double kDefaultDecayFactor = 0.95;
};

} // namespace Audio
} // namespace Aestra
