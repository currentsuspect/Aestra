// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIScrollbar.h" // Include Scrollbar
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "MusicHelpers.h"

namespace AestraUI {

using PianoRollTool = GlobalTool;

// -----------------------------------------------------------------------------
// PianoRollKeyLane: The vertical keyboard on the left
// -----------------------------------------------------------------------------
class PianoRollKeyLane : public NUIComponent {
public:
    /** @brief Create the vertical piano-key lane. */
    PianoRollKeyLane();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /** @brief Set the rendered key height in pixels. */
    void setKeyHeight(float height);
    /** @brief Get the rendered key height in pixels. */
    float getKeyHeight() const { return keyHeight_; }

    /** @brief Set the vertical scroll offset applied to the lane. */
    void setScrollOffsetY(float offset);

private:
    float keyHeight_;
    float scrollY_;
    int hoveredKey_; // -1 if none
};

// -----------------------------------------------------------------------------
// PianoRollMinimap: Top bar navigator (Playlist Style)
// -----------------------------------------------------------------------------
class PianoRollMinimap : public NUIComponent {
public:
    /** @brief Create the legacy piano-roll minimap widget. */
    PianoRollMinimap();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    /** @brief Set the visible beat window represented by the viewport. */
    void setView(double startBeat, double durationBeat);
    /** @brief Set the total beat span represented by the minimap. */
    void setTotalDuration(double totalBeats);
    /** @brief Set the current playhead beat displayed in the minimap. */
    void setPlayheadBeat(double beat);
    
    // Callbacks
    std::function<void(double start, double duration)> onViewChanged; // For Pan/Zoom

private:
    double startBeat_ = 0.0;
    double viewDuration_ = 4.0;
    double totalDuration_ = 100.0; // Default 100 bars?
    double playheadBeat_ = 0.0;

    // Interaction
    bool isDragging_ = false;
    bool isResizingL_ = false;
    bool isResizingR_ = false;
    NUIPoint dragStartPos_;
    double dragStartStart_;
    double dragStartDuration_;
    
    bool isHovered_ = false;
    
    // Helpers
    float beatToX(double beat) const;
    double xToBeat(float x) const;
};

// -----------------------------------------------------------------------------
// PianoRollRuler: The timeline ruler at the top
// -----------------------------------------------------------------------------
class PianoRollRuler : public NUIComponent {
public:
    /** @brief Create the piano-roll ruler. */
    PianoRollRuler();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override; // ADDED

    /** @brief Set the horizontal scroll offset in pixels. */
    void setScrollX(float scrollX); // MODIFIED from setScrollOffsetX
    /** @brief Set the horizontal zoom level in pixels per beat. */
    void setPixelsPerBeat(float ppb); // REORDERED
    /** @brief Set the current bar signature in beats per bar. */
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; repaint(); }
    /** @brief Set the playhead beat for ruler rendering. */
    void setPlayheadBeat(double beat) { playheadBeat_ = beat; repaint(); }

    // Callback: delta (wheel), mouseX (local)
    std::function<void(float delta, float mouseX)> onZoomRequested; // ADDED

private:
    float scrollX_; // REORDERED
    float pixelsPerBeat_; // REORDERED
    int beatsPerBar_;
    double playheadBeat_ = 0.0;
};

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
class PianoRollGrid;
class PianoRollNoteLayer;
class NUIDropdown;
class NUIButton;
class NUIIcon;
class NUILabel;
class NUIContextMenu; // Forward declaration

// -----------------------------------------------------------------------------
// PianoRollToolbar: Internal Toolbar (Tools + Scale)
// -----------------------------------------------------------------------------
class PianoRollToolbar : public NUIComponent {
public:
    /** @brief Create the internal piano-roll toolbar. */
    PianoRollToolbar();
    
    /** @brief Set the visible pattern name displayed in the toolbar. */
    void setPatternName(const std::string& name);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
    /** @brief Bind the grid widget controlled by the toolbar. */
    void setGrid(std::shared_ptr<PianoRollGrid> grid);
    /** @brief Bind the note layer controlled by the toolbar tools. */
    void setNoteLayer(std::shared_ptr<PianoRollNoteLayer> notes); // To set tools directly
    
    // Callbacks provided by view or used internally
    // void setOnToolChanged... -> Now we might just call NoteLayer directly
    
private:
    std::shared_ptr<NUIButton> m_menuBtn;
    
    // Tool Buttons
    std::shared_ptr<NUIButton> m_ptrBtn;
    std::shared_ptr<NUIButton> m_pencilBtn;
    std::shared_ptr<NUIButton> m_eraserBtn;
    
    GlobalTool activeTool_ = GlobalTool::Pointer;
    
    // Icons
    std::shared_ptr<AestraUI::NUIIcon> m_menuIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_ptrIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_pencilIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_eraserIcon;

    std::weak_ptr<PianoRollGrid> grid_;
    std::weak_ptr<PianoRollNoteLayer> notes_;
    
    std::shared_ptr<NUIContextMenu> m_activeContextMenu;
    
    void setupUI();
    void setActiveTool(GlobalTool tool);
    
    std::string m_patternName = "New Pattern";
    std::shared_ptr<NUILabel> m_patternLabel;
};

// -----------------------------------------------------------------------------
// PianoRollGrid: The background grid (static visual)
// -----------------------------------------------------------------------------
class PianoRollGrid : public NUIComponent { // ...
public:
    /** @brief Create the static piano-roll grid. */
    PianoRollGrid();

    void onRender(NUIRenderer& renderer) override;

    /** @brief Set the horizontal zoom level in pixels per beat. */
    void setPixelsPerBeat(float ppb);
    /** @brief Set the vertical pitch-row height. */
    void setKeyHeight(float height);
    /** @brief Set the horizontal scroll offset. */
    void setScrollOffsetX(float offset);
    /** @brief Set the vertical scroll offset. */
    void setScrollOffsetY(float offset);
    /** @brief Set the playhead beat rendered on the grid. */
    void setPlayheadBeat(double beat) { playheadBeat_ = beat; repaint(); }
    
    /** @brief Set the bar signature in beats per bar. */
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; repaint(); }

    /** @brief Set the musical root key used for scale highlighting. */
    void setRootKey(int root) { rootKey_ = root; repaint(); }
    /** @brief Set the active scale type used for scale highlighting. */
    void setScaleType(ScaleType type) { scaleType_ = type; repaint(); }
    
    /** @brief Set the active snap grid. */
    void setSnap(SnapGrid snap) { snap_ = snap; repaint(); }

private:
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_; // Added implementation
    int beatsPerBar_ = 4;
    double playheadBeat_ = 0.0;
    
    // Scale State
    int rootKey_ = 0; // 0=C, 1=C#, etc.
    ScaleType scaleType_ = ScaleType::Chromatic;
    
    // Snap State
    SnapGrid snap_ = SnapGrid::Beat; // Default to Beat
};


// -----------------------------------------------------------------------------
// Simple Undo Command
// -----------------------------------------------------------------------------
struct PianoRollCommand {
    /** @brief Human-readable description for the undo stack. */
    std::string description;
    /** @brief Note state before the edit. */
    std::vector<MidiNote> notesBefore;
    /** @brief Note state after the edit. */
    std::vector<MidiNote> notesAfter;
};

// -----------------------------------------------------------------------------
// PianoRollNoteLayer: Handles Rendering and Editing of Notes
// Contains Logic for: Painting, Selecting, Moving, Resizing
// -----------------------------------------------------------------------------
class PianoRollNoteLayer : public NUIComponent {
public:
    struct GhostPattern {
        /** @brief Notes rendered as a ghost overlay. */
        std::vector<MidiNote> notes;
        /** @brief Ghost overlay color. */
        AestraUI::NUIColor color;
    };

    /** @brief Create the editable piano-roll note layer. */
    PianoRollNoteLayer();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    /** @brief Replace the currently edited note list. */
    void setNotes(const std::vector<MidiNote>& notes);
    /** @brief Get the currently edited note list. */
    const std::vector<MidiNote>& getNotes() const { return notes_; }
    
    /** @brief Set ghost patterns rendered behind the active notes. */
    void setGhostPatterns(const std::vector<GhostPattern>& ghosts);
    
    /** @brief Set the active editing tool. */
    void setTool(PianoRollTool tool);
    /** @brief Get the active editing tool. */
    PianoRollTool getTool() const { return tool_; }
    
    /** @brief Set the snap grid used for note edits. */
    void setSnap(SnapGrid snap) { snap_ = snap; }
    /** @brief Get the snap grid used for note edits. */
    SnapGrid getSnap() const { return snap_; }
    
    /** @brief Push an undo command onto the local history stack. */
    void pushUndo(const std::string& desc, const std::vector<MidiNote>& oldNotes, const std::vector<MidiNote>& newNotes);
    /** @brief Undo the last local note edit. */
    void undo();
    /** @brief Redo the last undone local note edit. */
    void redo();

    /** @brief Set the horizontal zoom level in pixels per beat. */
    void setPixelsPerBeat(float ppb);
    /** @brief Set the vertical pitch-row height. */
    void setKeyHeight(float height);
    /** @brief Set the horizontal scroll offset. */
    void setScrollOffsetX(float offset);
    /** @brief Set the vertical scroll offset. */
    void setScrollOffsetY(float offset);
    /** @brief Set the current playhead beat used for rendering. */
    void setPlayheadBeat(double beat) { playheadBeat_ = beat; repaint(); }

    /** @brief Set the callback fired whenever notes change. */
    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);
    /** @brief Set the default unit assigned to newly created notes. */
    void setDefaultUnitId(uint64_t unitId) { defaultUnitId_ = unitId; }
    


private:
    std::vector<MidiNote> notes_;
    std::vector<GhostPattern> ghostPatterns_;
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_;
    double playheadBeat_ = 0.0;
    
    std::function<void(const std::vector<MidiNote>&)> onNotesChanged_;
    uint64_t defaultUnitId_ = 0;

    // Tool
    PianoRollTool tool_ = PianoRollTool::Pointer;
    
    // Undo Stack
    std::vector<PianoRollCommand> undoStack_;
    std::vector<PianoRollCommand> redoStack_;
    
    // Note Memory (Buffer)
    double lastNoteDuration_ = 1.0; // Default 1 beat
    int lastNoteVelocity_ = 100;    // Default velocity (User req: not 0)

    // Interaction State
    enum class State {
        None,
        Painting,       // Creating a new note (Drag extends duration)
        Moving,         // Moving existing note(s)
        Resizing,       // Resizing existing note(s) (Right edge)
        SelectingBox,   // Dragging selection rectangle
        Erasing         // Eraser Box/Hover
    };
    State state_ = State::None;
    
    NUIPoint dragStartPos_;
    std::vector<MidiNote> dragStartNotes_; // Snapshot for move/resize logic
    
    // For Painting
    int paintingNoteIndex_ = -1; // Index in notes_ of the note being painted
    double paintStartBeat_ = 0.0;
    int paintPitch_ = 0;
    
    // For Select Box
    NUIRect selectionRect_;
    
    // Snap
    SnapGrid snap_ = SnapGrid::Beat;

    // Helpers
    int findNoteAt(float localX, float localY);
    void commitNotes();
    double snapToGrid(double beat);
};

// -----------------------------------------------------------------------------
// PianoRollControlPanel: Bottom panel for Velocity/Control changes
// -----------------------------------------------------------------------------
class PianoRollControlPanel : public NUIComponent {
public:
    PianoRollControlPanel();
    
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
    void setNoteLayer(std::shared_ptr<PianoRollNoteLayer> layer);
    void setGrid(std::shared_ptr<PianoRollGrid> grid); // Added logic to link Grid
    
    void setPixelsPerBeat(float ppb);
    void setScrollX(float scrollX);

private:
    std::weak_ptr<PianoRollNoteLayer> noteLayer_;
    std::weak_ptr<PianoRollGrid> grid_; // Grid link
    
    float pixelsPerBeat_;
    float scrollX_;
    
    // Interaction
    int hoveringNoteIndex_ = -1;
    bool isDragging_ = false;
    NUIPoint dragStartPos_;
};

// -----------------------------------------------------------------------------
// PianoRollView: Main Container
// Orchestrates Layout and Scroll Sync
// -----------------------------------------------------------------------------
class PianoRollView : public NUIComponent {
public:
    PianoRollView();

    void onRender(NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    // API
    void setNotes(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& getNotes() const;
    void setPatternName(const std::string& name);
    void setPlayheadBeat(double beat, bool follow = false);
    void setTotalDurationBeats(double beats);
    void setLocalMinimapVisible(bool visible);
    double getViewStartBeat() const;
    double getViewDurationBeats() const;
    void setViewWindow(double startBeat, double durationBeats);
    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);
    void setDefaultUnitId(uint64_t unitId);
    
    void setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts);

    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);

    // Global Control API
    void setTool(GlobalTool tool);
    void setScale(int root, ScaleType type);
    
private:
    std::shared_ptr<PianoRollKeyLane> m_keys;
    std::shared_ptr<PianoRollRuler> m_ruler; 
    std::shared_ptr<PianoRollGrid> m_grid;
    std::shared_ptr<PianoRollNoteLayer> m_notes;
    std::shared_ptr<PianoRollControlPanel> m_controls;
    std::shared_ptr<PianoRollMinimap> m_minimap;
    std::shared_ptr<PianoRollToolbar> m_toolbar;
    
    std::shared_ptr<NUIScrollbar> m_vScroll; // Vertical Scrollbar still standard

    float m_keyLaneWidth;
    float m_rulerHeight;
    float m_controlPanelHeight = 100.0f;
    
    float m_pixelsPerBeat;
    float m_keyHeight;
    
    float m_scrollX;
    float m_scrollY;
    double m_playheadBeat = 0.0;
    double m_totalDurationBeats = 400.0;
    bool m_showLocalMinimap = true;

    bool m_isResizingPanel = false; // Added for splitter dragging
    float m_dragStartPanelHeight = 0.0f;
    NUIPoint m_dragStartPos;

    void syncChildren();
    void layoutChildren();
    void updateScrollbars(); // Renamed to updateNavigation?
};

} // namespace AestraUI
