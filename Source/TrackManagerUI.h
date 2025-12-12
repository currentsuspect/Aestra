// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "TrackUIComponent.h"
#include "PianoRollPanel.h"
#include "MixerPanel.h"
#include "StepSequencerPanel.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIScrollbar.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/OpenGL/NUIRenderCache.h"
#include <memory>
#include <vector>
#include <unordered_set>

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
class TrackManagerUI : public NomadUI::NUIComponent, public NomadUI::IDropTarget {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();
    
    // Solo coordination (exclusive solo behavior)
    void onTrackSoloToggled(TrackUIComponent* soloedTrack);
    
    // Clip deletion with animation
    void onClipDeleted(TrackUIComponent* trackComp, const NomadUI::NUIPoint& rippleCenter);
    
    // Clip splitting (split tool)
    void onSplitRequested(TrackUIComponent* trackComp, double splitTime);
    
    // Piano Roll Panel
    void togglePianoRoll();  // Show/hide piano roll panel
    
    // Mixer Panel
    void toggleMixer();  // Show/hide mixer panel

    // Sequencer Panel
    void toggleSequencer();  // Show/hide step sequencer panel
    
    // Playlist View
    void togglePlaylist();   // Show/hide playlist view (tracks/timeline)
    bool isPlaylistVisible() const { return m_showPlaylist; }
    
    // === TOOL SELECTION ===
    void setCurrentTool(PlaylistTool tool);
    void setActiveTool(PlaylistTool tool) { setCurrentTool(tool); }  // Alias
    PlaylistTool getCurrentTool() const { return m_currentTool; }
    PlaylistTool getActiveTool() const { return m_currentTool; }  // Alias
    
    // Cursor visibility callback (for custom cursor support)
    void setOnCursorVisibilityChanged(std::function<void(bool)> callback) { m_onCursorVisibilityChanged = callback; }
    
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
    
    // === CLIP MANIPULATION ===
    void splitSelectedClipAtPlayhead();  // Split clip at current playhead position
    void copySelectedClip();             // Copy selected clip to clipboard
    void cutSelectedClip();              // Cut selected clip (copy + delete)
    void pasteClip();                    // Paste clipboard at playhead position
    void duplicateSelectedClip();        // Duplicate selected clip immediately after
    void deleteSelectedClip();           // Delete selected clip
    TrackUIComponent* getSelectedTrackUI() const;  // Get currently selected track UI
    
    // === IDropTarget Interface ===
    NomadUI::DropFeedback onDragEnter(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::DropFeedback onDragOver(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    void onDragLeave() override;
    NomadUI::DropResult onDrop(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::NUIRect getDropBounds() const override { return getBounds(); }

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    
    // Hide setDirty to trigger cache invalidation (except during cache rendering)
    void setDirty(bool dirty = true) {
        NomadUI::NUIComponent::setDirty(dirty);
        if (dirty && !m_isRenderingToCache) {
            m_cacheInvalidated = true;
        }
    }
    
    // 🔥 VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(NomadUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;

    // UI Layout
    int m_trackHeight{80};
    int m_trackSpacing{2}; // Small gap for thick separator line
    float m_scrollOffset{0.0f};
    
    // Timeline/Ruler settings
    float m_pixelsPerBeat{50.0f};      // Horizontal zoom level
    float m_timelineScrollOffset{0.0f}; // Horizontal scroll position
    int m_beatsPerBar{4};               // Time signature numerator
    int m_subdivision{4};               // Grid subdivision (4 = 16th notes)
    
    // UI Components
    std::shared_ptr<NomadUI::NUIScrollbar> m_scrollbar;
    std::shared_ptr<NomadUI::NUIScrollbar> m_horizontalScrollbar;
    std::shared_ptr<NomadUI::NUIButton> m_addTrackButton;
    std::shared_ptr<NomadUI::NUIIcon> m_closeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_minimizeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_maximizeIcon;
    NomadUI::NUIRect m_closeIconBounds;
    NomadUI::NUIRect m_minimizeIconBounds;
    NomadUI::NUIRect m_maximizeIconBounds;
    
    // Tool icons (toolbar)
    std::shared_ptr<NomadUI::NUIIcon> m_selectToolIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_splitToolIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_multiSelectToolIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_loopToolIcon;
    NomadUI::NUIRect m_selectToolBounds;
    NomadUI::NUIRect m_splitToolBounds;
    NomadUI::NUIRect m_multiSelectToolBounds;
    NomadUI::NUIRect m_loopToolBounds;
    NomadUI::NUIRect m_toolbarBounds;
    bool m_selectToolHovered = false;
    bool m_splitToolHovered = false;
    bool m_multiSelectToolHovered = false;
    bool m_loopToolHovered = false;
    
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
    NomadUI::NUIPoint m_lastMousePos;  // Track mouse for split cursor rendering
    
    // Hover states for icons
    bool m_closeIconHovered = false;
    bool m_minimizeIconHovered = false;
    bool m_maximizeIconHovered = false;
    
    // Playhead dragging state
    bool m_isDraggingPlayhead = false;
    
    // === SELECTION BOX (Right-click drag or MultiSelect tool) ===
    bool m_isDrawingSelectionBox = false;
    NomadUI::NUIPoint m_selectionBoxStart;
    NomadUI::NUIPoint m_selectionBoxEnd;
    
    // === SMOOTH ZOOM ANIMATION (FL Studio style) ===
    float m_targetPixelsPerBeat = 50.0f;   // Target zoom level for animation (match initial m_pixelsPerBeat)
    float m_zoomVelocity = 0.0f;           // Current zoom velocity for momentum
    float m_lastMouseZoomX = 0.0f;         // Mouse X position during zoom for pivot
    bool m_isZooming = false;              // True while actively zooming
    bool m_dropTargetRegistered = false;   // Flag to ensure one-time registration
    
    // === FBO CACHING SYSTEM (like AudioSettingsDialog) ===
    NomadUI::CachedRenderData* m_cachedRender = nullptr;
    uint64_t m_cacheId;
    bool m_cacheInvalidated = true;  // Start invalidated to force initial render
    bool m_isRenderingToCache = false;  // Guard flag to prevent invalidation loops

        // Piano Roll Panel (can dock at bottom or maximize to full view)
    std::shared_ptr<PianoRollPanel> m_pianoRollPanel;
    bool m_showPianoRoll{false};  // Hidden by default
    float m_pianoRollHeight{300.0f};  // Default height when docked
    
    // Step Sequencer Panel (dockable at bottom like piano roll)
    std::shared_ptr<StepSequencerPanel> m_sequencerPanel;
    bool m_showSequencer{false};
    float m_sequencerHeight{220.0f};

    // Mixer Panel (can dock on right or maximize to full view)
    std::shared_ptr<MixerPanel> m_mixerPanel;
    bool m_showMixer{false};  // Hidden by default
    float m_mixerWidth{400.0f};  // Default width when docked
    
    // Playlist View State
    bool m_showPlaylist{true};   // Visible by default

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
    NomadUI::NUIRect m_dropPreviewRect;  // Visual preview rectangle
    
    // === SNAP-TO-GRID ===
    bool m_snapEnabled = true;           // Snap to grid enabled by default
    int m_snapDivision = 4;              // Snap to beats (1=bar, 4=beat, 16=16th, etc.)
    
    // === CLIPBOARD for copy/paste ===
    struct ClipboardData {
        bool hasData = false;
        std::vector<float> audioData;
        uint32_t sampleRate = 48000;
        uint32_t numChannels = 2;
        std::string name;
        double trimStart = 0.0;
        double trimEnd = -1.0;
        uint32_t sourceColor = 0xFFbb86fc;
    };
    ClipboardData m_clipboard;
    
    // === DELETE ANIMATION (FL Studio ripple effect) ===
    struct DeleteAnimation {
        std::shared_ptr<Track> track;     // Track being deleted from
        NomadUI::NUIPoint rippleCenter;   // Center of ripple effect
        NomadUI::NUIRect clipBounds;      // Original clip bounds
        float progress = 0.0f;            // Animation progress 0.0-1.0
        float duration = 0.25f;           // Animation duration in seconds
    };
    std::vector<DeleteAnimation> m_deleteAnimations;
    
    void updateBackgroundCache(NomadUI::NUIRenderer& renderer);
    void updateControlsCache(NomadUI::NUIRenderer& renderer);
    void updateTrackCache(NomadUI::NUIRenderer& renderer, size_t trackIndex);
    void invalidateAllCaches();
    void invalidateCache(); // Keep for compatibility

    void syncViewToggleButtons();
    void layoutTracks();
    void onAddTrackClicked();
    void updateTrackPositions();
    void updateScrollbar();
    void updateHorizontalScrollbar();
    void onScroll(double position);
    void onHorizontalScroll(double position);
    void deselectAllTracks();
    void renderTimeRuler(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds);
    void renderPlayhead(NomadUI::NUIRenderer& renderer);
    void renderDropPreview(NomadUI::NUIRenderer& renderer); // Render drop zone highlight
    void renderDeleteAnimations(NomadUI::NUIRenderer& renderer); // Render FL-style ripple delete
    void renderTrackManagerDirect(NomadUI::NUIRenderer& renderer);  // Direct rendering helper
    
    // Helper to convert mouse position to track/time
    int getTrackAtPosition(float y) const;
    double getTimeAtPosition(float x) const;
    void clearDropPreview(); // Clear drop preview state
    double snapTimeToGrid(double timeInSeconds) const; // Snap time to nearest grid line
    
    // Tool icons initialization and rendering
    void createToolIcons();
    void updateToolbarBounds();
    void renderToolbar(NomadUI::NUIRenderer& renderer);
    bool handleToolbarClick(const NomadUI::NUIPoint& position);
    void renderSplitCursor(NomadUI::NUIRenderer& renderer, const NomadUI::NUIPoint& position);
    
    // Instant clip dragging
    void startInstantClipDrag(TrackUIComponent* clip, const NomadUI::NUIPoint& clickPos);
    void updateInstantClipDrag(const NomadUI::NUIPoint& currentPos);
    void finishInstantClipDrag();
    void cancelInstantClipDrag();
    
    // Split tool
    void performSplitAtPosition(int trackIndex, double timeSeconds);
    
    // Calculate maximum timeline extent based on samples
    double getMaxTimelineExtent() const;
};

} // namespace Audio
} // namespace Nomad
