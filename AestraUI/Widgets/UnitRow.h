// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDragDrop.h"
#include "UnitManager.h"
#include "PatternSource.h" // For PatternID (value type, can't forward-declare)
#include "UnitNameLabel.h"
#include "NUIContextMenu.h"
#include <functional>
#include <memory>
#include <vector>

namespace Aestra { 
    namespace Audio { 
        class TrackManager;
    } 
}

namespace AestraUI {

class UnitRow : public NUIComponent, public IDropTarget {
public:
    /**
     * @brief Create a sequencer row for an Arsenal unit.
     * @param trackManager Shared track manager backing the current project.
     * @param manager Unit manager that owns the row's unit state.
     * @param unitId Unit identifier rendered by this row.
     * @param patternId Active pattern identifier edited through this row.
     */
    UnitRow(std::shared_ptr<Aestra::Audio::TrackManager> trackManager, Aestra::Audio::UnitManager& manager, Aestra::Audio::UnitID unitId, Aestra::Audio::PatternID patternId);
    ~UnitRow() override;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    void onResize(int width, int height) override;

    // IDropTarget interface
    DropFeedback onDragEnter(const DragData& data, const NUIPoint& position) override;
    DropFeedback onDragOver(const DragData& data, const NUIPoint& position) override;
    void onDragLeave() override;
    DropResult onDrop(const DragData& data, const NUIPoint& position) override;
    NUIRect getDropBounds() const override;

    /**
     * @brief Refresh cached unit state from the backing managers.
     */
    void updateState();

    /** @brief Callback fired when row dragging begins. */
    std::function<void(Aestra::Audio::UnitID)> m_onDragStart;
    /** @brief Callback fired when the row is dropped at a new index. */
    std::function<void(Aestra::Audio::UnitID, int)> m_onDrop;
    /** @brief Callback that requests a color picker for the unit. */
    std::function<void()> m_onRequestColorPicker;
    /** @brief Callback used to open the unit editor. */
    std::function<void(Aestra::Audio::UnitID)> m_onEditUnit;
    /** @brief Callback used to trigger sample loading for the unit. */
    std::function<void(Aestra::Audio::UnitID)> m_onLoadUnitSample;
    /** @brief Callback fired when a sample path is dropped directly onto the unit. */
    std::function<void(Aestra::Audio::UnitID, const std::string&)> m_onSampleDropped; // Direct sample path
    /** @brief Callback fired when a plugin identifier is dropped onto the unit. */
    std::function<void(Aestra::Audio::UnitID, const std::string&)> m_onPluginDropped;
    /** @brief Callback fired after the shared pattern backing this row is edited. */
    std::function<void(Aestra::Audio::PatternID)> m_onPatternEdited;
    /** @brief Callback used to open the full Piano Roll editor. */
    std::function<void(Aestra::Audio::PatternID)> m_onOpenPatternEditor;
    /** @brief Callback fired when the unit name is renamed. */
    std::function<void(Aestra::Audio::UnitID, const std::string&)> m_onRenameUnit;
    /** @brief Callback fired when the unit is deleted via context menu. */
    std::function<void(Aestra::Audio::UnitID)> m_onDeleteUnit;
    /** @brief Callback fired when the unit is duplicated via context menu. */
    std::function<void(Aestra::Audio::UnitID)> m_onDuplicateUnit;

    void setOnDragStart(std::function<void(Aestra::Audio::UnitID)> cb) { m_onDragStart = cb; }
    void setOnDrop(std::function<void(Aestra::Audio::UnitID, int)> cb) { m_onDrop = cb; }
    void setOnRequestColorPicker(std::function<void()> cb) { m_onRequestColorPicker = cb; }
    void setOnEditUnit(std::function<void(Aestra::Audio::UnitID)> cb) { m_onEditUnit = cb; }
    void setOnLoadUnitSample(std::function<void(Aestra::Audio::UnitID)> cb) { m_onLoadUnitSample = cb; }
    void setOnSampleDropped(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onSampleDropped = cb; }
    void setOnPluginDropped(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onPluginDropped = cb; }
    void setOnPatternEdited(std::function<void(Aestra::Audio::PatternID)> cb) { m_onPatternEdited = cb; }
    void setOnOpenPatternEditor(std::function<void(Aestra::Audio::PatternID)> cb) { m_onOpenPatternEditor = cb; }
    void setOnRenameUnit(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onRenameUnit = cb; }
    void setOnDeleteUnit(std::function<void(Aestra::Audio::UnitID)> cb) { m_onDeleteUnit = cb; }
    void setOnDuplicateUnit(std::function<void(Aestra::Audio::UnitID)> cb) { m_onDuplicateUnit = cb; }
    
    /**
     * @brief Set the visible step count for the sequencer section.
     * @param count Number of step pads to render.
     */
    void setStepCount(int count);

    /**
     * @brief Choose whether the grid fits the whole loop to width or keeps a
     *        readable minimum pad size and scrolls. Owned by the panel so all
     *        rows + the header agree.
     * @param fit True to fit the entire loop (pads shrink, no scroll).
     */
    void setFitToWidth(bool fit);
    /**
     * @brief Get the visible step count for the sequencer section.
     * @return Number of rendered steps.
     */
    int getStepCount() const { return m_stepCount; }
    
    /**
     * @brief Set the horizontal grid scroll offset (shared across rows by the panel).
     * @param x Scroll offset in pixels (>= 0).
     */
    void setScrollX(float x) {
        x = x < 0.0f ? 0.0f : x;
        if (x != m_scrollX) { m_scrollX = x; invalidateVisuals(); }
    }
    /**
     * @brief Get the horizontal grid scroll offset.
     * @return Scroll offset in pixels.
     */
    float getScrollX() const { return m_scrollX; }
    /**
     * @brief Set the callback fired when the user wheel-scrolls the step grid.
     * When set, the panel owns the scroll offset and pushes it back via setScrollX
     * so every row (and the progress header) stays in lockstep.
     * @param cb Callback receiving the requested scroll delta in pixels.
     */
    void setOnGridScroll(std::function<void(float)> cb) { m_onGridScroll = std::move(cb); }
    /**
     * @brief Restrict rendering and hit-testing to the panel's list viewport.
     * Rows partially outside are clipped; rows fully outside skip rendering and
     * ignore pointer events. An empty rect disables the restriction.
     * @param viewport Viewport rect in window-absolute coordinates.
     */
    void setViewport(const NUIRect& viewport) { m_viewport = viewport; }
    /**
     * @brief Get the unit identifier represented by this row.
     * @return Backing unit identifier.
     */
    Aestra::Audio::UnitID getUnitId() const { return m_unitId; }
    /**
     * @brief Update the row's selected-state styling.
     * @param selected True when the row is currently selected.
     */
    void setSelected(bool selected) { m_isSelected = selected; invalidateVisuals(); }
    /**
     * @brief Check whether the row is selected.
     * @return True when the row is selected.
     */
    bool isSelected() const { return m_isSelected; }

private:
    std::shared_ptr<Aestra::Audio::TrackManager> m_trackManager;
    Aestra::Audio::UnitManager& m_manager;
    Aestra::Audio::UnitID m_unitId;
    Aestra::Audio::PatternID m_patternId; // The active pattern being edited

    // Cached state
    std::string m_name;
    uint32_t m_color;
    Aestra::Audio::UnitGroup m_group;
    Aestra::Audio::UnitType m_type{Aestra::Audio::UnitType::Sampler};
    int m_rootMidiNote = 60; // Pitch that plays the unit's sample untransposed
    bool m_isEnabled = true;
    bool m_isArmed = false;
    bool m_isMuted = false;
    bool m_isSolo = false;
    std::string m_audioClip; // Audio clip filename
    std::string m_pluginId;
    std::string m_sourceSummary;
    std::string m_groupLabel;
    double m_audioDurationSeconds = 0.0;
    std::vector<float> m_audioPreviewWaveform;
    int m_mixerChannel = -1;

    // === Internal State ===
    void layoutNameLabel();
    void showRowContextMenu(const NUIPoint& pos);
    void showDeleteConfirmation(const NUIPoint& pos);

    // === Layout Constants (Premium v2) ===
    static constexpr float ROW_HEIGHT = 56.0f;
    static constexpr float CONTROL_WIDTH = 312.0f;
    static constexpr float DRAG_HANDLE_WIDTH = 16.0f; // Grip area
    static constexpr float COLOR_STRIP_WIDTH = 5.0f;  // Wider strip
    static constexpr float BUTTON_SIZE = 20.0f;
    static constexpr float BUTTON_SPACING = 6.0f;     // More breathing room
    static constexpr float PAD_MIN_SIZE = 20.0f;      // Minimum step pad size
    static constexpr float PAD_SPACING = 3.0f;        // Space between pads
    
    float m_controlWidth = CONTROL_WIDTH;
    
    // Step sequencer
    int m_stepCount = 16;
    bool m_fitToWidth = true; // Fit whole loop vs readable-min + scroll
    float m_scrollX = 0.0f;

    // Pad width for the current fit mode: fit shrinks to show every step,
    // scroll mode keeps a readable minimum and lets m_scrollX page the grid.
    float gridStepWidth(float availWidth) const {
        const float w = availWidth / static_cast<float>(std::max(1, m_stepCount));
        return m_fitToWidth ? std::max(w, 4.0f) : std::max(w, PAD_MIN_SIZE);
    }
    std::function<void(float)> m_onGridScroll; // Panel-owned shared scroll (see setOnGridScroll)
    NUIRect m_viewport{}; // Panel list viewport; empty = unrestricted
    int m_hoveredStep = -1;
    
    // Minimap pitch scroll
    float m_minimapPitchOffset = 0.0f; // Scroll offset for pitch viewport
    
    // === Interaction States ===
    bool m_isHovered = false;
    bool m_isDragging = false;
    bool m_isSelected = false;
    bool m_isDropHighlighted = false;
    NUIPoint m_dragStartPos;

    // Velocity-drag session (Sampler step grid). A press arms a step; a
    // vertical drag sets its velocity, a click without vertical movement
    // toggles the step (place empty / remove active) on release.
    static constexpr float kDefaultStepVelocity = 100.0f / 127.0f;
    static constexpr float kMinStepVelocity = 0.05f;
    int m_velEditStep = -1;         // -1 = no session
    float m_velEditStartY = 0.0f;   // pointer Y at press
    float m_velEditBaseVelocity = kDefaultStepVelocity;
    bool m_velEditMoved = false;    // crossed the drag threshold → velocity edit
    bool m_velEditWasActive = false; // step already had a note at press

    long long m_lastClipClickTimeMs = 0; // For double-click on clip/waveform area

    // === Helpers ===
    void drawContent(NUIRenderer& renderer); // Main drawing logic (cached)
    void drawDragHandle(NUIRenderer& renderer, const NUIRect& bounds);
    void drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds);
    void drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds);
    bool shouldUseNoteRoll() const;
    // Current playhead position within the active pattern (looped), in beats.
    // Returns -1 when the transport is not driving the Arsenal (not pattern
    // mode) so callers can skip the live-step highlight. Mirrors the Arsenal
    // progress-header playhead so the row highlight stays in sync with it.
    double playheadBeatInPattern() const;

    void handleControlClick(const NUIMouseEvent& event, const NUIRect& bounds);
    void handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds);

    // Sampler step-grid note helpers (root pitch, single step). Each returns
    // true if the pattern changed.
    bool stepHasNote(int step, float& velocityOut) const; // true if a note exists at step
    bool placeStepNote(int step);                          // place at root + default velocity
    bool removeStepNote(int step);                         // remove note at step
    void setStepNoteVelocity(int step, float velocity);    // set velocity of note at step

    // Icon drawing helpers
    void drawPowerIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawArmIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawMuteIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawSoloIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawGearIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active); // [NEW]

    // Internal components
    std::shared_ptr<UnitNameLabel> m_nameLabel;
    std::shared_ptr<NUIContextMenu> m_rowContextMenu;

    // Cache management
    bool m_needsCacheUpdate = true;
    void invalidateVisuals() { m_needsCacheUpdate = true; repaint(); }
};

} // namespace AestraUI
