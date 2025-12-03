// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/Track.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUISlider.h"
#include <memory>

namespace Nomad {
namespace Audio {

// Forward declaration
class TrackManager;

/**
 * @brief UI wrapper for Track class
 *
 * Provides UI interface for a Track, including controls for
 * volume, pan, mute, solo, and record functionality.
 */
class TrackUIComponent : public NomadUI::NUIComponent {
public:
    TrackUIComponent(std::shared_ptr<Track> track, TrackManager* trackManager = nullptr);
    ~TrackUIComponent() override;

    std::shared_ptr<Track> getTrack() const { return m_track; }
    
    // Callback for when solo is toggled (so parent can update all track UIs)
    void setOnSoloToggled(std::function<void(TrackUIComponent*)> callback) { m_onSoloToggledCallback = callback; }
    
    // Callback for when UI needs cache invalidation (button hover, etc.)
    void setOnCacheInvalidationNeeded(std::function<void()> callback) { m_onCacheInvalidationCallback = callback; }
    
    // Selection state
    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }
    
    // Timeline zoom settings
    void setPixelsPerBeat(float ppb) { m_pixelsPerBeat = ppb; }
    void setBeatsPerBar(int bpb) { m_beatsPerBar = bpb; }
    void setTimelineScrollOffset(float offset) { m_timelineScrollOffset = offset; }
    void setMaxTimelineExtent(double extent) { m_maxTimelineExtent = extent; }
    
    // UI state update (public so parent can refresh after clearing solos)
    void updateUI();

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    void onMouseEnter();
    void onMouseLeave();
    void onUpdate(double deltaTime);

private:
    std::shared_ptr<Track> m_track;
    TrackManager* m_trackManager; // For coordinating solo exclusivity
    bool m_selected = false; // Track selection state
    
    // Callbacks
    std::function<void(TrackUIComponent*)> m_onSoloToggledCallback;
    std::function<void()> m_onCacheInvalidationCallback;
    
    // Timeline settings (synced from TrackManagerUI)
    float m_pixelsPerBeat = 50.0f;
    int m_beatsPerBar = 4;
    float m_timelineScrollOffset = 0.0f;
    double m_maxTimelineExtent = 0.0; // Maximum timeline extent in seconds

    // UI Components
    std::shared_ptr<NomadUI::NUILabel> m_nameLabel;
    std::shared_ptr<NomadUI::NUILabel> m_durationLabel;  // Shows sample duration
    std::shared_ptr<NomadUI::NUIButton> m_muteButton;
    std::shared_ptr<NomadUI::NUIButton> m_soloButton;
    std::shared_ptr<NomadUI::NUIButton> m_recordButton;

    // UI callbacks
    void onVolumeChanged(float volume);
    void onPanChanged(float pan);
    void onMuteToggled();
    void onSoloToggled();
    void onRecordToggled();

    // Waveform rendering
    void drawWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds, 
                     float offsetRatio = 0.0f, float visibleRatio = 1.0f);
    void generateWaveformCache(int width, int height);
    
    // Sample clip container (FL Studio style)
    void drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds);
    
    // Waveform cache (regenerate only when audio data or size changes)
    std::vector<std::pair<float, float>> m_waveformCache; // min/max pairs per pixel
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    size_t m_cachedAudioDataSize = 0;
    
    // Playlist grid rendering
    void drawPlaylistGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds);

    // UI state
    void updateTrackNameColors(); // Update track name with bright colors based on number
};

} // namespace Audio
} // namespace Nomad
