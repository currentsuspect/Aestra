// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITextInput.h"
#include "NUIDragDrop.h"
#include "UnitManager.h"
#include <functional>
#include <memory>

namespace Aestra { 
    namespace Audio { 
        class TrackManager; 
        struct PatternID;
    } 
}

namespace AestraUI {

class UnitRow : public NUIComponent, public IDropTarget {
public:
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

    // Called by parent to refresh state reference
    void updateState();

    // === Callbacks for parent panel ===
    std::function<void(Aestra::Audio::UnitID)> m_onDragStart;
    std::function<void(Aestra::Audio::UnitID, int)> m_onDrop;
    std::function<void()> m_onRequestColorPicker;
    std::function<void(Aestra::Audio::UnitID)> m_onEditUnit;
    std::function<void(Aestra::Audio::UnitID)> m_onLoadUnitSample;
    std::function<void(Aestra::Audio::UnitID, const std::string&)> m_onSampleDropped; // Direct sample path
    
    void setOnDragStart(std::function<void(Aestra::Audio::UnitID)> cb) { m_onDragStart = cb; }
    void setOnDrop(std::function<void(Aestra::Audio::UnitID, int)> cb) { m_onDrop = cb; }
    void setOnRequestColorPicker(std::function<void()> cb) { m_onRequestColorPicker = cb; }
    void setOnEditUnit(std::function<void(Aestra::Audio::UnitID)> cb) { m_onEditUnit = cb; }
    void setOnLoadUnitSample(std::function<void(Aestra::Audio::UnitID)> cb) { m_onLoadUnitSample = cb; }
    void setOnSampleDropped(std::function<void(Aestra::Audio::UnitID, const std::string&)> cb) { m_onSampleDropped = cb; }
    
    // Step count configuration
    void setStepCount(int count) { m_stepCount = count; invalidateVisuals(); }
    int getStepCount() const { return m_stepCount; }
    
    Aestra::Audio::UnitID getUnitId() const { return m_unitId; }

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
    int m_mixerChannel = -1; // Mixer route

    // === Internal State ===
    void startEditing(const NUIRect& rect);
    void stopEditing(bool save);
    
    // === Layout Constants (Premium v2) ===
    static constexpr float ROW_HEIGHT = 42.0f;        // Increased from 28px
    static constexpr float CONTROL_WIDTH = 280.0f;    // Increased from 220px
    static constexpr float DRAG_HANDLE_WIDTH = 16.0f; // Grip area
    static constexpr float COLOR_STRIP_WIDTH = 5.0f;  // Wider strip
    static constexpr float BUTTON_SIZE = 22.0f;       // Larger buttons
    static constexpr float BUTTON_SPACING = 6.0f;     // More breathing room
    static constexpr float PAD_MIN_SIZE = 20.0f;      // Minimum step pad size
    static constexpr float PAD_SPACING = 3.0f;        // Space between pads
    
    float m_controlWidth = CONTROL_WIDTH;
    
    // Step sequencer
    int m_stepCount = 16;
    float m_scrollX = 0.0f;
    int m_hoveredStep = -1;
    
    // === Interaction States ===
    bool m_isHovered = false;
    bool m_isDragging = false;
    NUIPoint m_dragStartPos;
    
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
