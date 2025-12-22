// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerMath.h"
#include "MeterSnapshot.h"
#include "ChannelSlotMap.h"
#include "TrackManager.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace Nomad {

/**
 * @brief Per-channel UI state for the mixer.
 *
 * Stores channel identity, control state, and meter smoothing state.
 * Meter values are stored in dB space for natural-looking decay.
 *
 * Requirements: 3.1 - Each channel strip SHALL display track name and color.
 * Requirements: 12.3 - Meter smoothing state per channel.
 */
struct ChannelViewModel {
    // Identity
    uint32_t id{0};                      ///< Stable track/channel ID
    uint32_t slotIndex{0};               ///< Dense index into MeterSnapshotBuffer
    std::string name;                    ///< Display name
    uint32_t trackColor{0xFF808080};     ///< Track color (ARGB)
    std::string routeName{"Master"};     ///< Output routing name

    // Control state (reflects engine, optimistic updates allowed)
    float faderGainDb{0.0f};             ///< Fader position in dB
    float pan{0.0f};                     ///< Pan position (-1.0 to 1.0)
    float trimDb{0.0f};                  ///< Trim/gain in dB
    bool muted{false};                   ///< Mute state
    bool soloed{false};                  ///< Solo state
    bool armed{false};                   ///< Record arm state

    // FX state
    int fxCount{0};                      ///< Number of insert effects

    // Meter state (UI-side smoothing, stored in dB)
    float smoothedPeakL{MixerMath::DB_MIN};  ///< Smoothed left peak (dB)
    float smoothedPeakR{MixerMath::DB_MIN};  ///< Smoothed right peak (dB)
    float peakHoldL{MixerMath::DB_MIN};      ///< Peak hold left (dB)
    float peakHoldR{MixerMath::DB_MIN};      ///< Peak hold right (dB)
    double peakHoldTimerL{0.0};              ///< Time since peak hold set (seconds)
    double peakHoldTimerR{0.0};              ///< Time since peak hold set (seconds)
    bool clipLatchL{false};                  ///< Left channel clip latch
    bool clipLatchR{false};                  ///< Right channel clip latch

    /**
     * @brief Reset meter state to silence.
     */
    void resetMeters() {
        smoothedPeakL = MixerMath::DB_MIN;
        smoothedPeakR = MixerMath::DB_MIN;
        peakHoldL = MixerMath::DB_MIN;
        peakHoldR = MixerMath::DB_MIN;
        peakHoldTimerL = 0.0;
        peakHoldTimerR = 0.0;
        clipLatchL = false;
        clipLatchR = false;
    }
};


/**
 * @brief UI state manager for the mixer panel.
 *
 * Uses stable unique_ptr storage to prevent pointer invalidation when
 * channels are added/removed. An id→index map provides O(1) lookup.
 *
 * Meter smoothing happens in dB space for natural-looking decay.
 * Audio thread writes LINEAR peaks, this class converts to dB and applies smoothing.
 *
 * Requirements: 1.3 - Meter values SHALL update at minimum 30 Hz.
 * Requirements: 1.4 - Meter ballistics SHALL use attack ≤10ms, release 300ms.
 * Requirements: 9.1, 9.2, 9.3 - Peak hold and clip latch behavior.
 */
class MixerViewModel {
public:
    /// Meter smoothing parameters
    static constexpr float METER_ATTACK_MS = 5.0f;     ///< Fast attack for transients
    static constexpr float METER_RELEASE_MS = 300.0f;  ///< Smooth release decay
    static constexpr float PEAK_HOLD_MS = 750.0f;      ///< Time before peak hold decays
    static constexpr float PEAK_DECAY_MS = 1500.0f;    ///< Peak hold decay time

    MixerViewModel();
    ~MixerViewModel() = default;

    // Non-copyable, movable
    MixerViewModel(const MixerViewModel&) = delete;
    MixerViewModel& operator=(const MixerViewModel&) = delete;
    MixerViewModel(MixerViewModel&&) = default;
    MixerViewModel& operator=(MixerViewModel&&) = default;

    /**
     * @brief Update meter values from snapshot buffer.
     *
     * Reads LINEAR peaks from snapshot, converts to dB, applies smoothing.
     * Called from UI thread at frame rate.
     *
     * @param snapshots Lock-free meter snapshot buffer
     * @param deltaTime Time since last update (seconds)
     */
    void updateMeters(const Audio::MeterSnapshotBuffer& snapshots, double deltaTime);

    /**
     * @brief Sync channel list from engine state.
     *
     * Rebuilds channel list to match current tracks.
     * Called when tracks are added/removed.
     *
     * @param trackManager Audio engine track manager
     * @param slotMap Channel ID to slot index mapping
     */
    void syncFromEngine(const Audio::TrackManager& trackManager,
                        const Audio::ChannelSlotMap& slotMap);

    /**
     * @brief Get channel by ID.
     *
     * O(1) lookup via id→index map.
     *
     * @param id Channel/track ID
     * @return Pointer to channel view model, or nullptr if not found
     */
    ChannelViewModel* getChannelById(uint32_t id);
    const ChannelViewModel* getChannelById(uint32_t id) const;

    /**
     * @brief Get currently selected channel.
     *
     * @return Pointer to selected channel, or nullptr if none selected
     */
    ChannelViewModel* getSelectedChannel();
    const ChannelViewModel* getSelectedChannel() const;

    /**
     * @brief Get channel count (excluding master).
     */
    size_t getChannelCount() const { return m_channels.size(); }

    /**
     * @brief Get channel by index.
     *
     * @param index Zero-based index
     * @return Pointer to channel, or nullptr if out of range
     */
    ChannelViewModel* getChannelByIndex(size_t index);
    const ChannelViewModel* getChannelByIndex(size_t index) const;

    /**
     * @brief Get master channel.
     */
    ChannelViewModel* getMaster() { return m_master.get(); }
    const ChannelViewModel* getMaster() const { return m_master.get(); }

    /**
     * @brief Set selected channel by ID.
     *
     * @param id Channel ID, or -1 to clear selection
     */
    void setSelectedChannelId(int32_t id) { m_selectedChannelId = id; }

    /**
     * @brief Get selected channel ID.
     *
     * @return Selected channel ID, or -1 if none selected
     */
    int32_t getSelectedChannelId() const { return m_selectedChannelId; }

    /**
     * @brief Clear clip latch for a channel.
     *
     * @param id Channel ID
     */
    void clearClipLatch(uint32_t id);

    /**
     * @brief Clear clip latch for master channel.
     */
    void clearMasterClipLatch();

private:
    /// Stable storage - pointers remain valid across add/remove
    std::vector<std::unique_ptr<ChannelViewModel>> m_channels;

    /// Master channel (always exists)
    std::unique_ptr<ChannelViewModel> m_master;

    /// O(1) lookup by channel ID
    std::unordered_map<uint32_t, size_t> m_idToIndex;

    /// Currently selected channel ID (-1 = none)
    int32_t m_selectedChannelId{-1};

    /**
     * @brief Rebuild id→index map after channel list changes.
     */
    void rebuildIdMap();

    /**
     * @brief Apply meter smoothing to a single channel.
     *
     * @param channel Channel to update
     * @param linearL Left peak (LINEAR 0..1+)
     * @param linearR Right peak (LINEAR 0..1+)
     * @param clipL Left clip flag from snapshot
     * @param clipR Right clip flag from snapshot
     * @param deltaTime Time since last update (seconds)
     */
    void smoothMeterChannel(ChannelViewModel& channel,
                            float linearL, float linearR,
                            bool clipL, bool clipR,
                            double deltaTime);
};

} // namespace Nomad
