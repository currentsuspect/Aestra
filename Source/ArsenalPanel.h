// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "WindowPanel.h"
#include "../NomadAudio/include/TrackManager.h"
#include "../NomadUI/Widgets/UnitRow.h"
#include "../NomadUI/Core/NUIComponent.h"

namespace Nomad {
namespace Audio {

/**
 * @brief The Arsenal: Unit-Based Sequencer Window
 * Replaces the traditional Step Sequencer with a persistent "Rack" of Units.
 */
class ArsenalPanel : public WindowPanel {
public:
    ArsenalPanel(std::shared_ptr<TrackManager> trackManager);
    ~ArsenalPanel() override = default;

    void onRender(NomadUI::NUIRenderer& renderer) override;
    
    // Rebuilds the UI from UnitManager state
    void refreshUnits();
    
    // Set Pattern Browser for bidirectional communication
    void setPatternBrowser(class PatternBrowserPanel* browser) { m_patternBrowser = browser; }
    
    // Called when Pattern Browser selection changes
    void setActivePattern(PatternID patternId);
    
    // Get current active pattern
    PatternID getActivePatternID() const { return m_activePatternID; }

private:
    std::shared_ptr<TrackManager> m_trackManager;
    
    // Container for the scrollable list of units
    // Container for the scrollable list of units
    std::shared_ptr<NomadUI::NUIComponent> m_listContainer;
    std::vector<std::shared_ptr<NomadUI::UnitRow>> m_unitRows;
    
    // Footer controls
    std::shared_ptr<NomadUI::NUIComponent> m_footer;
    
    // Layout & Scrolling
    float m_scrollY = 0.0f;
    void layoutUnits();

    // Pattern Management (driven by Pattern Browser)
    PatternID m_activePatternID = 0; // The pattern being edited
    class PatternBrowserPanel* m_patternBrowser = nullptr; // For refresh
    void ensureDefaultPattern(); // Auto-create Pattern 1 if needed

    void createLayout();
    void onAddUnit();
    
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
};

} // namespace Audio
} // namespace Nomad
