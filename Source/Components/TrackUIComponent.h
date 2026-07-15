// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerChannel.h"
#include "ClipInstance.h"
#include "PlaylistModel.h"
#include "WaveformCache.h"

#include "NUIComponent.h"
#include "NUIContextMenu.h"
#include "MusicHelpers.h"
#include "NUILabel.h"
#include "NUIButton.h"
#include "NUISlider.h"
#include "NUIDragDrop.h"
#include <memory>
#include <map>

namespace AestraUI {
class NUIPlatformBridge;
}

namespace Aestra {
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
class TrackUIComponent : public AestraUI::NUIComponent {
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
    void setOnClipDeleted(std::function<void(TrackUIComponent*, ClipInstanceID, AestraUI::NUIPoint)> callback) { m_onClipDeletedCallback = callback; }

    
    // Callback to check if split tool is active
    void setIsSplitToolActive(std::function<bool()> callback) { m_isSplitToolActiveCallback = callback; }
    
    // Callback for split action at a position
    void setOnSplitRequested(std::function<void(TrackUIComponent*, double)> callback) { m_onSplitRequestedCallback = callback; }
    
    // Callback for clip selection
    void setOnClipSelected(std::function<void(TrackUIComponent*, ClipInstanceID)> callback) { m_onClipSelectedCallback = callback; }
    void setOnPatternClipOpenRequested(std::function<void(PatternID)> callback) { m_onPatternClipOpenRequested = std::move(callback); }
    void setOnPatternClipDragStarted(std::function<void(PatternID)> callback) { m_onPatternClipDragStarted = std::move(callback); }

    // Callback for track selection
    void setOnTrackSelected(std::function<void(TrackUIComponent*, bool)> callback) { m_onTrackSelectedCallback = callback; }

    // Audition integration
    void setOnSendToAudition(std::function<void()> callback) { m_onSendToAuditionCallback = callback; }

    // Platform bridge for cursor capture (volume knob)
    void setPlatformBridge(AestraUI::NUIPlatformBridge* bridge) { m_platformBridge = bridge; }

    
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
    void setSnapSetting(AestraUI::SnapGrid snap) { m_snapSetting = snap; }
    
    // Loop state for visual rendering
    void setLoopEnabled(bool enabled) { m_loopEnabled = enabled; }
    void setLoopRegion(double startBeat, double endBeat) { m_loopStartBeat = startBeat; m_loopEndBeat = endBeat; }
    
    // Automation State Query for Parent (Global Drag Handling)
    bool isDraggingAutomation() const { return m_isDraggingPoint; }
    
    // Trim Edge Hover Query (for cursor icon)
    bool isHoveringTrimEdge() const { return m_hoverTrimEdge != TrimEdge::None; }
    bool isTrimming() const { return m_isTrimming; }

    // Accessors
    std::shared_ptr<MixerChannel> getChannel() const { return m_channel; }
    const std::map<ClipInstanceID, AestraUI::NUIRect>& getAllClipBounds() const { return m_allClipBounds; }

    // Loading state for visual feedback
    void setLoading(bool loading, float progress = 0.0f) { 
        if (m_isLoading != loading || std::abs(m_loadProgress - progress) > 0.01f) {
            m_isLoading = loading; 
            m_loadProgress = progress; 
            setDirty(true); 
        } 
    }

    // UI state update (public so parent can refresh after clearing solos)
    void updateUI();
    void renderControlOverlay(AestraUI::NUIRenderer& renderer);

    // Split rendering for optimization (Static = Cached, Dynamic = Real-time)
    void renderStatic(AestraUI::NUIRenderer& renderer);
    void renderDynamic(AestraUI::NUIRenderer& renderer);

protected:
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    void onMouseEnter();
    void onMouseLeave();
    void onUpdate(double deltaTime);

private:
    TrackManager* m_trackManager; // For coordinating solo exclusivity
    bool m_selected = false; // Track selection state
    bool m_isPrimaryForLane = true; // Primary draws control area, secondary only draws clip
    bool m_isLoading = false;
    float m_loadProgress = 0.0f;

    
    // Callbacks
    std::function<void(TrackUIComponent*)> m_onSoloToggledCallback;
    std::function<void()> m_onCacheInvalidationCallback;
    std::function<void(TrackUIComponent*, ClipInstanceID, AestraUI::NUIPoint)> m_onClipDeletedCallback;

    std::function<bool()> m_isSplitToolActiveCallback;
    std::function<void(TrackUIComponent*, double)> m_onSplitRequestedCallback;
    std::function<void(TrackUIComponent*, ClipInstanceID)> m_onClipSelectedCallback;
    std::function<void(PatternID)> m_onPatternClipOpenRequested;
    std::function<void(PatternID)> m_onPatternClipDragStarted;
    std::function<void(TrackUIComponent*, bool)> m_onTrackSelectedCallback;
    std::function<void()> m_onSendToAuditionCallback;

    
    
    // Timeline settings (synced from TrackManagerUI)
    float m_pixelsPerBeat = 50.0f;
    int m_beatsPerBar = 4;
    int m_rowIndex = 0; // For zebra striping
    float m_timelineScrollOffset = 0.0f;
    double m_maxTimelineExtent = 0.0; // Maximum timeline extent in seconds
    
    // Snap Setting
    AestraUI::SnapGrid m_snapSetting = AestraUI::SnapGrid::Bar;
    
    // Loop state for visual rendering
    bool m_loopEnabled = false;
    double m_loopStartBeat = 0.0;
    double m_loopEndBeat = 4.0;
    
    // Clip dragging state
    bool m_clipDragPotential = false;     // Potential drag detected (mousedown on clip)
    bool m_isDraggingClip = false;        // Active drag in progress
    AestraUI::NUIPoint m_clipDragStartPos; // Where drag started
    AestraUI::NUIRect m_clipBounds;        // Cached clip bounds for hit testing (primary track)
    
    // Multi-clip bounds for hit testing (maps ClipInstanceID to its rendered bounds)
    std::map<ClipInstanceID, AestraUI::NUIRect> m_allClipBounds;
    ClipInstanceID m_activeClipId;  // Currently clicked/dragged clip id
    ClipInstanceID m_lastClickedClipId;
    long long m_lastClipClickTimeMs = 0;

    
    // Clip trimming state (edge resize)
    enum class TrimEdge { None, Left, Right };
    TrimEdge m_trimEdge = TrimEdge::None;     // Which edge is being dragged
    TrimEdge m_hoverTrimEdge = TrimEdge::None; // Which edge is being hovered (for cursor)
    bool m_isTrimming = false;                // True during trim operation
    double m_trimOriginalStart = 0.0;         // Original trim start before drag
    double m_trimOriginalDuration = 0.0;      // Original trim duration before drag
    double m_trimOriginalEnd = 0.0;           // Original trim end before drag
    float m_trimDragStartX = 0.0f;            // Mouse X when trim started
    static constexpr float TRIM_EDGE_WIDTH = 8.0f;  // Pixels for edge hit detection
    
    // Snap helper for trimming
    double snapBeatToGrid(double beat) const;
    double getSnapGridSizeBeats() const;
 
    // Automation Interaction State (v3.1)
    bool m_isDraggingPoint = false;
    bool m_isDraggingVolumeFader = false;
    int m_draggedPointIndex = -1;
    int m_draggedCurveIndex = -1;
    AestraUI::NUIPoint m_lastAutomationMousePos;

    // Which automation target the user is editing. Point edits (add/move/delete)
    // route to the lane curve carrying this target; the Vol/Pan chips drawn at the
    // top-left of the automation lane switch it. Was previously hardcoded to the
    // first curve (Volume only) — see #468.
    Aestra::Audio::AutomationTarget m_activeAutomationTarget = Aestra::Audio::AutomationTarget::Volume;

    // Resolve the lane curve matching m_activeAutomationTarget. When createIfMissing
    // is true a curve for that target is appended (used on first point add); returns
    // nullptr if the lane is gone or the curve is absent and creation was not asked.
    Aestra::Audio::AutomationCurve* activeAutomationCurve(bool createIfMissing);
    // Per-target curve color; dimmed when the target is not the active one.
    AestraUI::NUIColor automationTargetColor(Aestra::Audio::AutomationTarget target, bool active) const;
    // Screen rect of the target-selector chip at slot `index` (0=Vol, 1=Pan) within
    // the automation grid area. Same math is used for drawing and hit-testing.
    AestraUI::NUIRect automationTargetChipRect(int index, const AestraUI::NUIRect& gridArea) const;

    // Optimization
    uint32_t m_backgroundTexture = 0;
    bool m_backgroundValid = false;
    AestraUI::NUIRect m_lastRenderBounds;
    uint64_t m_lastModelModId = 0;
    void invalidateCache() { m_backgroundValid = false; }

    PlaylistMode m_playlistMode = PlaylistMode::Clips;

    // UI Components
    std::shared_ptr<AestraUI::NUILabel> m_nameLabel;
    std::shared_ptr<AestraUI::NUISlider> m_volumeFader;
    std::shared_ptr<AestraUI::NUIButton> m_muteButton;
    std::shared_ptr<AestraUI::NUIButton> m_soloButton;
    std::shared_ptr<AestraUI::NUIButton> m_recordButton;
    std::shared_ptr<AestraUI::NUIContextMenu> m_recordModeMenu;

    // Volume Knob (replaces route button)
    float m_volumeKnobValue = 1.0f;
    bool m_isDraggingVolumeKnob = false;
    bool m_volumeKnobHovered = false;
    AestraUI::NUIPoint m_volumeKnobDragStartPos;
    float m_volumeKnobDragStartValue = 0.0f;
    AestraUI::NUIRect m_volumeKnobBounds;

    // Cursor capture state for volume knob (hidden cursor + lock-on)
    AestraUI::NUIPlatformBridge* m_platformBridge = nullptr;
    AestraUI::NUIPoint m_volumeWarpOrigin;
    float m_volumeLastDragY = 0.0f;

    // UI callbacks
    void onVolumeChanged(float volume);
    void onPanChanged(float pan);
    void onMuteToggled();
    void onSoloToggled();
    void onRecordToggled();
    void showRecordModeMenu(const AestraUI::NUIPoint& position);
    void updateRecordTooltip();

    void drawWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                     float offsetRatio = 0.0f, float visibleRatio = 1.0f);
    void drawWaveformForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                              const ClipInstance& clip, float offsetRatio = 0.0f, float visibleRatio = 1.0f);

    void generateWaveformCache(int width, int height);

    // Shared clip display color (bright track palette / channel / clip fallback)
    AestraUI::NUIColor resolveClipDisplayColor(const ClipInstance& clip) const;

    // Zoom-aware waveform drawing helpers
    void drawChannelWaveform(AestraUI::NUIRenderer& renderer, float x, float y, float w, float h,
                             const std::vector<Aestra::Audio::WaveformPeak>& peaks,
                             const AestraUI::NUIColor& tint);
    void drawCombinedWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                              const std::vector<Aestra::Audio::WaveformPeak>& peaksL,
                              const std::vector<Aestra::Audio::WaveformPeak>& peaksR, size_t numChannels,
                              const AestraUI::NUIColor& tint);

    // Deep-zoom helpers: render finer than the peak cache's base mip level.
    // Both read bounded sample ranges directly from the (immutable) source buffer.
    void drawSampleWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds,
                            const Aestra::Audio::AudioBufferData& buffer, double startFrame, double endFrame,
                            const AestraUI::NUIColor& tint);
    static void computeDirectPeaks(const Aestra::Audio::AudioBufferData& buffer, uint32_t channel,
                                   size_t startFrame, size_t endFrame, int numColumns,
                                   std::vector<Aestra::Audio::WaveformPeak>& outPeaks);

    // Sample clip container
    void drawSampleClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds);
    void drawSampleClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip);

    // Pattern clip rendering
    void drawPatternClipForClip(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& clipBounds,
                                 const AestraUI::NUIRect& fullClipBounds, const ClipInstance& clip);

    // Reusable peak buffers to avoid per-frame allocations
    std::vector<Aestra::Audio::WaveformPeak> m_waveformPeaksL;
    std::vector<Aestra::Audio::WaveformPeak> m_waveformPeaksR;
    std::vector<Aestra::Audio::WaveformPeak> m_waveformPeaksMerged;
    std::vector<AestraUI::NUIPoint> m_waveformTopPts;
    std::vector<AestraUI::NUIPoint> m_waveformBottomPts;
    
    PlaylistLaneID m_laneId;
    std::shared_ptr<MixerChannel> m_channel;

    
    // Playlist grid rendering
    void drawPlaylistGrid(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds);
    
    // Helper to draw a single clip (waveform + container) at calculated position
    void drawClipAtPosition(AestraUI::NUIRenderer& renderer, const ClipInstance& clip,
                           const AestraUI::NUIRect& bounds, float controlAreaWidth);

    // Live Waveform (v3.0.2)
    void drawLiveWaveform(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float controlAreaWidth);

    // Automation Layer (v3.1)
    void renderAutomationLayer(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX);


    // UI state
    void updateTrackNameColors(); // Update track name with bright colors based on number
};

} // namespace Audio
} // namespace Aestra
