// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadUI/Widgets/UnitRow.h"
#include "../NomadUI/Widgets/UnitColorPicker.h"
#include "../NomadUI/Core/NUIComponent.h"
#include <optional>

namespace Nomad {
namespace Audio {

/**
 * @brief The Arsenal: Unit-Based Sequencer Window
 * Standardized as a WindowPanel (v3.1)
 */
class ArsenalPanel : public WindowPanel {
public:
    ArsenalPanel(std::shared_ptr<TrackManager> trackManager);
    ~ArsenalPanel() override = default;

    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;

    // Rebuilds the UI from UnitManager state
    void refreshUnits();
    
    // Set Pattern Browser for bidirectional communication
    void setPatternBrowser(class PatternBrowserPanel* browser) { m_patternBrowser = browser; }
    
    // Called when Pattern Browser selection changes
    void setActivePattern(PatternID patternId);
    
    // Get current active pattern
    PatternID getActivePatternID() const { return m_activePatternID; }
    
    // Step count configuration (16/32/64)
    void setStepCount(int count);
    int getStepCount() const { return m_stepCount; }
    
    // Copy/paste operations
    void copySelectedPattern();
    void pastePattern();

private:
    std::shared_ptr<TrackManager> m_trackManager;
    
    // Container for the scrollable list of units
    std::shared_ptr<NomadUI::NUIComponent> m_listContainer;
    std::vector<std::shared_ptr<NomadUI::UnitRow>> m_unitRows;
    
    // Footer controls
    std::shared_ptr<NomadUI::NUIComponent> m_footer;
    
    // Color picker popup
    std::shared_ptr<NomadUI::UnitColorPicker> m_colorPicker;
    UnitID m_colorPickerTargetUnit = 0;
    
    // Drag-drop state
    bool m_isDragging = false;
    UnitID m_draggedUnitId = 0;
    int m_dropTargetIndex = -1;
    
    // Layout & Scrolling
    float m_scrollY = 0.0f;
    int m_stepCount = 16; // Default step count
    void layoutUnits();

    // Pattern Management (driven by Pattern Browser)
    PatternID m_activePatternID = 0; // The pattern being edited
    class PatternBrowserPanel* m_patternBrowser = nullptr; // For refresh
    void ensureDefaultPattern(); // Auto-create Pattern 1 if needed
    
    // Copy/paste clipboard
    struct PatternClipboard {
        std::vector<MidiNote> notes;
        UnitID sourceUnitId;
    };
    std::optional<PatternClipboard> m_clipboard;
    UnitID m_selectedUnitId = 0; // Currently selected unit for copy/paste

    void createLayout();
    void onAddUnit();
    
    // Drag-drop callbacks
    void onUnitDragStart(UnitID unitId);
    void onUnitDrop(UnitID unitId, int dropIndex);
    void showColorPicker(UnitID unitId, NomadUI::NUIPoint position);
    
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
};

} // namespace Audio
} // namespace Nomad
