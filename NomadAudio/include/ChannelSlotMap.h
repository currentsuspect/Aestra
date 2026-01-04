// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <memory>

namespace Nomad {
namespace Audio {

// Forward declaration
class MixerChannel;

/**
 * @brief Maps channel IDs to dense slot indices for lock-free buffer access.
 *
 * Channel IDs are not necessarily dense (e.g., after track deletion, IDs may be 1, 3, 5).
 * The MeterSnapshotBuffer and ContinuousParamBuffer use dense slot indices (0, 1, 2...)
 * for cache efficiency. This class translates between them.
 *
 * Thread Safety:
 * - rebuild() must be called only at safe points (stop transport, track add/remove)
 * - After rebuild(), getSlotIndex() and getChannelId() are safe to call from any thread
 *   (read-only access to immutable data after rebuild)
 *
 * Requirements: 12.5 - Each channel control parameter SHALL be identified by stable
 * (channelId, paramId) pairs for automation readiness.
 */
class ChannelSlotMap {
public:
    /// Reserved slot index for master channel
    static constexpr uint32_t MASTER_SLOT_INDEX = 127;
    
    /// Returned when lookup fails
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    
    /// Maximum number of channel slots (excluding master)
    static constexpr uint32_t MAX_CHANNEL_SLOTS = 127;

    ChannelSlotMap() = default;
    ~ChannelSlotMap() = default;

    // Copyable (UI snapshots) and movable
    ChannelSlotMap(const ChannelSlotMap&) = default;
    ChannelSlotMap& operator=(const ChannelSlotMap&) = default;
    ChannelSlotMap(ChannelSlotMap&&) = default;
    ChannelSlotMap& operator=(ChannelSlotMap&&) = default;

    /**
     * @brief Rebuild the mapping from a list of tracks.
     *
     * Must be called only at safe points:
     * - When transport is stopped
     * - After track add/remove operations
     * - Before audio processing resumes
     *
     * Assigns dense slot indices (0, 1, 2...) to tracks in order.
     * Master channel always uses MASTER_SLOT_INDEX (127).
     *
     * @param tracks Vector of track pointers (order determines slot assignment)
     */
    void rebuild(const std::vector<std::shared_ptr<MixerChannel>>& channels);

    /**
     * @brief Get the dense slot index for a channel ID.
     *
     * O(1) lookup. Safe to call from audio thread after rebuild().
     *
     * @param channelId The track/channel ID
     * @return Dense slot index, or INVALID_SLOT if not found
     */
    uint32_t getSlotIndex(uint32_t channelId) const;

    /**
     * @brief Get the channel ID for a dense slot index.
     *
     * O(1) lookup. Safe to call from audio thread after rebuild().
     *
     * @param slotIndex Dense slot index (0..MAX_CHANNEL_SLOTS-1)
     * @return Channel ID, or INVALID_SLOT if not found
     */
    uint32_t getChannelId(uint32_t slotIndex) const;

    /**
     * @brief Get the number of active channel slots.
     *
     * @return Number of channels mapped (excluding master)
     */
    uint32_t getChannelCount() const { return m_channelCount; }

    /**
     * @brief Check if a channel ID is mapped.
     *
     * @param channelId The track/channel ID
     * @return True if the channel has a valid slot assignment
     */
    bool hasChannel(uint32_t channelId) const;

    /**
     * @brief Clear all mappings.
     */
    void clear();

private:
    std::unordered_map<uint32_t, uint32_t> m_idToSlot;   ///< channelId -> slotIndex
    std::unordered_map<uint32_t, uint32_t> m_slotToId;   ///< slotIndex -> channelId
    uint32_t m_channelCount{0};                          ///< Number of mapped channels
};

} // namespace Audio
} // namespace Nomad
