// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/MixerChannel.h"
#include "../NomadAudio/include/ClipInstance.h"
#include "../NomadAudio/include/PlaylistModel.h"

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Common/MusicHelpers.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Core/NUISlider.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include <memory>
#include <map>

namespace Nomad {
namespace Audio {

// Forward declaration
class TrackManager;
 
/**
 * @brief View modes for the playlist
 */
enum class PlaylistMode {
    Clips,        // Regular clip view
    Automation    // Automation envelope view
};

/**
 * @brief UI wrapper for Track class
 *
 * Provides UI interface for a Track, including controls for
 * volume, pan, mute, solo, and record functionality.
 */
class TrackUIComponent : public NomadUI::NUIComponent {
    friend class TrackManagerUI; // Allow parent to access protected event handlers for global drag routing
public:
    TrackUIComponent(PlaylistLaneID laneId, std::shared_ptr<MixerChannel> channel, TrackManager* trackManager = nullptr);
    ~TrackUIComponent() override;

    PlaylistLaneID getLaneId() const { return m_laneId; }
    std::shared_ptr<MixerChannel> getMixerChannel() const { return m_channel; }
    
    // Legacy mapping (for easier refactoring transition)
    std::shared_ptr<MixerChannel> getTrack() const { return m_channel; }


    
    // Primary/Secondary lane status - primary draws controls, secondary only draws clip
    void setIsPrimaryForLane(bool isPrimary) { m_isPrimaryForLane = isPrimary; }
    bool isPrimaryForLane() const { return m_isPrimaryForLane; }
    
    // Callback for when solo is toggled (so parent can update all track UIs)
    void setOnSoloToggled(std::function<void(TrackUIComponent*)> callback) { m_onSoloToggledCallback = callback; }

    // Zebra Striping Support
    void setRowIndex(int index) { m_rowIndex = index; }
    
    // Callback for when UI needs cache invalidation (button hover, etc.)
    void setOnCacheInvalidationNeeded(std::function<void()> callback) { m_onCacheInvalidationCallback = callback; }
    
    // Callback for clip deletion (clip identity and ripple position for animation)
    void setOnClipDeleted(std::function<void(TrackUIComponent*, ClipInstanceID, NomadUI::NUIPoint)> callback) { m_onClipDeletedCallback = callback; }

    
    // Callback to check if split tool is active
    void setIsSplitToolActive(std::function<bool()> callback) { m_isSplitToolActiveCallback = callback; }
    
    // Callback for split action at a position
    void setOnSplitRequested(std::function<void(TrackUIComponent*, double)> callback) { m_onSplitRequestedCallback = callback; }
    
    // Callback for clip selection
    void setOnClipSelected(std::function<void(TrackUIComponent*, ClipInstanceID)> callback) { m_onClipSelectedCallback = callback; }

    // Callback for track selection
    void setOnTrackSelected(std::function<void(TrackUIComponent*, bool)> callback) { m_onTrackSelectedCallback = callback; }

    
    // Selection state
    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }
    
    // View mode support (v3.1)
    void setPlaylistMode(PlaylistMode mode) {
        if (m_playlistMode != mode) {
            m_playlistMode = mode;
            setDirty(true); // Invalidate cache
        }
    }
    PlaylistMode getPlaylistMode() const { return m_playlistMode; }
    
    // Timeline zoom settings
    // Timeline zoom settings
    void setPixelsPerBeat(float ppb) { m_pixelsPerBeat = ppb; }
    void setBeatsPerBar(int bpb) { m_beatsPerBar = bpb; }
    void setTimelineScrollOffset(float offset) { m_timelineScrollOffset = offset; }
    void setMaxTimelineExtent(double extent) { m_maxTimelineExtent = extent; }
    void setSnapSetting(NomadUI::SnapGrid snap) { m_snapSetting = snap; }
    
    // Loop state for visual rendering
    void setLoopEnabled(bool enabled) { m_loopEnabled = enabled; }
    void setLoopRegion(double startBeat, double endBeat) { m_loopStartBeat = startBeat; m_loopEndBeat = endBeat; }
    
    // Automation State Query for Parent (Global Drag Handling)
    bool isDraggingAutomation() const { return m_isDraggingPoint; }

    // Accessors
    std::shared_ptr<MixerChannel> getChannel() const { return m_channel; }
    const std::map<ClipInstanceID, NomadUI::NUIRect>& getAllClipBounds() const { return m_allClipBounds; }

    // UI state update (public so parent can refresh after clearing solos)
    void updateUI();
    void renderControlOverlay(NomadUI::NUIRenderer& renderer);

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    void onMouseEnter();
    void onMouseLeave();
    void onUpdate(double deltaTime);

private:
    TrackManager* m_trackManager; // For coordinating solo exclusivity
    bool m_selected = false; // Track selection state
    bool m_isPrimaryForLane = true; // Primary draws control area, secondary only draws clip

    
    // Callbacks
    std::function<void(TrackUIComponent*)> m_onSoloToggledCallback;
    std::function<void()> m_onCacheInvalidationCallback;
    std::function<void(TrackUIComponent*, ClipInstanceID, NomadUI::NUIPoint)> m_onClipDeletedCallback;

    std::function<bool()> m_isSplitToolActiveCallback;
    std::function<void(TrackUIComponent*, double)> m_onSplitRequestedCallback;
    std::function<void(TrackUIComponent*, ClipInstanceID)> m_onClipSelectedCallback;
    std::function<void(TrackUIComponent*, bool)> m_onTrackSelectedCallback;

    
    
    // Timeline settings (synced from TrackManagerUI)
    float m_pixelsPerBeat = 50.0f;
    int m_beatsPerBar = 4;
    int m_rowIndex = 0; // For zebra striping
    float m_timelineScrollOffset = 0.0f;
    double m_maxTimelineExtent = 0.0; // Maximum timeline extent in seconds
    
    // Snap Setting
    NomadUI::SnapGrid m_snapSetting = NomadUI::SnapGrid::Bar;
    
    // Loop state for visual rendering
    bool m_loopEnabled = false;
    double m_loopStartBeat = 0.0;
    double m_loopEndBeat = 4.0;
    
    // Clip dragging state
    bool m_clipDragPotential = false;     // Potential drag detected (mousedown on clip)
    bool m_isDraggingClip = false;        // Active drag in progress
    NomadUI::NUIPoint m_clipDragStartPos; // Where drag started
    NomadUI::NUIRect m_clipBounds;        // Cached clip bounds for hit testing (primary track)
    
    // Multi-clip bounds for hit testing (maps ClipInstanceID to its rendered bounds)
    std::map<ClipInstanceID, NomadUI::NUIRect> m_allClipBounds;
    ClipInstanceID m_activeClipId;  // Currently clicked/dragged clip id

    
    // Clip trimming state (edge resize)
    enum class TrimEdge { None, Left, Right };
    TrimEdge m_trimEdge = TrimEdge::None;     // Which edge is being dragged
    bool m_isTrimming = false;                // True during trim operation
    double m_trimOriginalStart = 0.0;         // Original trim start before drag
    double m_trimOriginalDuration = 0.0;      // Original trim duration before drag
    double m_trimOriginalEnd = 0.0;           // Original trim end before drag
    float m_trimDragStartX = 0.0f;            // Mouse X when trim started
    static constexpr float TRIM_EDGE_WIDTH = 8.0f;  // Pixels for edge hit detection
 
    // Automation Interaction State (v3.1)
    bool m_isDraggingPoint = false;
    int m_draggedPointIndex = -1;
    int m_draggedCurveIndex = -1;
    NomadUI::NUIPoint m_lastAutomationMousePos;

    // Optimization
    uint32_t m_backgroundTexture = 0;
    bool m_backgroundValid = false;
    NomadUI::NUIRect m_lastRenderBounds;
    uint64_t m_lastModelModId = 0;
    void invalidateCache() { m_backgroundValid = false; }
 
    PlaylistMode m_playlistMode = PlaylistMode::Clips;

	    // UI Components
	    std::shared_ptr<NomadUI::NUILabel> m_nameLabel;
	    std::shared_ptr<NomadUI::NUIButton> m_muteButton;
	    std::shared_ptr<NomadUI::NUIButton> m_soloButton;
	    std::shared_ptr<NomadUI::NUIButton> m_recordButton;

    // UI callbacks
    void onVolumeChanged(float volume);
    void onPanChanged(float pan);
    void onMuteToggled();
    void onSoloToggled();
    void onRecordToggled();

    void drawWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds, 
                     float offsetRatio = 0.0f, float visibleRatio = 1.0f);
    void drawWaveformForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                              const ClipInstance& clip, float offsetRatio = 0.0f, float visibleRatio = 1.0f);

    void generateWaveformCache(int width, int height);
    
    // Sample clip container (FL Studio style)
    void drawSampleClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds);
    void drawSampleClipForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds,
                                const NomadUI::NUIRect& fullClipBounds, const ClipInstance& clip);

    // Pattern clip rendering (FL Studio style)
    void drawPatternClipForClip(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& clipBounds,
                                 const NomadUI::NUIRect& fullClipBounds, const ClipInstance& clip);

    
    // Waveform cache (regenerate only when audio data or size changes)
    std::vector<std::pair<float, float>> m_waveformCache; // min/max pairs per pixel
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    size_t m_cachedAudioDataSize = 0;
    
    PlaylistLaneID m_laneId;
    std::shared_ptr<MixerChannel> m_channel;

    
    // Playlist grid rendering
    void drawPlaylistGrid(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds);
    
    // Helper to draw a single clip (waveform + container) at calculated position
    void drawClipAtPosition(NomadUI::NUIRenderer& renderer, const ClipInstance& clip,
                           const NomadUI::NUIRect& bounds, float controlAreaWidth);

    // Automation Layer (v3.1)
    void renderAutomationLayer(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds, float gridStartX);


    // UI state
    void updateTrackNameColors(); // Update track name with bright colors based on number
};

} // namespace Audio
} // namespace Nomad
