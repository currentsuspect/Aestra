// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUIScrollbar.h" // Include Scrollbar
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Common/MusicHelpers.h"

namespace NomadUI {

using PianoRollTool = GlobalTool;

// -----------------------------------------------------------------------------
// PianoRollKeyLane: The vertical keyboard on the left
// -----------------------------------------------------------------------------
class PianoRollKeyLane : public NUIComponent {
public:
    PianoRollKeyLane();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setKeyHeight(float height);
    float getKeyHeight() const { return keyHeight_; }

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
    PianoRollMinimap();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    // State
    void setView(double startBeat, double durationBeat);
    void setTotalDuration(double totalBeats);
    
    // Callbacks
    std::function<void(double start, double duration)> onViewChanged; // For Pan/Zoom

private:
    double startBeat_ = 0.0;
    double viewDuration_ = 4.0;
    double totalDuration_ = 100.0; // Default 100 bars?

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
    PianoRollRuler();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override; // ADDED

    void setScrollX(float scrollX); // MODIFIED from setScrollOffsetX
    void setPixelsPerBeat(float ppb); // REORDERED
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; repaint(); }

    // Callback: delta (wheel), mouseX (local)
    std::function<void(float delta, float mouseX)> onZoomRequested; // ADDED

private:
    float scrollX_; // REORDERED
    float pixelsPerBeat_; // REORDERED
    int beatsPerBar_;
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

// -----------------------------------------------------------------------------
// PianoRollToolbar: Internal Toolbar (Tools + Scale)
// -----------------------------------------------------------------------------
class PianoRollToolbar : public NUIComponent {
public:
    PianoRollToolbar();
    
    void setPatternName(const std::string& name);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    
    void setGrid(std::shared_ptr<PianoRollGrid> grid);
    void setNoteLayer(std::shared_ptr<PianoRollNoteLayer> notes); // To set tools directly
    
    // Callbacks provided by view or used internally
    // void setOnToolChanged... -> Now we might just call NoteLayer directly
    
private:
    std::shared_ptr<NUIDropdown> m_rootDropdown;
    std::shared_ptr<NUIDropdown> m_scaleDropdown;
    std::shared_ptr<NUIDropdown> m_snapDropdown;
    
    // Tool Buttons
    std::shared_ptr<NUIButton> m_ptrBtn;
    std::shared_ptr<NUIButton> m_pencilBtn;
    std::shared_ptr<NUIButton> m_eraserBtn;
    
    GlobalTool activeTool_ = GlobalTool::Pointer;
    
    // Icons
    std::shared_ptr<NomadUI::NUIIcon> m_ptrIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_pencilIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_eraserIcon;

    std::weak_ptr<PianoRollGrid> grid_;
    std::weak_ptr<PianoRollNoteLayer> notes_;
    
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
    PianoRollGrid();

    void onRender(NUIRenderer& renderer) override;

    void setPixelsPerBeat(float ppb);
    void setKeyHeight(float height);
    void setScrollOffsetX(float offset);
    void setScrollOffsetY(float offset);
    
    // Zoom/Grid settings
    void setBeatsPerBar(int bpb) { beatsPerBar_ = bpb; repaint(); }

    // Scale Settings
    void setRootKey(int root) { rootKey_ = root; repaint(); }
    void setScaleType(ScaleType type) { scaleType_ = type; repaint(); }
    
    // Snap
    void setSnap(SnapGrid snap) { snap_ = snap; repaint(); }

private:
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_; // Added implementation
    int beatsPerBar_ = 4;
    
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
    std::string description;
    std::vector<MidiNote> notesBefore;
    std::vector<MidiNote> notesAfter;
};

// -----------------------------------------------------------------------------
// PianoRollNoteLayer: Handles Rendering and Editing of Notes
// Contains Logic for: Painting, Selecting, Moving, Resizing
// -----------------------------------------------------------------------------
class PianoRollNoteLayer : public NUIComponent {
public:
    struct GhostPattern {
        std::vector<MidiNote> notes;
        NomadUI::NUIColor color;
    };

    PianoRollNoteLayer();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    void setNotes(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& getNotes() const { return notes_; }
    
    void setGhostPatterns(const std::vector<GhostPattern>& ghosts);
    
    // Tool State
    void setTool(PianoRollTool tool);
    PianoRollTool getTool() const { return tool_; }
    
    void setSnap(SnapGrid snap) { snap_ = snap; }
    SnapGrid getSnap() const { return snap_; }
    
    // Undo/Redo
    void pushUndo(const std::string& desc, const std::vector<MidiNote>& oldNotes, const std::vector<MidiNote>& newNotes);
    void undo();
    void redo();

    void setPixelsPerBeat(float ppb);
    void setKeyHeight(float height);
    void setScrollOffsetX(float offset);
    void setScrollOffsetY(float offset);

    // Callbacks
    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);
    


private:
    std::vector<MidiNote> notes_;
    std::vector<GhostPattern> ghostPatterns_;
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_;
    
    std::function<void(const std::vector<MidiNote>&)> onNotesChanged_;

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

    bool m_isResizingPanel = false; // Added for splitter dragging
    float m_dragStartPanelHeight = 0.0f;
    NUIPoint m_dragStartPos;

    void syncChildren();
    void layoutChildren();
    void updateScrollbars(); // Renamed to updateNavigation?
};

} // namespace NomadUI
