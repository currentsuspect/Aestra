// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TrackManager.h"
#include "ClipInstance.h"
#include "PlaylistModel.h"
#include "TrackUIComponent.h"
#include "PianoRollPanel.h"
#include "MixerPanel.h"
// Sequencer panel include removed (replaced by Arsenal)
#include "TimelineMinimapBar.h"
#include "TimelineMinimapModel.h"
#include "TimelineSummaryCache.h"
#include "NUIComponent.h"
#include "NUIScrollbar.h"
#include "NUIButton.h"
#include "NUIIcon.h"
#include "NUIDragDrop.h"
#include "../AestraUI/Graphics/OpenGL/NUIRenderCache.h"
#include "MusicHelpers.h"
#include "NUIDropdown.h" // Full type for shared_ptr usage
#include <memory>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <functional>
#include "NUIContextMenu.h"
#include "WaveformCache.h"

namespace AestraUI { class NUIPlatformBridge; }

namespace Aestra {
namespace Audio {

/**
 * @brief Tool modes for playlist editing
 */
enum class PlaylistTool {
    Select,     // Default - select/move clips  
    Split,      // Blade tool - click to split clips
    MultiSelect,// Rectangle selection for multiple clips
    Paint,      // Paint tool - left-click to stamp clipboard clip
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
class TrackManagerUI : public ::AestraUI::NUIComponent, public ::AestraUI::IDropTarget {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    void setPlatformWindow(::AestraUI::NUIPlatformBridge* window);
    ::AestraUI::NUIPlatformBridge* getPlatformWindow() const { return m_window; }

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();
    void invalidateAllCaches();
    
    void invalidateCache(); // Keep for compatibility
    
    // Solo coordination (exclusive solo behavior)
    void onTrackSoloToggled(TrackUIComponent* soloedTrack);
    
    void onClipDeleted(TrackUIComponent* trackComp, ClipInstanceID clipId, const ::AestraUI::NUIPoint& rippleCenter);
    
    // Clip splitting (split tool)
    void onSplitRequested(TrackUIComponent* trackComp, double splitBeat);

    // Clipboard & Paint Tool
    void copySelectedClip();
    void pasteClipboardAtCursor(); // Paste at playhead
    void pasteClipToRight();       // Ctrl+B: Paste at end of selected clip, select new clip
    void onPaintClip(TrackUIComponent* trackComp, double beat);
    const Audio::ClipInstance& getClipboardClip() const { return m_clipboardClip; }
    bool hasClipboardClip() const { return m_clipboardClip.id.isValid(); }

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
    
    // Pattern Playback Mode (Arsenal) - Hides playhead/playline
    void setPatternMode(bool enabled);
    bool isPatternMode() const { return m_patternMode; }
    
    // Cursor visibility callback (for custom cursor support)
    void setOnCursorVisibilityChanged(std::function<void(bool)> callback) { m_onCursorVisibilityChanged = callback; }
    bool isMinimapResizeCursorActive() const;
    bool isCustomCursorActive() const;  // Returns true if any tool/resize cursor is active
    
    // View Toggle Callbacks (v3.1)
    void setOnToggleMixer(std::function<void()> cb) { m_onToggleMixer = cb; }
    void setOnTogglePianoRoll(std::function<void()> cb) { m_onTogglePianoRoll = cb; }
    void setOnToggleSequencer(std::function<void()> cb) { m_onToggleSequencer = cb; }
    void setOnTogglePlaylist(std::function<void()> cb) { m_onTogglePlaylist = cb; }
    
    // Loop control callback (preset: 0=Off, 1=1Bar, 2=2Bars, 3=4Bars, 4=8Bars, 5=Selection, 6=Project)
    void setOnLoopPresetChanged(std::function<void(int preset)> cb) { m_onLoopPresetChanged = cb; }
    int getLoopPreset() const { return m_loopPreset; }
    
    // Selection made callback - called when ruler selection is finalized (startBeat, endBeat)
    // This should jump playhead to start and set up the loop region
    void setOnSelectionMade(std::function<void(double startBeat, double endBeat)> cb) { m_onSelectionMade = cb; }
    
    // Loop region update callback - called when loop region needs to change (for Project auto-update)
    void setOnLoopRegionUpdate(std::function<void(double startBeat, double endBeat)> cb) { m_onLoopRegionUpdate = cb; }
    
    // Audition Mode integration - called when user wants to send track/clip to Audition
    void setOnSendToAudition(std::function<void(uint32_t trackId, const std::string& trackName)> cb) { m_onSendToAudition = cb; }
    void setOnSendSelectionToAudition(std::function<void(double startBeat, double endBeat)> cb) { m_onSendSelectionToAudition = cb; }
    
    // === MULTI-SELECTION ===
    void selectTrack(TrackUIComponent* track, bool addToSelection = false);
    void deselectTrack(TrackUIComponent* track);
    void selectAllTracks();
    void clearSelection();
    const std::unordered_set<TrackUIComponent*>& getSelectedTracks() const { return m_selectedTracks; }
    bool isTrackSelected(TrackUIComponent* track) const;
    
    // Context Menu Helpers (v4.0)
    void openTrackContextMenu(const ::AestraUI::NUIPoint& position, std::function<void()> onSendToAudition);
    
    // Snap-to-Grid control
    void setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
    bool isSnapEnabled() const { return m_snapEnabled; }
    void setSnapDivision(int division) { m_snapDivision = division; } // 1=bar, 4=beat, 16=16th
    int getSnapDivision() const { return m_snapDivision; }
    
    // Follow Playhead
    enum class FollowMode {
        Page,       // Jump to next page when playhead reaches edge
        Continuous  // Smooth scrolling keeping playhead centered
    };
    
    void setFollowPlayhead(bool enabled) { m_followPlayhead = enabled; }
    bool isFollowPlayhead() const { return m_followPlayhead; }
    
    void setFollowMode(FollowMode mode) { m_followMode = mode; }
    FollowMode getFollowMode() const { return m_followMode; }
    
    // New Snap System
    void setSnapSetting(::AestraUI::SnapGrid snap);
    ::AestraUI::SnapGrid getSnapSetting() const { return m_snapSetting; }
    
    // === CLIP MANIPULATION ===
    void splitSelectedClipAtPlayhead();  // Split clip at current playhead position
    // copySelectedClip moved to line 85
    void cutSelectedClip();              // Cut selected clip (copy + delete)
    // pasteClip replaced by pasteClipboardAtCursor
    // stampClipAtCursor replaced by onPaintClip
    void duplicateSelectedClip();        // Duplicate selected clip immediately after
    void deleteSelectedClip();           // Delete selected clip
    TrackUIComponent* getSelectedTrackUI() const;  // Get currently selected track UI

    // Instant clip dragging
    void startInstantClipDrag(TrackUIComponent* trackComp, ClipInstanceID clipId, const ::AestraUI::NUIPoint& clickPos);
    void updateInstantClipDrag(const ::AestraUI::NUIPoint& currentPos);
    void finishInstantClipDrag();
    void cancelInstantClipDrag();
    
    // === IDropTarget Interface ===
    ::AestraUI::DropFeedback onDragEnter(const ::AestraUI::DragData& data, const ::AestraUI::NUIPoint& position) override;
    ::AestraUI::DropFeedback onDragOver(const ::AestraUI::DragData& data, const ::AestraUI::NUIPoint& position) override;
    void onDragLeave() override;
    ::AestraUI::DropResult onDrop(const ::AestraUI::DragData& data, const ::AestraUI::NUIPoint& position) override;
    ::AestraUI::NUIRect getDropBounds() const override { return getBounds(); }
    
    // Loop markers (Visual feedback)
    void setLoopRegion(double startBeat, double endBeat, bool enabled);

    bool onMouseEvent(const ::AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const ::AestraUI::NUIKeyEvent& event) override;

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

    // Issue #120: Track View Zoom/Scroll State Persistence
    float getHorizontalZoom() const { return m_pixelsPerBeat; }
    void setHorizontalZoom(float zoom) {
        m_pixelsPerBeat = std::clamp(zoom, 1.0f, 300.0f);
        m_targetPixelsPerBeat = m_pixelsPerBeat;
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setPixelsPerBeat(m_pixelsPerBeat);
        }
        invalidateCache();
    }
    
    float getHorizontalScroll() const { return m_timelineScrollOffset; }
    void setHorizontalScroll(float scroll) {
        m_timelineScrollOffset = std::max(0.0f, scroll);
        for (auto& trackUI : m_trackUIComponents) {
            trackUI->setTimelineScrollOffset(m_timelineScrollOffset);
        }
        invalidateCache();
    }
    
    float getVerticalScroll() const { return m_scrollOffset; }
    void setVerticalScroll(float scroll) {
        m_scrollOffset = std::max(0.0f, scroll);
        layoutTracks();
    }

protected:
    void onRender(::AestraUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    
    // Hide setDirty to trigger cache invalidation (except during cache rendering)
    void setDirty(bool dirty = true) {
        ::AestraUI::NUIComponent::setDirty(dirty);
        if (dirty && !m_isRenderingToCache) {
            m_cacheInvalidated = true;
        }
    }
    
    // 🔥 VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(::AestraUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;
    ::AestraUI::NUIPlatformBridge* m_window = nullptr;

    // UI Layout
    int m_trackHeight{48};
    int m_trackSpacing{4}; // 8px grid spacing scale (S1)
    float m_scrollOffset{0.0f};
    PlaylistMode m_playlistMode{PlaylistMode::Clips};
    bool m_patternMode = false; // True when Pattern (Arsenal) playback is active
    
    // Timeline/Ruler settings
    float m_pixelsPerBeat{50.0f};      // Horizontal zoom level
    float m_timelineScrollOffset{0.0f}; // Horizontal scroll position
    int m_beatsPerBar{4};               // Time signature numerator
    int m_subdivision{4};               // Grid subdivision (4 = 16th notes)
    ::AestraUI::SnapGrid m_snapSetting = ::AestraUI::SnapGrid::Bar;
    
    // Legacy Snap (Check if used)
    bool m_snapEnabled = true;
    int m_snapDivision = 4;
    
    // UI Components
    std::shared_ptr<::AestraUI::NUIScrollbar> m_scrollbar;
    std::shared_ptr<::AestraUI::TimelineMinimapBar> m_timelineMinimap;
    std::shared_ptr<::AestraUI::NUIIcon> m_addTrackIcon;
    ::AestraUI::NUIRect m_addTrackBounds;
    bool m_addTrackHovered = false;

    // Timeline minimap state (beats-domain)
    ::AestraUI::TimelineSummaryCache m_timelineSummaryCache;
    ::AestraUI::TimelineSummarySnapshot m_timelineSummarySnapshot;
    ::AestraUI::TimelineMinimapMode m_minimapMode{::AestraUI::TimelineMinimapMode::Clips};
    ::AestraUI::TimelineMinimapAggregation m_minimapAggregation{::AestraUI::TimelineMinimapAggregation::MaxPresence};
    double m_minimapDomainStartBeat{0.0};
    double m_minimapDomainEndBeat{0.0};
    bool m_minimapNeedsRebuild{true};
    ::AestraUI::TimelineRange m_minimapSelectionBeatRange{};
    
    // Tool icons (toolbar)
    std::shared_ptr<::AestraUI::NUIIcon> m_menuIcon;       // Menu dropdown icon (down arrow)
    std::shared_ptr<::AestraUI::NUIIcon> m_selectToolIcon;
    std::shared_ptr<::AestraUI::NUIIcon> m_splitToolIcon;
    std::shared_ptr<::AestraUI::NUIIcon> m_multiSelectToolIcon;
    std::shared_ptr<::AestraUI::NUIIcon> m_paintToolIcon;  // Paint/stamp tool icon
    std::shared_ptr<::AestraUI::NUIIcon> m_moveCursorIcon; // Move (4-way arrow) cursor for Paint tool hovering clips
    
    std::shared_ptr<::AestraUI::NUIContextMenu> m_activeContextMenu; // Keep track for cleanup
    
    // Clipboard
    Audio::ClipInstance m_clipboardClip;

    // Animation state
    float m_menuIconRotation = 0.0f;
    float m_menuIconTargetRotation = 0.0f;
    
    ::AestraUI::NUIRect m_menuIconBounds;
    ::AestraUI::NUIRect m_selectToolBounds;
    ::AestraUI::NUIRect m_splitToolBounds;
    ::AestraUI::NUIRect m_multiSelectToolBounds;
    ::AestraUI::NUIRect m_paintToolBounds;
    ::AestraUI::NUIRect m_followPlayheadBounds; // Toggle button bounds
    ::AestraUI::NUIRect m_toolbarBounds;

    bool m_menuHovered = false;
    bool m_selectToolHovered = false;
    bool m_splitToolHovered = false;
    bool m_multiSelectToolHovered = false;
    bool m_paintToolHovered = false;
    bool m_followPlayheadHovered = false;
    
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
    ClipInstanceID m_draggedClipId;
    double m_clipDragOffsetBeats = 0.0;
    bool m_suppressPlaylistRefresh = false;

    float m_clipDragOffsetX = 0.0f;  // Offset from clip start to mouse
    double m_clipOriginalStartTime = 0.0;  // Original position before drag
    int m_clipOriginalTrackIndex = -1;  // Original track before drag
    
    // Split tool cursor position
    float m_splitCursorX = 0.0f;
    bool m_showSplitCursor = false;
    ::AestraUI::NUIPoint m_lastMousePos;  // Track mouse for split cursor rendering
    
    // Playhead dragging state
    bool m_isDraggingPlayhead = false;
    
    // === RULER SELECTION (Right-click or Ctrl+Left-click on ruler for looping) ===
    bool m_isDraggingRulerSelection = false;
    double m_rulerSelectionStartBeat = 0.0;
    double m_rulerSelectionEndBeat = 0.0;
    bool m_hasRulerSelection = false;
    
    // === LOOP MARKERS (Visual feedback on ruler) ===
    bool m_loopEnabled = false;  // Default OFF - no loop until user makes selection
    double m_loopStartBeat = 0.0;
    double m_loopEndBeat = 4.0;
    bool m_isDraggingLoopStart = false;
    bool m_isDraggingLoopEnd = false;
    bool m_hoveringLoopStart = false;
    bool m_hoveringLoopEnd = false;
    double m_loopDragStartBeat = 0.0;  // Original beat position when drag started
    
    // === SELECTION BOX (Right-click drag or MultiSelect tool) ===
    bool m_isDrawingSelectionBox = false;
    ::AestraUI::NUIPoint m_selectionBoxStart;
    ::AestraUI::NUIPoint m_selectionBoxEnd;
    
    // === SMOOTH ZOOM ANIMATION ===
    float m_targetPixelsPerBeat = 50.0f;   // Target zoom level for animation (match initial m_pixelsPerBeat)
    float m_zoomVelocity = 0.0f;           // Current zoom velocity for momentum
    float m_lastMouseZoomX = 0.0f;         // Mouse X position during zoom for pivot
    bool m_isZooming = false;              // True while actively zooming
    bool m_dropTargetRegistered = false;   // Flag to ensure one-time registration
    
    // === FBO CACHING SYSTEM (like AudioSettingsDialog) ===
    ::AestraUI::CachedRenderData* m_cachedRender = nullptr;
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
    ::AestraUI::NUIRect m_dropPreviewRect;  // Visual preview rectangle
    
    // === SNAP-TO-GRID ===
    // === SNAP-TO-GRID (Legacy - preserved for compatibility but shadowed by m_snapSetting) ===
    // bool m_snapEnabled = true;           // Snap to grid enabled by default
    // int m_snapDivision = 4;              // Snap to beats (1=bar, 4=beat, 16=16th, etc.)

    bool m_followPlayhead = false;          // Whether timeline automatically scrolls to follow playhead
    FollowMode m_followMode = FollowMode::Page; // Default logic
    std::shared_ptr<::AestraUI::NUIIcon> m_followPlayheadIcon; // Icon for the toggle button
    
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

    
    // === DELETE ANIMATION (Ripple effect) ===
    struct DeleteAnimation {
        PlaylistLaneID laneId;            // Lane being deleted from
        ClipInstanceID clipId;            // Clip ID (for reference during animation if needed)
        ::AestraUI::NUIPoint rippleCenter;   // Center of ripple effect

        ::AestraUI::NUIRect clipBounds;      // Original clip bounds
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
    std::function<void(double, double)> m_onSelectionMade;  // Called when ruler selection finalized
    std::function<void(double, double)> m_onLoopRegionUpdate;  // Called when loop region needs update (Project auto-update)
    std::function<void(uint32_t, const std::string&)> m_onSendToAudition;  // Called for "Send to Audition"
    std::function<void(double, double)> m_onSendSelectionToAudition;  // Called for "Send Selection to Audition"
    
    void updateBackgroundCache(::AestraUI::NUIRenderer& renderer);
    void updateControlsCache(::AestraUI::NUIRenderer& renderer);
    void updateTrackCache(::AestraUI::NUIRenderer& renderer, size_t trackIndex);

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
    void resizeTimelineViewEdgeFromMinimap(::AestraUI::TimelineMinimapResizeEdge edge, double anchorBeat, double edgeBeat, bool isFinal);
    void centerTimelineViewAtBeat(double centerBeat);
    void zoomTimelineAroundBeat(double anchorBeat, float zoomMultiplier);
    float getTimelineGridWidthPixels() const;
    double secondsToBeats(double seconds) const;
    void renderTimeRuler(::AestraUI::NUIRenderer& renderer, const ::AestraUI::NUIRect& rulerBounds);
    void renderLoopMarkers(::AestraUI::NUIRenderer& renderer, const ::AestraUI::NUIRect& rulerBounds);
    void renderPlayhead(::AestraUI::NUIRenderer& renderer);
    void renderDropPreview(::AestraUI::NUIRenderer& renderer); // Render drop zone highlight
    void renderDeleteAnimations(::AestraUI::NUIRenderer& renderer); // Render FL-style ripple delete
    void renderTrackManagerStatic(::AestraUI::NUIRenderer& renderer);  // Static content (cached)
    void renderTrackManagerDynamic(::AestraUI::NUIRenderer& renderer); // Dynamic content (real-time)
    
    // Helper to convert mouse position to track/time
    int getTrackAtPosition(float y) const;
    double getTimeAtPosition(float x) const;
    void clearDropPreview(); // Clear drop preview state
    double snapBeatToGrid(double beat) const; // Snap beat to nearest grid line
    double snapBeatToGridForward(double beat) const; // Snap beat to next grid line (paste-to-right)
    
    // Grid helper
    void drawGrid(::AestraUI::NUIRenderer& renderer, const ::AestraUI::NUIRect& bounds, float gridStartX, float gridWidth, float timelineScrollOffset);

    // Tool icons initialization and rendering
    void createToolIcons();
    void updateToolbarBounds();
    void renderToolbar(::AestraUI::NUIRenderer& renderer);
    bool handleToolbarClick(const ::AestraUI::NUIPoint& position);
    void renderToolCursor(::AestraUI::NUIRenderer& renderer, const ::AestraUI::NUIPoint& position);
    void renderMinimapResizeCursor(::AestraUI::NUIRenderer& renderer, const ::AestraUI::NUIPoint& position);
    

    
    // Split tool
    void performSplitAtPosition(int trackIndex, double timeSeconds);
    
    // Calculate maximum timeline extent based on samples
    double getMaxTimelineExtent() const;

    // Async waveform builder
    ::Aestra::Audio::WaveformCacheBuilder m_waveformBuilder;

    // Async Task Queue (for main thread callbacks)
    std::mutex m_pendingTasksMutex;
    std::vector<std::function<void()>> m_pendingTasks;

    // === IMPORT ANIMATION ===
    struct PendingImport {
        std::string displayName;
        PlaylistLaneID laneId;
        double startBeat;
        double estimatedDurationBeats = 16.0; // Default placeholder width
        float animationTime = 0.0f;
        float progress = 0.0f; // 0.0 to 1.0
    };
    std::vector<PendingImport> m_pendingImports;
    void renderPendingImports(AestraUI::NUIRenderer& renderer);
    
    // (Duplicate methods removed)
};

} // namespace Audio
} // namespace Aestra
