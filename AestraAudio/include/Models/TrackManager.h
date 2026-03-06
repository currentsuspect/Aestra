#pragma once
#include "../Core/ChannelSlotMap.h"
#include "MeterSnapshot.h"
#include "MixerChannel.h"
#include "PatternManager.h"
#include "PlaylistModel.h"
#include "SourceManager.h"
#include "UnitManager.h"

#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

// Forward declarations
struct MeterSnapshots;

/**
 * @brief Track/Channel manager for the audio engine
 */
class TrackManager {
public:
    TrackManager() = default;

    /**
     * @brief Get the number of channels
     */
    size_t getChannelCount() const { return m_channels.size(); }

    /**
     * @brief Get a channel by index
     */
    MixerChannel* getChannel(size_t index) {
        if (index < m_channels.size()) {
            return m_channels[index].get();
        }
        return nullptr;
    }

    const MixerChannel* getChannel(size_t index) const {
        if (index < m_channels.size()) {
            return m_channels[index].get();
        }
        return nullptr;
    }

    /**
     * @brief Add a new channel
     */
    void addChannel(const std::string& name = "") {
        auto channel =
            std::make_unique<MixerChannel>(name.empty() ? "Track " + std::to_string(m_channels.size() + 1) : name,
                                           static_cast<uint32_t>(m_channels.size()));
        m_channels.push_back(std::move(channel));
    }

    /**
     * @brief Get the playlist model
     */
    PlaylistModel& getPlaylistModel() { return m_playlistModel; }

    const PlaylistModel& getPlaylistModel() const { return m_playlistModel; }

    /**
     * @brief Get the pattern manager
     */
    PatternManager& getPatternManager() { return m_patternManager; }

    const PatternManager& getPatternManager() const { return m_patternManager; }

    /**
     * @brief Get the source manager
     */
    SourceManager& getSourceManager() { return m_sourceManager; }

    const SourceManager& getSourceManager() const { return m_sourceManager; }

    /**
     * @brief Get the unit manager (Arsenal)
     */
    UnitManager& getUnitManager() { return m_unitManager; }

    const UnitManager& getUnitManager() const { return m_unitManager; }

    /**
     * @brief Set output sample rate
     */
    void setOutputSampleRate(double rate) { m_outputSampleRate = rate; }

    /**
     * @brief Set input sample rate
     */
    void setInputSampleRate(double rate) { m_inputSampleRate = rate; }

    /**
     * @brief Set input channel count
     */
    void setInputChannelCount(int count) { m_inputChannelCount = count; }

    /**
     * @brief Set meter snapshots buffer
     */
    void setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) { m_meterSnapshots = snapshots; }

    /**
     * @brief Get meter snapshots
     */
    std::shared_ptr<MeterSnapshotBuffer> getMeterSnapshots() const { return m_meterSnapshots; }

    // STUB: getContinuousParams — Phase 2 will return real-time automation parameter buffer
    std::shared_ptr<ContinuousParamBuffer> getContinuousParams() const { return m_continuousParams; }

    /**
     * @brief Get channel slot map
     */
    std::shared_ptr<ChannelSlotMap> getChannelSlotMapShared() const { return m_channelSlotMap; }

    /**
     * @brief Set channel slot map
     */
    void setChannelSlotMapShared(std::shared_ptr<ChannelSlotMap> slotMap) { m_channelSlotMap = slotMap; }

    /**
     * @brief Set playhead position
     */
    void setPosition(double position) { m_position = position; }

    /**
     * @brief Get playhead position
     */
    double getPosition() const { return m_position; }

private:
    std::vector<std::unique_ptr<MixerChannel>> m_channels;
    PlaylistModel m_playlistModel;
    PatternManager m_patternManager;
    SourceManager m_sourceManager;

    double m_outputSampleRate{48000.0};
    double m_inputSampleRate{48000.0};
    int m_inputChannelCount{0};
    double m_position{0.0};
    std::shared_ptr<MeterSnapshotBuffer> m_meterSnapshots;
    std::shared_ptr<ContinuousParamBuffer> m_continuousParams; // STUB: Phase 2
    std::shared_ptr<ChannelSlotMap> m_channelSlotMap;
    UnitManager m_unitManager;
};

} // namespace Audio
} // namespace Aestra