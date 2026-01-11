// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "../NomadAudio/include/ClipInstance.h"
#include "../NomadAudio/include/PlaylistModel.h"
#include "TrackUIComponent.h"
#include "PianoRollPanel.h"
#include "MixerPanel.h"
#include "StepSequencerPanel.h"
#include "TimelineMinimapBar.h"
#include "TimelineMinimapModel.h"
#include "TimelineSummaryCache.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIScrollbar.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/OpenGL/NUIRenderCache.h"
#include "../NomadUI/Common/MusicHelpers.h"
#include "../NomadUI/Widgets/NUIDropdown.h" // Full type for shared_ptr usage
#include <memory>
#include <vector>
#include <unordered_set>

namespace NomadUI { class NUIPlatformBridge; }

namespace Nomad {
namespace Audio {

/**
 * @brief Tool modes for playlist editing (FL Studio style)
 */
enum class PlaylistTool {
    Select,     // Default - select/move clips  
    Split,      // Blade tool - click to split clips
    MultiSelect,// Rectangle selection for multiple clips
    Loop,       // Loop region tool
    Draw,       // Draw automation/MIDI
    Erase,      // Erase clips/notes
    Mute,       // Click to mute clips
    Slip        // Adjust content within clip bounds
};

/**
 * @brief UI wrapper for TrackManager
 *
 * Provides visual track management interface with:
 * - Track layout and scrolling
 * - Add/remove track functionality
 * - Visual timeline integration
 * - Drag-and-drop support for files and clips
 */
class TrackManagerUI : public ::NomadUI::NUIComponent, public ::NomadUI::IDropTarget {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    void setPlatformWindow(::NomadUI::NUIPlatformBridge* window);
    ::NomadUI::NUIPlatformBridge* getPlatformWindow() const { return m_window; }

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();
    void invalidateAllCaches();
    
    void invalidateCache(); // Keep for compatibility
    
    // Solo coordination (exclusive solo behavior)
    void onTrackSoloToggled(TrackUIComponent* soloedTrack);
    
    void onClipDeleted(TrackUIComponent* trackComp, ClipInstanceID clipId, const ::NomadUI::NUIPoint& rippleCenter);
    
    // Clip splitting (split tool)
    void onSplitRequested(TrackUIComponent* trackComp, double splitBeat);

    // Playlist View
    void togglePlaylist() { if (m_onTogglePlaylist) m_onTogglePlaylist(); }
    void setPlaylistVisible(bool visible);
    bool isPlaylistVisible() const { return m_playlistVisible; }
    
    // === TOOL SELECTION ===
    void setCurrentTool(PlaylistTool tool);
    void setActiveTool(PlaylistTool tool) { setCurrentTool(tool); }  // Alias
    PlaylistTool getCurrentTool() const { return m_currentTool; }
    PlaylistTool getActiveTool() const { return m_currentTool; }  // Alias
 
    // === VIEW MODES ===
    void setPlaylistMode(PlaylistMode mode);
    PlaylistMode getPlaylistMode() const { return m_playlistMode; }
    
    // Cursor visibility callback (for custom cursor support)
    void setOnCursorVisibilityChanged(std::function<void(bool)> callback) { m_onCursorVisibilityChanged = callback; }
    
    // View Toggle Callbacks (v3.1)
    void setOnToggleMixer(std::function<void()> cb) { m_onToggleMixer = cb; }
    void setOnTogglePianoRoll(std::function<void()> cb) { m_onTogglePianoRoll = cb; }
    void setOnToggleSequencer(std::function<void()> cb) { m_onToggleSequencer = cb; }
    void setOnTogglePlaylist(std::function<void()> cb) { m_onTogglePlaylist = cb; }
    
    // Loop control callback (preset: 0=Off, 1=1Bar, 2=2Bars, 3=4Bars, 4=8Bars, 5=Selection)
    void setOnLoopPresetChanged(std::function<void(int preset)> cb) { m_onLoopPresetChanged = cb; }
    int getLoopPreset() const { return m_loopPreset; }
    
    // === MULTI-SELECTION ===
    void selectTrack(TrackUIComponent* track, bool addToSelection = false);
    void deselectTrack(TrackUIComponent* track);
    void selectAllTracks();
    void clearSelection();
    const std::unordered_set<TrackUIComponent*>& getSelectedTracks() const { return m_selectedTracks; }
    bool isTrackSelected(TrackUIComponent* track) const;
    
    // Snap-to-Grid control
    void setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
    bool isSnapEnabled() const { return m_snapEnabled; }
    void setSnapDivision(int division) { m_snapDivision = division; } // 1=bar, 4=beat, 16=16th
    int getSnapDivision() const { return m_snapDivision; }
    
    // New Snap System
    void setSnapSetting(::NomadUI::SnapGrid snap);
    ::NomadUI::SnapGrid getSnapSetting() const { return m_snapSetting; }
    
    // === CLIP MANIPULATION ===
    void splitSelectedClipAtPlayhead();  // Split clip at current playhead position
    void copySelectedClip();             // Copy selected clip to clipboard
    void cutSelectedClip();              // Cut selected clip (copy + delete)
    void pasteClip();                    // Paste clipboard at playhead position
    void duplicateSelectedClip();        // Duplicate selected clip immediately after
    void deleteSelectedClip();           // Delete selected clip
    TrackUIComponent* getSelectedTrackUI() const;  // Get currently selected track UI
    
    // === IDropTarget Interface ===
    ::NomadUI::DropFeedback onDragEnter(const ::NomadUI::DragData& data, const ::NomadUI::NUIPoint& position) override;
    ::NomadUI::DropFeedback onDragOver(const ::NomadUI::DragData& data, const ::NomadUI::NUIPoint& position) override;
    void onDragLeave() override;
    ::NomadUI::DropResult onDrop(const ::NomadUI::DragData& data, const ::NomadUI::NUIPoint& position) override;
    ::NomadUI::NUIRect getDropBounds() const override { return getBounds(); }
    
    // Loop markers (FL Studio-style visual feedback)
    void setLoopRegion(double startBeat, double endBeat, bool enabled);

    bool onMouseEvent(const ::NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const ::NomadUI::NUIKeyEvent& event) override;

    // Selection query for looping
    std::pair<double, double> getSelectionBeatRange() const;
    
    // Time Signature Sync
    void setBeatsPerBar(int bpb) {
        if (m_beatsPerBar == bpb) return;
        m_beatsPerBar = bpb;
        for(auto& track : m_trackUIComponents) {
            if(track) track->setBeatsPerBar(bpb);
        }
        setDirty(true);
    }

protected:
    void onRender(::NomadUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    
    // Hide setDirty to trigger cache invalidation (except during cache rendering)
    void setDirty(bool dirty = true) {
        ::NomadUI::NUIComponent::setDirty(dirty);
        if (dirty && !m_isRenderingToCache) {
            m_cacheInvalidated = true;
        }
    }
    
    // 🔥 VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(::NomadUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;
    ::NomadUI::NUIPlatformBridge* m_window = nullptr;

    // UI Layout
    int m_trackHeight{48};
    int m_trackSpacing{4}; // 8px grid spacing scale (S1)
    float m_scrollOffset{0.0f};
    PlaylistMode m_playlistMode{PlaylistMode::Clips};
    
    // Timeline/Ruler settings
    float m_pixelsPerBeat{50.0f};      // Horizontal zoom level
    float m_timelineScrollOffset{0.0f}; // Horizontal scroll position
    int m_beatsPerBar{4};               // Time signature numerator
    int m_subdivision{4};               // Grid subdivision (4 = 16th notes)
    ::NomadUI::SnapGrid m_snapSetting = ::NomadUI::SnapGrid::Bar;
    
    // Legacy Snap (Check if used)
    bool m_snapEnabled = true;
    int m_snapDivision = 4;
    
    // UI Components
    std::shared_ptr<::NomadUI::NUIScrollbar> m_scrollbar;
    std::shared_ptr<::NomadUI::TimelineMinimapBar> m_timelineMinimap;
    std::shared_ptr<::NomadUI::NUIIcon> m_addTrackIcon;
    ::NomadUI::NUIRect m_addTrackBounds;
    bool m_addTrackHovered = false;

    // Timeline minimap state (beats-domain)
    ::NomadUI::TimelineSummaryCache m_timelineSummaryCache;
    ::NomadUI::TimelineSummarySnapshot m_timelineSummarySnapshot;
    ::NomadUI::TimelineMinimapMode m_minimapMode{::NomadUI::TimelineMinimapMode::Clips};
    ::NomadUI::TimelineMinimapAggregation m_minimapAggregation{::NomadUI::TimelineMinimapAggregation::MaxPresence};
    double m_minimapDomainStartBeat{0.0};
    double m_minimapDomainEndBeat{0.0};
    double m_minimapShrinkCooldown{0.0};
    bool m_minimapNeedsRebuild{true};
    ::NomadUI::TimelineRange m_minimapSelectionBeatRange{};
    
    // Tool icons (toolbar)
    std::shared_ptr<::NomadUI::NUIIcon> m_selectToolIcon;
    std::shared_ptr<::NomadUI::NUIIcon> m_splitToolIcon;
    std::shared_ptr<::NomadUI::NUIIcon> m_multiSelectToolIcon;
    std::shared_ptr<::NomadUI::NUIDropdown> m_loopDropdown;  // Loop preset dropdown
    std::shared_ptr<::NomadUI::NUIDropdown> m_snapDropdown;  // Snap Dropdown
    ::NomadUI::NUIRect m_selectToolBounds;
    ::NomadUI::NUIRect m_splitToolBounds;
    ::NomadUI::NUIRect m_multiSelectToolBounds;
    ::NomadUI::NUIRect m_loopDropdownBounds;
    ::NomadUI::NUIRect m_snapDropdownBounds; // Bounds
    ::NomadUI::NUIRect m_toolbarBounds;
    bool m_selectToolHovered = false;
    bool m_splitToolHovered = false;
    bool m_multiSelectToolHovered = false;
    
    // Loop state
    int m_loopPreset{0};  // 0=Off, 1=1Bar, 2=2Bars, 3=4Bars, 4=8Bars, 5=Selection
    
    // Current editing tool
    PlaylistTool m_currentTool = PlaylistTool::Select;
    bool m_cursorHidden = false;  // Track cursor visibility state
    std::function<void(bool)> m_onCursorVisibilityChanged;
    
    // Multi-selection
    std::unordered_set<TrackUIComponent*> m_selectedTracks;
    
    // Instant clip dragging (no ghost)
    bool m_isDraggingClipInstant = false;
    TrackUIComponent* m_draggedClipTrack = nullptr;
    float m_clipDragOffsetX = 0.0f;  // Offset from clip start to mouse
    double m_clipOriginalStartTime = 0.0;  // Original position before drag
    int m_clipOriginalTrackIndex = -1;  // Original track before drag
    
    // Split tool cursor position
    float m_splitCursorX = 0.0f;
    bool m_showSplitCursor = false;
    ::NomadUI::NUIPoint m_lastMousePos;  // Track mouse for split cursor rendering
    
    // Playhead dragging state
    bool m_isDraggingPlayhead = false;
    
    // === RULER SELECTION (Right-click or Ctrl+Left-click on ruler for looping) ===
    bool m_isDraggingRulerSelection = false;
    double m_rulerSelectionStartBeat = 0.0;
    double m_rulerSelectionEndBeat = 0.0;
    bool m_hasRulerSelection = false;
    
    // === LOOP MARKERS (Visual feedback on ruler) ===
    bool m_loopEnabled = true;  // Default enabled (1-bar loop)
    double m_loopStartBeat = 0.0;
    double m_loopEndBeat = 4.0;
    bool m_isDraggingLoopStart = false;
    bool m_isDraggingLoopEnd = false;
    bool m_hoveringLoopStart = false;
    bool m_hoveringLoopEnd = false;
    double m_loopDragStartBeat = 0.0;  // Original beat position when drag started
    
    // === SELECTION BOX (Right-click drag or MultiSelect tool) ===
    bool m_isDrawingSelectionBox = false;
    ::NomadUI::NUIPoint m_selectionBoxStart;
    ::NomadUI::NUIPoint m_selectionBoxEnd;
    
    // === SMOOTH ZOOM ANIMATION (FL Studio style) ===
    float m_targetPixelsPerBeat = 50.0f;   // Target zoom level for animation (match initial m_pixelsPerBeat)
    float m_zoomVelocity = 0.0f;           // Current zoom velocity for momentum
    float m_lastMouseZoomX = 0.0f;         // Mouse X position during zoom for pivot
    bool m_isZooming = false;              // True while actively zooming
    bool m_dropTargetRegistered = false;   // Flag to ensure one-time registration
    
    // === FBO CACHING SYSTEM (like AudioSettingsDialog) ===
    ::NomadUI::CachedRenderData* m_cachedRender = nullptr;
    uint64_t m_cacheId;
    bool m_cacheInvalidated = true;  // Start invalidated to force initial render
    bool m_isRenderingToCache = false;  // Guard flag to prevent invalidation loops

    // Playlist View State
    bool m_playlistVisible{true};

    // ⚡ MULTI-LAYER CACHING SYSTEM for 60+ FPS
    
    // Layer 1: Static Background (grid, ruler ticks)
    uint32_t m_backgroundTextureId = 0;
    int m_backgroundCachedWidth = 0;
    int m_backgroundCachedHeight = 0;
    float m_backgroundCachedZoom = 0.0f;
    bool m_backgroundNeedsUpdate = true;
    
    
    // Layer 2: Track Controls (buttons, labels - semi-static)
    uint32_t m_controlsTextureId = 0;
    bool m_controlsNeedsUpdate = true;
    
    // Layer 3: Waveforms (per-track FBO caching)
    struct TrackCache {
        uint32_t textureId = 0;
        bool needsUpdate = true;
        double lastContentHash = 0; // Simple hash to detect content changes
    };
    std::vector<TrackCache> m_trackCaches;
    
    // Dirty flags for smart invalidation
    bool m_playheadMoved = false;        // Only redraw playhead overlay
    bool m_metersChanged = false;        // Only redraw audio meters
    
    // === DROP PREVIEW STATE ===
    bool m_showDropPreview = false;      // True when drag is over timeline
    int m_dropTargetTrack = -1;          // Track index for drop preview
    double m_dropTargetTime = 0.0;       // Time position for drop preview
    ::NomadUI::NUIRect m_dropPreviewRect;  // Visual preview rectangle
    
    // === SNAP-TO-GRID ===
    // === SNAP-TO-GRID (Legacy - preserved for compatibility but shadowed by m_snapSetting) ===
    // bool m_snapEnabled = true;           // Snap to grid enabled by default
    // int m_snapDivision = 4;              // Snap to beats (1=bar, 4=beat, 16=16th, etc.)
    
    // === CLIPBOARD for copy/paste (v3.0) ===
    struct ClipboardData {
        bool hasData = false;
        PatternID patternId;
        double durationBeats = 0.0;
        LocalEdits edits;
        std::string name;
        uint32_t colorRGBA = 0xFF4A90D9;
    };
    ClipboardData m_clipboard;
    
    ClipInstanceID m_selectedClipId; // Track single selected clip for manipulation

    
    // === DELETE ANIMATION (FL Studio ripple effect) ===
    struct DeleteAnimation {
        PlaylistLaneID laneId;            // Lane being deleted from
        ClipInstanceID clipId;            // Clip ID (for reference during animation if needed)
        ::NomadUI::NUIPoint rippleCenter;   // Center of ripple effect

        ::NomadUI::NUIRect clipBounds;      // Original clip bounds
        float progress = 0.0f;            // Animation progress 0.0-1.0
        float duration = 0.25f;           // Animation duration in seconds
    };
    std::vector<DeleteAnimation> m_deleteAnimations;
    
    // Callbacks for toggles
    std::function<void()> m_onToggleMixer;
    std::function<void()> m_onTogglePianoRoll;
    std::function<void()> m_onToggleSequencer;
    std::function<void()> m_onTogglePlaylist;
    std::function<void(int)> m_onLoopPresetChanged;  // Called when loop preset dropdown changes
    
    void updateBackgroundCache(::NomadUI::NUIRenderer& renderer);
    void updateControlsCache(::NomadUI::NUIRenderer& renderer);
    void updateTrackCache(::NomadUI::NUIRenderer& renderer, size_t trackIndex);

    void syncViewToggleButtons();
    void layoutTracks();
    void onAddTrackClicked();
    void updateTrackPositions();
    void updateScrollbar();
    void onScroll(double position);
    void onHorizontalScroll(double position);
    void deselectAllTracks();

    // Timeline minimap (top bar)
    void scheduleTimelineMinimapRebuild();
    void updateTimelineMinimap(double deltaTime);
    void setTimelineViewStartBeat(double viewStartBeat, bool isFinal);
    void resizeTimelineViewEdgeFromMinimap(::NomadUI::TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal);
    void centerTimelineViewAtBeat(double centerBeat);
    void zoomTimelineAroundBeat(double anchorBeat, float zoomMultiplier);
    float getTimelineGridWidthPixels() const;
    double secondsToBeats(double seconds) const;
    void renderTimeRuler(::NomadUI::NUIRenderer& renderer, const ::NomadUI::NUIRect& rulerBounds);
    void renderLoopMarkers(::NomadUI::NUIRenderer& renderer, const ::NomadUI::NUIRect& rulerBounds);
    void renderPlayhead(::NomadUI::NUIRenderer& renderer);
    void renderDropPreview(::NomadUI::NUIRenderer& renderer); // Render drop zone highlight
    void renderDeleteAnimations(::NomadUI::NUIRenderer& renderer); // Render FL-style ripple delete
    void renderTrackManagerDirect(::NomadUI::NUIRenderer& renderer);  // Direct rendering helper
    
    // Helper to convert mouse position to track/time
    int getTrackAtPosition(float y) const;
    double getTimeAtPosition(float x) const;
    void clearDropPreview(); // Clear drop preview state
    double snapBeatToGrid(double beat) const; // Snap beat to nearest grid line
    
    // Grid helper
    void drawGrid(::NomadUI::NUIRenderer& renderer, const ::NomadUI::NUIRect& bounds, float gridStartX, float gridWidth, float timelineScrollOffset);

    // Tool icons initialization and rendering
    void createToolIcons();
    void updateToolbarBounds();
    void renderToolbar(::NomadUI::NUIRenderer& renderer);
    bool handleToolbarClick(const ::NomadUI::NUIPoint& position);
    void renderSplitCursor(::NomadUI::NUIRenderer& renderer, const ::NomadUI::NUIPoint& position);
    void renderMinimapResizeCursor(::NomadUI::NUIRenderer& renderer, const ::NomadUI::NUIPoint& position);
    
    // Instant clip dragging
    void startInstantClipDrag(TrackUIComponent* clip, const ::NomadUI::NUIPoint& clickPos);
    void updateInstantClipDrag(const ::NomadUI::NUIPoint& currentPos);
    void finishInstantClipDrag();
    void cancelInstantClipDrag();
    
    // Split tool
    void performSplitAtPosition(int trackIndex, double timeSeconds);
    
    // Calculate maximum timeline extent based on samples
    double getMaxTimelineExtent() const;

    // (Duplicate methods removed)
};

} // namespace Audio
} // namespace Nomad
