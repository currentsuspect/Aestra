// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/Track.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUISlider.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include <memory>
#include <map>

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
    
    // Multi-clip support: add additional clips to render on this same lane
    void addLaneClip(std::shared_ptr<Track> clip) { m_laneClips.push_back(clip); }
    void clearLaneClips() { m_laneClips.clear(); }
    const std::vector<std::shared_ptr<Track>>& getLaneClips() const { return m_laneClips; }
    
    // Get all clips (primary + lane clips) for this lane
    std::vector<std::shared_ptr<Track>> getAllClips() const {
        std::vector<std::shared_ptr<Track>> all;
        if (m_track) all.push_back(m_track);
        all.insert(all.end(), m_laneClips.begin(), m_laneClips.end());
        return all;
    }
    
    // Primary/Secondary lane status - primary draws controls, secondary only draws clip
    void setIsPrimaryForLane(bool isPrimary) { m_isPrimaryForLane = isPrimary; }
    bool isPrimaryForLane() const { return m_isPrimaryForLane; }
    
    // Callback for when solo is toggled (so parent can update all track UIs)
    void setOnSoloToggled(std::function<void(TrackUIComponent*)> callback) { m_onSoloToggledCallback = callback; }
    
    // Callback for when UI needs cache invalidation (button hover, etc.)
    void setOnCacheInvalidationNeeded(std::function<void()> callback) { m_onCacheInvalidationCallback = callback; }
    
    // Callback for clip deletion (ripple position for animation)
    void setOnClipDeleted(std::function<void(TrackUIComponent*, NomadUI::NUIPoint)> callback) { m_onClipDeletedCallback = callback; }
    
    // Callback to check if split tool is active
    void setIsSplitToolActive(std::function<bool()> callback) { m_isSplitToolActiveCallback = callback; }
    
    // Callback for split action at a position
    void setOnSplitRequested(std::function<void(TrackUIComponent*, double)> callback) { m_onSplitRequestedCallback = callback; }
    
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
    bool m_isPrimaryForLane = true; // Primary draws control area, secondary only draws clip
    
    // Callbacks
    std::function<void(TrackUIComponent*)> m_onSoloToggledCallback;
    std::function<void()> m_onCacheInvalidationCallback;
    std::function<void(TrackUIComponent*, NomadUI::NUIPoint)> m_onClipDeletedCallback;
    std::function<bool()> m_isSplitToolActiveCallback;
    std::function<void(TrackUIComponent*, double)> m_onSplitRequestedCallback;
    
    // Timeline settings (synced from TrackManagerUI)
    float m_pixelsPerBeat = 50.0f;
    int m_beatsPerBar = 4;
    float m_timelineScrollOffset = 0.0f;
    double m_maxTimelineExtent = 0.0; // Maximum timeline extent in seconds
    
    // Clip dragging state
    bool m_clipDragPotential = false;     // Potential drag detected (mousedown on clip)
    bool m_isDraggingClip = false;        // Active drag in progress
    NomadUI::NUIPoint m_clipDragStartPos; // Where drag started
    NomadUI::NUIRect m_clipBounds;        // Cached clip bounds for hit testing (primary track)
    
    // Multi-clip bounds for hit testing (maps Track pointer to its rendered bounds)
    std::map<std::shared_ptr<Track>, NomadUI::NUIRect> m_allClipBounds;
    std::shared_ptr<Track> m_activeClip;  // Currently clicked/dragged clip (may be primary or lane clip)
    
    // Clip trimming state (edge resize)
    enum class TrimEdge { None, Left, Right };
    TrimEdge m_trimEdge = TrimEdge::None;     // Which edge is being dragged
    bool m_isTrimming = false;                // True during trim operation
    double m_trimOriginalStart = 0.0;         // Original trim start before drag
    double m_trimOriginalEnd = 0.0;           // Original trim end before drag
    float m_trimDragStartX = 0.0f;            // Mouse X when trim started
    static constexpr float TRIM_EDGE_WIDTH = 8.0f;  // Pixels for edge hit detection

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
    void drawWaveformForTrack(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                              std::shared_ptr<Track> track, float offsetRatio = 0.0f, float visibleRatio = 1.0f);
    void generateWaveformCache(int width, int height);
    
    // Sample clip container (FL Studio style)
    void drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds);
    void drawSampleClipForTrack(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds, std::shared_ptr<Track> track);
    
    // Waveform cache (regenerate only when audio data or size changes)
    std::vector<std::pair<float, float>> m_waveformCache; // min/max pairs per pixel
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    size_t m_cachedAudioDataSize = 0;
    
    // Multi-clip support: additional clips on the same lane
    std::vector<std::shared_ptr<Track>> m_laneClips;
    
    // Playlist grid rendering
    void drawPlaylistGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds);
    
    // Helper to draw a single clip (waveform + container) at calculated position
    void drawClipAtPosition(NomadUI::NUIRenderer& renderer, std::shared_ptr<Track> clip,
                           const NomadUI::NUIRect& bounds, float controlAreaWidth);

    // UI state
    void updateTrackNameColors(); // Update track name with bright colors based on number
};

} // namespace Audio
} // namespace Nomad
