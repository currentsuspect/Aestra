// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Widgets/NUIMixerWidgets.h"
#include "TrackManager.h"
#include <memory>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Channel strip component - represents one track in the mixer
 * Shows volume fader, pan control, mute/solo buttons, level meter
 */
class ChannelStrip : public AestraUI::NUIComponent {
public:
    ChannelStrip(std::shared_ptr<Track> track, TrackManager* trackManager = nullptr);
    
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    
    void setTrack(std::shared_ptr<Track> track) { m_track = track; }
    std::shared_ptr<Track> getTrack() const { return m_track; }

private:
    std::shared_ptr<Track> m_track;
    TrackManager* m_trackManager; // For coordinating solo exclusivity
    
    // UI Controls
    std::shared_ptr<AestraUI::Fader> m_volumeFader;
    std::shared_ptr<AestraUI::PanKnob> m_panKnob;
    std::shared_ptr<AestraUI::MuteButton> m_muteButton;
    std::shared_ptr<AestraUI::SoloButton> m_soloButton;
    
    // Level meter state
    float m_peakLevel{0.0f};
    float m_peakDecay{0.0f};
    
    // Visual mapping
    PlaylistLaneID m_laneId; 
    
    void layoutControls();
    
public:
    void setPlaylistLaneID(PlaylistLaneID id) { m_laneId = id; }
};

/**
 * @brief Mixer view - shows all tracks as channel strips
 * Similar to a traditional mixing console
 */
class MixerView : public AestraUI::NUIComponent {
public:
    MixerView(std::shared_ptr<TrackManager> trackManager);
    
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    
    void refreshChannels();  // Rebuild channel strips when tracks change

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<ChannelStrip>> m_channelStrips;
    
    float m_channelWidth{80.0f};  // Width of each channel strip
    float m_scrollOffset{0.0f};   // Horizontal scroll offset
    
    void layoutChannels();
};

} // namespace Audio
} // namespace Aestra
