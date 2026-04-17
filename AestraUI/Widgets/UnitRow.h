// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITextInput.h"
#include "NUIDragDrop.h"
#include "UnitManager.h"
#include "PatternSource.h" // For PatternID (value type, can't forward-declare)
#include <functional>
#include <memory>

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
    
    void setOnDragStart(std::function<void(Aestra::Audio::UnitID)> cb) { m_onDragStart = cb; }
    void setOnDrop(std::function<void(Aestra::Audio::UnitID, int)> cb) { m_onDrop = cb; }
    void setOnRequestColorPicker(std::function<void()> cb) { m_onRequestColorPicker = cb; }
    void setOnEditUnit(std::function<void(Aestra::Audio::UnitID)> cb) { m_onEditUnit = cb; }
    void setOnLoadUnitSample(std::function<void(Aestra::Audio::UnitID)> cb) { m_onLoadUnitSample = cb; }
    void setOnSampleDropped(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onSampleDropped = cb; }
    void setOnPluginDropped(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onPluginDropped = cb; }
    void setOnPatternEdited(std::function<void(Aestra::Audio::PatternID)> cb) { m_onPatternEdited = cb; }
    
    /**
     * @brief Set the visible step count for the sequencer section.
     * @param count Number of step pads to render.
     */
    void setStepCount(int count) { m_stepCount = count; invalidateVisuals(); }
    /**
     * @brief Get the visible step count for the sequencer section.
     * @return Number of rendered steps.
     */
    int getStepCount() const { return m_stepCount; }
    
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
    bool m_isEnabled = true;
    bool m_isArmed = false;
    bool m_isMuted = false;
    bool m_isSolo = false;
    std::string m_audioClip; // Audio clip filename
    std::string m_pluginId;
    std::string m_sourceSummary;
    std::string m_groupLabel;
    int m_mixerChannel = -1; // Mixer route

    // === Internal State ===
    void startEditing(const NUIRect& rect);
    void stopEditing(bool save);
    
    // === Layout Constants (Premium v2) ===
    static constexpr float ROW_HEIGHT = 56.0f;
    static constexpr float CONTROL_WIDTH = 336.0f;
    static constexpr float DRAG_HANDLE_WIDTH = 16.0f; // Grip area
    static constexpr float COLOR_STRIP_WIDTH = 5.0f;  // Wider strip
    static constexpr float BUTTON_SIZE = 20.0f;
    static constexpr float BUTTON_SPACING = 6.0f;     // More breathing room
    static constexpr float PAD_MIN_SIZE = 20.0f;      // Minimum step pad size
    static constexpr float PAD_SPACING = 3.0f;        // Space between pads
    
    float m_controlWidth = CONTROL_WIDTH;
    
    // Step sequencer
    int m_stepCount = 16;
    float m_scrollX = 0.0f;
    int m_hoveredStep = -1;
    
    // Minimap pitch scroll
    float m_minimapPitchOffset = 0.0f; // Scroll offset for pitch viewport
    
    // === Interaction States ===
    bool m_isHovered = false;
    bool m_isDragging = false;
    bool m_isSelected = false;
    NUIPoint m_dragStartPos;
    bool m_isStepEditing = false;
    int m_stepEditStart = -1;
    int m_stepEditEndExclusive = -1;
    
    // Inline name editing
    bool m_isEditingName = false;
    std::string m_editBuffer;
    long long m_lastClickTimeMs = 0;
    long long m_lastClipClickTimeMs = 0; // For double-click on clip/waveform area
    
    // === Helpers ===
    void drawContent(NUIRenderer& renderer); // Main drawing logic (cached)
    void drawDragHandle(NUIRenderer& renderer, const NUIRect& bounds);
    void drawControlBlock(NUIRenderer& renderer, const NUIRect& bounds);
    void drawContextBlock(NUIRenderer& renderer, const NUIRect& bounds);
    
    void handleControlClick(const NUIMouseEvent& event, const NUIRect& bounds);
    void handleContextClick(const NUIMouseEvent& event, const NUIRect& bounds);
    
    // Icon drawing helpers
    void drawPowerIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawArmIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawMuteIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawSoloIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active);
    void drawGearIcon(NUIRenderer& renderer, const NUIRect& bounds, bool active); // [NEW]
    
    // Internal components
    std::shared_ptr<NUITextInput> m_nameInput;
    
    // Cache management
    bool m_needsCacheUpdate = true;
    void invalidateVisuals() { m_needsCacheUpdate = true; repaint(); }
};

} // namespace AestraUI
