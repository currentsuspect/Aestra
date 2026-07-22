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
    void setHoveredKey(int pitch) { hoveredKey_ = pitch; repaint(); }

    /** @brief Set callback for note preview (pitch, velocity). Called when user clicks a key. */
    void setOnPreviewNote(std::function<void(int pitch, int velocity)> cb);
    void setOnHoveredKeyChanged(std::function<void(int pitch)> cb) {
        onHoveredKeyChanged_ = std::move(cb);
    }

    /** @brief Set callback to check if transport is playing (suppress preview when playing). */
    void setIsPlayingCallback(std::function<bool()> cb);

    /** @brief Forward isPlaying callback from PianoRollView. */
    void setIsPlayingFromParent(std::function<bool()> cb) { m_isPlayingCallback = std::move(cb); }

private:
    float keyHeight_;
    float scrollY_;
    int hoveredKey_; // -1 if none
    int previewPitch_; // Currently playing preview note (-1 if none)
    std::function<void(int pitch, int velocity)> onPreviewNote_;
    std::function<void(int pitch)> onHoveredKeyChanged_;
    std::function<bool()> m_isPlayingCallback;
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
    void setView(double startBeat, double durationBeat, bool preserveEdge = false);
    /** @brief Set the total beat span represented by the minimap. */
    void setTotalDuration(double totalBeats);
    /** @brief Set the current playhead beat displayed in the minimap. */
    void setPlayheadBeat(double beat);
    /** @brief Replace the note spans rendered inside the minimap. */
    void setNotes(const std::vector<MidiNote>& notes);
    
    // Callbacks
    std::function<void(double start, double duration)> onViewChanged; // For Pan/Zoom

private:
    std::vector<MidiNote> notes_;
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
    /** @brief Called while the user clicks or drags the ruler playhead. */
    std::function<void(double beat, bool active)> onPlayheadScrubbed;

private:
    float scrollX_; // REORDERED
    float pixelsPerBeat_; // REORDERED
    int beatsPerBar_;
    double playheadBeat_ = 0.0;
    bool isScrubbing_ = false;
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
    struct PatternChoice {
        int value = 0;
        std::string label;
    };

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
    void setPatternLengthBeats(double beats);
    void setPatternChoices(const std::vector<PatternChoice>& choices, int selectedValue);
    /** @brief Populate the unit switcher (reuses the PatternChoice value/label shape). */
    void setUnitChoices(const std::vector<PatternChoice>& choices, int selectedValue);
    void setOnAdjustPatternLength(std::function<void(int barsDelta)> cb) { onAdjustPatternLength_ = std::move(cb); }
    void setOnPatternChoiceSelected(std::function<void(int patternValue)> cb) {
        onPatternChoiceSelected_ = std::move(cb);
    }
    void setOnUnitChoiceSelected(std::function<void(int unitValue)> cb) {
        onUnitChoiceSelected_ = std::move(cb);
    }
    /** @brief Set callback fired by the menu's "Keyboard Shortcuts" item. */
    void setOnShowShortcutHelp(std::function<void()> cb) { onShowShortcutHelp_ = std::move(cb); }
    /** @brief Get the currently open context menu, if any. */
    std::shared_ptr<NUIComponent> getActiveContextMenu() const { return m_activeContextMenu; }
    /** @brief Close and remove the currently open context menu, if any. */
    void dismissActiveContextMenu() { closeActiveContextMenu(); }
    
    // Callbacks provided by view or used internally
    // void setOnToolChanged... -> Now we might just call NoteLayer directly
    
private:
    std::shared_ptr<NUIButton> m_menuBtn;
    
    // Tool Buttons
    std::shared_ptr<NUIButton> m_ptrBtn;
    std::shared_ptr<NUIButton> m_pencilBtn;
    std::shared_ptr<NUIButton> m_eraserBtn;
    std::shared_ptr<NUIButton> m_lengthDownBtn;
    std::shared_ptr<NUIButton> m_lengthUpBtn;
    std::shared_ptr<NUIDropdown> m_patternDropdown;
    std::shared_ptr<NUIDropdown> m_unitDropdown;
    std::shared_ptr<NUIDropdown> m_snapDropdown;

    GlobalTool activeTool_ = GlobalTool::Pencil;
    void applySnap(SnapGrid snap);
    
    // Icons
    std::shared_ptr<AestraUI::NUIIcon> m_menuIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_ptrIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_pencilIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_eraserIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_lengthDownIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_lengthUpIcon;

    std::weak_ptr<PianoRollGrid> grid_;
    std::weak_ptr<PianoRollNoteLayer> notes_;
    
    std::shared_ptr<NUIComponent> m_activeContextMenu;
    std::function<void(int barsDelta)> onAdjustPatternLength_;
    std::function<void(int patternValue)> onPatternChoiceSelected_;
    std::function<void(int unitValue)> onUnitChoiceSelected_;
    std::function<void()> onShowShortcutHelp_;
    bool m_updatingPatternDropdown = false;
    bool m_updatingUnitDropdown = false;
    bool m_updatingSnapDropdown = false;
    SnapGrid m_currentSnap = SnapGrid::Beat;

    void closeActiveContextMenu();
    
    void setupUI();
    void setActiveTool(GlobalTool tool);
    
    std::string m_patternName = "New Pattern";
    double m_patternLengthBeats = 8.0;
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
    void setTotalDurationBeats(double beats) { totalDurationBeats_ = std::max(0.0, beats); repaint(); }
    void setHoveredPitch(int pitch) { hoveredPitch_ = pitch; repaint(); }
    
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
    double totalDurationBeats_ = 8.0;
    int hoveredPitch_ = -1;
    
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

class NUIPlatformBridge;

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
        /** @brief Fill alpha — same-pattern unit ghosts read stronger than cross-pattern ghosts. */
        float fillAlpha{0.1f};
        /** @brief Stroke alpha. */
        float strokeAlpha{0.2f};
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
    
    /** @brief Set the musical root key for scale snapping. */
    void setRootKey(int root) { rootKey_ = root; }
    /** @brief Set the active scale type for scale snapping. */
    void setScaleType(ScaleType type) { scaleType_ = type; }
    /** @brief Enable or disable snap-to-scale quantization on the Y-axis. */
    void setSnapToScale(bool enabled) { snapToScale_ = enabled; }
    /** @brief Check if snap-to-scale is currently active. */
    bool getSnapToScale() const { return snapToScale_; }
    
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
    void setTotalDurationBeats(double beats) { totalDurationBeats_ = beats; }
    double getTotalDurationBeats() const { return totalDurationBeats_; }

    /** @brief Call during drag to update edge scrolling and apply scroll offset. */
    void updateEdgeScrolling(float mouseX, float mouseY, const NUIRect& bounds, std::function<void()> syncCallback = nullptr);
    
    /** @brief Set the current playhead beat used for rendering. */
    void setPlayheadBeat(double beat) { playheadBeat_ = beat; repaint(); }

    /** @brief Set whether transport is playing, so sounding notes can light up. */
    void setPlaying(bool playing) { isPlaying_ = playing; }

    /** @brief Set beats-per-bar, used for the bar:beat readout while editing. */
    void setBeatsPerBar(int bpb) { beatsPerBar_ = std::max(1, bpb); }

    /** @brief Set callback used to audition a pitch (velocity 0 = note-off). */
    void setOnPreviewNote(std::function<void(int pitch, int velocity)> cb) {
        onPreviewNote_ = std::move(cb);
    }
    /** @brief Set callback to check if transport is playing (suppresses audition). */
    void setIsPlayingCallback(std::function<bool()> cb) { isPlayingCallback_ = std::move(cb); }

    /** @brief Set the callback fired whenever notes change. */
    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);
    void setOnHoveredPitchChanged(std::function<void(int pitch)> cb) {
        onHoveredPitchChanged_ = std::move(cb);
    }
    /** @brief Set the default unit assigned to newly created notes. */
    void setDefaultUnitId(uint64_t unitId) { defaultUnitId_ = unitId; }

    /** @brief Toggle chord mode: the pencil stamps a diatonic triad, not one note. */
    void setChordMode(bool enabled) { chordMode_ = enabled; }
    bool getChordMode() const { return chordMode_; }

    /**
     * @brief Stagger the selected notes into an upward strum.
     *
     * Notes are anchored at the earliest selected start and cascaded low-to-high
     * pitch by @p spreadBeats each. No-op for fewer than two selected notes.
     */
    void strumSelectedNotes(double spreadBeats);

    /**
     * @brief Elongate selected notes to connect to the following note (legato).
     *
     * Each selected note's end is extended forward to meet the start of the next
     * note in time; when nothing follows, it extends to the next snap/beat
     * boundary instead. Only ever lengthens — never shortens.
     */
    void connectSelectedNotes();

    /** @brief Snap the starts of the selected notes to the current snap grid. */
    void quantizeSelectedNotes();

    /** @brief Record an undo step for an edit applied externally (e.g. velocity lane). */
    void pushExternalEdit(const std::vector<MidiNote>& before, const std::string& description) {
        pushUndo(description, before, notes_);
    }

    /** @brief Merge overlapping/touching selected notes on the same pitch into one. */
    void glueSelectedNotes();

    /** @brief Add slight random velocity variation to the selected notes. */
    void humanizeSelectedVelocities();

    /** @brief Set the platform bridge for cursor style changes. */
    void setPlatformBridge(NUIPlatformBridge* bridge);


private:
    std::vector<MidiNote> notes_;
    std::vector<GhostPattern> ghostPatterns_;
    float pixelsPerBeat_;
    float keyHeight_;
    float scrollX_;
    float scrollY_;
    double playheadBeat_ = 0.0;
    double totalDurationBeats_ = 400.0;
    bool isPlaying_ = false;
    int beatsPerBar_ = 4;

    std::function<void(const std::vector<MidiNote>&)> onNotesChanged_;
    std::function<void(int pitch)> onHoveredPitchChanged_;
    std::function<void(int pitch, int velocity)> onPreviewNote_;
    std::function<bool()> isPlayingCallback_;
    int auditionPitch_ = -1; // Pitch currently sounding from edit audition; -1 if none
    uint64_t defaultUnitId_ = 0;

    // Tool
    PianoRollTool tool_ = PianoRollTool::Pencil;
    
    // Undo Stack
    std::vector<PianoRollCommand> undoStack_;
    std::vector<PianoRollCommand> redoStack_;
    
    // Note Memory (Buffer)
    double lastNoteDuration_ = 1.0; // Default 1 beat
    float lastNoteVelocity_ = 0.79f; // Default velocity (0-1 float, ~MIDI 100)

    // Interaction State
    enum class State : uint8_t {
        None,
        Painting,       // Creating a new note (Drag extends duration)
        BrushPainting,  // Ctrl+pencil drag: lay one note per snap cell crossed
        Moving,         // Moving existing note(s)
        Resizing,       // Resizing existing note(s) (Right edge)
        ResizingLeft,   // Resizing from left edge (moves start, keeps end)
        SelectingBox,   // Dragging selection rectangle
        Erasing,        // Eraser Box/Hover
        CopyDragging    // Alt+drag copy of selection
    };
    State state_ = State::None;
    // Alt held during a move/resize/paint drag: snapToGrid passes through
    // untouched for fine positioning. Recomputed on every mouse event.
    bool fineDrag_ = false;

    // Smart Cursor hover state
    int hoveredNoteIndex_ = -1;
    int hoveredPitch_ = -1;
    double hoverBeat_ = -1.0; // Snapped cursor beat for the draw-mode preview; <0 when idle
    bool hoverOnRightEdge_ = false;
    bool hoverOnLeftEdge_ = false;

    // Alt+drag copy state
    std::vector<int> copyDragIndices_;
    NUIPlatformBridge* platformBridge_ = nullptr;

    // Manual double-click detection: the platform layer never populates
    // NUIMouseEvent::doubleClick, so pair up quick same-spot left presses
    // ourselves (same approach as UnitRow / UnitNameLabel).
    long long lastClickTimeMs_ = 0;
    NUIPoint lastClickPos_;
    
    NUIPoint dragStartPos_;
    float dragStartScrollX_ = 0.0f;
    float dragStartScrollY_ = 0.0f;
    std::vector<MidiNote> dragStartNotes_; // Snapshot for move/resize logic
    
    // For Painting
    int paintingNoteIndex_ = -1; // Index in notes_ of the note being painted
    double paintStartBeat_ = 0.0;
    int paintPitch_ = 0;

    // For Moving: pitch of the grabbed note at drag start, so audition can
    // follow the note under the cursor as it's dragged up and down.
    int moveAnchorPitch_ = 0;
    // Note being placed/dragged, so a floating pitch label can track it.
    int dragAnchorIndex_ = -1;
    // Note whose velocity was last nudged by Alt+wheel — shows a value bubble
    // while the cursor stays on it, cleared when the hover moves away.
    int velocityBubbleIndex_ = -1;

    // Note Properties popup (double-click a note). While open it captures
    // pointer + keys; rows drag (or wheel) to adjust, Reset restores the
    // opening values, Accept/outside-click commits one undo step, Escape cancels.
    int propNoteIndex_ = -1;        // notes_ index being edited; -1 = closed
    NUIRect propPanelRect_;         // fixed at open, screen coords
    std::vector<MidiNote> propUndoSnapshot_; // full notes_ at open (undo baseline)
    MidiNote propOriginalNote_{};   // target note at open (Reset target)
    MidiNote propDragStartNote_{};  // target note at row-drag start
    int propDragField_ = -1;        // row being dragged; -1 = none
    NUIPoint propDragStartPos_;

    void openNoteProperties(int noteIndex);
    void closeNoteProperties(bool accept);
    bool handleNotePropertiesMouse(const NUIMouseEvent& event);
    void renderNoteProperties(NUIRenderer& renderer);
    void applyNotePropertyDelta(int field, float dy, bool coarseStep);
    
    // For Select Box
    NUIRect selectionRect_;
    
    // Snap
    SnapGrid snap_ = SnapGrid::Beat;
    
    // Scale Snapping
    int rootKey_ = 0;
    ScaleType scaleType_ = ScaleType::Chromatic;
    bool snapToScale_ = false;

    // Chord mode: stamp a diatonic triad (root + scale third + scale fifth).
    bool chordMode_ = false;
    /** @brief Build the diatonic triad rooted at @p rootPitch under the current scale. */
    std::vector<int> buildTriad(int rootPitch) const;

    // Edge Scrolling During Drag
    static constexpr float kEdgeThreshold = 50.0f;
    static constexpr float kMaxScrollSpeed = 15.0f;
    bool isEdgeScrolling_ = false;
    NUIPoint edgeScrollDir_; // (-1,-1) to (1,1) indicating direction

    // Helpers
    int findNoteAt(float localX, float localY);
    void commitNotes();
    double snapToGrid(double beat);
    int snapPitchToScale(int pitch);

    // Edit audition — play the note under the cursor while placing/dragging it,
    // so pitch is audible before commit. Suppressed while the transport plays.
    void auditionPitch(int pitch);
    void auditionStop();

    // Paint-brush: stamp one snapped note at the cursor cell if empty, used for
    // Ctrl+pencil drag strokes. Returns true if a note was added.
    bool paintBrushAt(float localX, float localY);
};

// -----------------------------------------------------------------------------
// PianoRollControlPanel: Bottom panel for Velocity/Control changes
// -----------------------------------------------------------------------------
class PianoRollControlPanel : public NUIComponent {
public:
    /** @brief Which per-note property the lane draws and edits. */
    enum class LaneMode : uint8_t { Velocity, Pan };

    PianoRollControlPanel();

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setNoteLayer(std::shared_ptr<PianoRollNoteLayer> layer);
    void setGrid(std::shared_ptr<PianoRollGrid> grid); // Added logic to link Grid

    void setPixelsPerBeat(float ppb);
    void setScrollX(float scrollX);
    /** @brief Set the bar signature so the lane grid matches the ruler/note grid. */
    void setBeatsPerBar(int bpb) { beatsPerBar_ = std::max(1, bpb); repaint(); }

private:
    std::weak_ptr<PianoRollNoteLayer> noteLayer_;
    std::weak_ptr<PianoRollGrid> grid_; // Grid link

    float pixelsPerBeat_;
    float scrollX_;
    int beatsPerBar_ = 4;
    LaneMode laneMode_ = LaneMode::Velocity; // Toggled by clicking the lane's sidebar

    // Interaction
    int hoveringNoteIndex_ = -1;
    bool isDragging_ = false;
    NUIPoint dragStartPos_;
    std::vector<MidiNote> dragUndoSnapshot_; // notes at lane-drag start, for one undo step
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
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;

    // API
    void setNotes(const std::vector<MidiNote>& notes);
    const std::vector<MidiNote>& getNotes() const;
    void setPatternName(const std::string& name);
    void setPatternChoices(const std::vector<PianoRollToolbar::PatternChoice>& choices, int selectedValue);
    void setUnitChoices(const std::vector<PianoRollToolbar::PatternChoice>& choices, int selectedValue);
    void setPatternLengthBeats(double beats);
    void setPlayheadBeat(double beat, bool follow = false);
    void setTotalDurationBeats(double beats);
    void setLocalMinimapVisible(bool visible);
    void applyEdgeAutoScroll(float scrollX, float scrollY);
    double getViewStartBeat() const;
    double getViewDurationBeats() const;
    void setViewWindow(double startBeat, double durationBeats);
    void setOnNotesChanged(std::function<void(const std::vector<MidiNote>&)> cb);
    void setDefaultUnitId(uint64_t unitId);
    
    void setGhostPatterns(const std::vector<PianoRollNoteLayer::GhostPattern>& ghosts);

    /** @brief Set callback to check transport playback state (suppress preview when playing). */
    void setIsPlayingCallback(std::function<bool()> cb);

    /** @brief Forward preview note callback to key lane. */
    void setOnPreviewNote(std::function<void(int pitch, int velocity)> cb);
    void setOnAdjustPatternLength(std::function<void(int barsDelta)> cb);
    void setOnPatternChoiceSelected(std::function<void(int patternValue)> cb);
    void setOnUnitChoiceSelected(std::function<void(int unitValue)> cb);
    void setOnPlayheadScrubbed(std::function<void(double beat, bool active)> cb);

    void setPixelsPerBeat(float ppb);
    void setBeatsPerBar(int bpb);

    // Global Control API
    void setTool(GlobalTool tool);
    void setScale(int root, ScaleType type);
    void setSnapToScale(bool enabled);

    /** @brief Set platform bridge for cursor style changes (forwarded to note layer). */
    void setPlatformBridge(NUIPlatformBridge* bridge);
    
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
    float m_controlPanelHeight = 116.0f;
    
    float m_pixelsPerBeat;
    float m_keyHeight;
    
    float m_scrollX;
    float m_scrollY;
    float m_targetScrollX;
    float m_targetScrollY;
    double m_playheadBeat = 0.0;
    double m_totalDurationBeats = 400.0;
    double m_patternLengthBeats = 8.0;
    bool m_showLocalMinimap = true;
    bool m_showShortcutHelp = false;

    std::function<bool()> m_isPlayingCallback;
    std::function<void(double beat, bool active)> m_onPlayheadScrubbed;

    bool m_isResizingPanel = false; // Added for splitter dragging
    bool m_splitterHovered = false;
    float m_dragStartPanelHeight = 0.0f;
    NUIPoint m_dragStartPos;

    void syncChildren();
    void layoutChildren();
    void updateScrollbars(); // Renamed to updateNavigation?
    void renderShortcutHelp(NUIRenderer& renderer);
};

} // namespace AestraUI
