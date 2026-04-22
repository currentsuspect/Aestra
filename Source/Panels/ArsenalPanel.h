// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "TrackManager.h"
#include "../AestraUI/Widgets/UnitRow.h"
#include "../AestraUI/Widgets/UnitColorPicker.h"
#include "NUIComponent.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include <optional>

namespace Aestra {
namespace Audio {

/**
 * @brief The Arsenal: Unit-Based Sequencer Window
 * Standardized as a WindowPanel (v3.1)
 */
class ArsenalPanel : public WindowPanel, public AestraUI::IDropTarget {
public:
    /** @brief Create the Arsenal sequencer panel. */
    ArsenalPanel(std::shared_ptr<TrackManager> trackManager);
    /** @brief Destroy the Arsenal sequencer panel. */
    ~ArsenalPanel() override;

    /** @brief Render the Arsenal panel and unit rows. */
    void onRender(AestraUI::NUIRenderer& renderer) override;
    /** @brief Relayout the panel after a resize. */
    void onResize(int width, int height) override;
    /** @brief Advance Arsenal playback state and UI sync. */
    void onUpdate(double dt) override;
    /** @brief Handle mouse input routed to the Arsenal panel. */
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    /** @brief Handle drag-enter for plugin/sample drops. */
    AestraUI::DropFeedback onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    /** @brief Handle drag-over for plugin/sample drops. */
    AestraUI::DropFeedback onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    /** @brief Clear drag state when the cursor leaves the panel. */
    void onDragLeave() override;
    /** @brief Commit a drop operation onto the Arsenal panel. */
    AestraUI::DropResult onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    /** @brief Get the drop target bounds for the Arsenal panel. */
    AestraUI::NUIRect getDropBounds() const override;
    /** @brief Register the panel and current rows for lifecycle-scoped drag/drop. */
    void registerDropTargets(bool reorder = false);
    /** @brief Unregister the panel and current rows from drag/drop. */
    void unregisterDropTargets();

    /** @brief Rebuild the UI rows from the current UnitManager state. */
    void refreshUnits();
    
    /** @brief Bind the pattern browser used for bidirectional pattern sync. */
    void setPatternBrowser(class PatternBrowserPanel* browser) { m_patternBrowser = browser; }
    
    /** @brief Set the active pattern edited by Arsenal. */
    void setActivePattern(PatternID patternId);
    /** @brief Select the Arsenal unit used for editing and clipboard actions. */
    void setSelectedUnit(UnitID unitId);
    
    /** @brief Get the active pattern edited by Arsenal. */
    PatternID getActivePatternID() const { return m_activePatternID; }
    
    /** @brief Set the visible step count for Arsenal rows. */
    void setStepCount(int count);
    /** @brief Get the visible step count for Arsenal rows. */
    int getStepCount() const { return m_stepCount; }
    
    /** @brief Copy notes from the selected unit into the local clipboard. */
    void copySelectedPattern();
    /** @brief Paste clipboard notes into the selected unit. */
    void pastePattern();
    
    /** @brief Set the callback used to open a unit editor. */
    void setOnRequestEditor(std::function<void(UnitID)> cb) { m_onRequestEditor = cb; }
    /** @brief Set the callback used to open a pattern in the Piano Roll. */
    void setOnRequestPatternEditor(std::function<void(PatternID)> cb) { m_onRequestPatternEditor = cb; }
    /** @brief Set the callback used to request sample loading. */
    void setOnRequestLoadSample(std::function<void(UnitID)> cb) { m_onRequestLoadSample = cb; }
    /** @brief Set the callback used for generic plugin drops. */
    void setOnPluginDropped(std::function<void(const std::string&)> cb) { m_onPluginDropped = cb; }
    /** @brief Set the callback used for plugin drops onto a specific unit. */
    void setOnPluginDroppedToUnit(std::function<void(UnitID, const std::string&)> cb) { m_onPluginDroppedToUnit = cb; }
    /** @brief Set the callback used when an audio sample is dropped onto a unit. */
    void setOnSampleDroppedToUnit(std::function<void(UnitID, const std::string&)> cb) { m_onSampleDroppedToUnit = cb; }
    /** @brief Set the callback fired when unit selection changes. */
    void setOnSelectedUnitChanged(std::function<void(UnitID)> cb) { m_onSelectedUnitChanged = std::move(cb); }
    /** @brief Set the callback used to activate playback before editing. */
    void setOnRequestPlaybackActivation(std::function<void()> cb) { m_onRequestPlaybackActivation = std::move(cb); }
    /** @brief Set the callback fired when the active pattern is edited. */
    void setOnPatternEdited(std::function<void(PatternID)> cb) { m_onPatternEdited = std::move(cb); }
    /** @brief Set the callback fired when the active pattern changes. */
    void setOnActivePatternChanged(std::function<void(PatternID)> cb) { m_onActivePatternChanged = std::move(cb); }
    /** @brief Get the currently selected unit identifier. */
    UnitID getSelectedUnitId() const { return m_selectedUnitId; }

private:
    std::shared_ptr<TrackManager> m_trackManager;
    
    // Container for the scrollable list of units
    std::shared_ptr<AestraUI::NUIComponent> m_listContainer;
    std::vector<std::shared_ptr<AestraUI::UnitRow>> m_unitRows;
    
    // Footer controls
    std::shared_ptr<AestraUI::NUIComponent> m_footer;
    std::shared_ptr<AestraUI::NUIButton> m_playBtn; // Play/Stop Button
    
    // Color picker popup
    std::shared_ptr<AestraUI::UnitColorPicker> m_colorPicker;
    UnitID m_colorPickerTargetUnit = 0;
    
    // Drag-drop state
    bool m_isDragging = false;
    UnitID m_draggedUnitId = 0;
    int m_dropTargetIndex = -1;
    
    // Layout & Scrolling
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    int m_stepCount = 16; // Default step count
    void layoutUnits();
    
    // Pattern Progress Visualization
    static constexpr float PROGRESS_HEADER_HEIGHT = 28.0f;
    int m_currentPlayStep = -1;  // Current step for visualization (-1 = not playing)
    void drawProgressHeader(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds);
    int calculateCurrentStep(); // Calculate step from TrackManager clock
    void adjustPatternBars(int deltaBars);
    void createUnitOfType(UnitType type);
    void drawUnitTypePicker(AestraUI::NUIRenderer& renderer);

    // Pattern Management (driven by Pattern Browser)
    PatternID m_activePatternID{}; // The pattern being edited
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
    bool removeSelectedUnit();
    void syncRowSelection();
    void removeUnitNotes(UnitID unitId);
    
    // Drag-drop callbacks
    void onUnitDragStart(UnitID unitId);
    void onUnitDrop(UnitID unitId, int dropIndex);
    void showColorPicker(UnitID unitId, AestraUI::NUIPoint position);
    
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;
    
    std::function<void(UnitID)> m_onRequestEditor;
    std::function<void(PatternID)> m_onRequestPatternEditor;
    std::function<void(UnitID)> m_onRequestLoadSample;
    std::function<void(const std::string&)> m_onPluginDropped;
    std::function<void(UnitID, const std::string&)> m_onPluginDroppedToUnit;
    std::function<void(UnitID, const std::string&)> m_onSampleDroppedToUnit;
    std::function<void(UnitID)> m_onSelectedUnitChanged;
    std::function<void()> m_onRequestPlaybackActivation;
    std::function<void(PatternID)> m_onPatternEdited;
    std::function<void(PatternID)> m_onActivePatternChanged;
    bool m_dropTargetRegistered = false;
    bool m_showUnitTypePicker = false;
    AestraUI::NUIRect m_addUnitButtonRect{};
    AestraUI::NUIRect m_unitTypePickerRect{};
    AestraUI::NUIRect m_barsDecrementRect{};
    AestraUI::NUIRect m_barsValueRect{};
    AestraUI::NUIRect m_barsIncrementRect{};
};

} // namespace Audio
} // namespace Aestra
