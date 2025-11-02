// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUISlider.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadAudio/include/TrackManager.h"
#include <memory>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief Channel strip component - represents one track in the mixer
 * Shows volume fader, pan control, mute/solo buttons, level meter
 */
class ChannelStrip : public NomadUI::NUIComponent {
public:
    ChannelStrip(std::shared_ptr<Track> track);
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    
    void setTrack(std::shared_ptr<Track> track) { m_track = track; }
    std::shared_ptr<Track> getTrack() const { return m_track; }

private:
    std::shared_ptr<Track> m_track;
    
    // UI Controls
    std::shared_ptr<NomadUI::NUISlider> m_volumeFader;
    std::shared_ptr<NomadUI::NUISlider> m_panKnob;
    std::shared_ptr<NomadUI::NUIButton> m_muteButton;
    std::shared_ptr<NomadUI::NUIButton> m_soloButton;
    
    // Level meter state
    float m_peakLevel{0.0f};
    float m_peakDecay{0.0f};
    
    void layoutControls();
};

/**
 * @brief Mixer view - shows all tracks as channel strips
 * Similar to a traditional mixing console
 */
class MixerView : public NomadUI::NUIComponent {
public:
    MixerView(std::shared_ptr<TrackManager> trackManager);
    
    void onRender(NomadUI::NUIRenderer& renderer) override;
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
} // namespace Nomad
