// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadAudio/include/PatternManager.h"
#include "../NomadAudio/include/PatternSource.h"
#include <memory>
#include <vector>
#include <functional>

namespace Nomad {
namespace Audio {

class TrackManager;

/**
 * @brief Pattern Browser Panel - Lists all patterns for selection and drag-to-timeline
 */
class PatternBrowserPanel : public NomadUI::NUIComponent {
public:
    PatternBrowserPanel(TrackManager* trackManager = nullptr);
    ~PatternBrowserPanel() override = default;

    // Refresh the pattern list from PatternManager
    void refreshPatterns();

    // Callbacks
    void setOnPatternSelected(std::function<void(PatternID)> callback) { m_onPatternSelected = callback; }
    void setOnPatternDragStart(std::function<void(PatternID)> callback) { m_onPatternDragStart = callback; }
    void setOnPatternDoubleClick(std::function<void(PatternID)> callback) { m_onPatternDoubleClick = callback; }

    // Currently selected pattern
    PatternID getSelectedPatternId() const { return m_selectedPatternId; }

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    void onResize(int width, int height) override;

private:
    TrackManager* m_trackManager = nullptr;
    
    // Pattern list
    struct PatternEntry {
        PatternID id;
        std::string name;
        bool isMidi;
        double lengthBeats;
        int mixerChannel = -1;  // -1 = auto, 0+ = routed to specific channel
    };
    std::vector<PatternEntry> m_patterns;
    
    PatternID m_selectedPatternId;
    PatternID m_hoveredPatternId;
    
    // UI Layout
    float m_headerHeight = 40.0f;
    float m_itemHeight = 32.0f;
    float m_scrollOffset = 0.0f;
    
    // Callbacks
    std::function<void(PatternID)> m_onPatternSelected;
    std::function<void(PatternID)> m_onPatternDragStart;
    std::function<void(PatternID)> m_onPatternDoubleClick;
    
    // Buttons
    std::shared_ptr<NomadUI::NUIButton> m_createButton;
    std::shared_ptr<NomadUI::NUIButton> m_duplicateButton;
    std::shared_ptr<NomadUI::NUIButton> m_deleteButton;
    
    // Icons
    std::shared_ptr<NomadUI::NUIIcon> m_addIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_copyIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_trashIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_midiIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_audioIcon;
    
    // Theme colors (cached)
    NomadUI::NUIColor m_backgroundColor;
    NomadUI::NUIColor m_textColor;
    NomadUI::NUIColor m_borderColor;
    NomadUI::NUIColor m_selectedColor;
    
    // Drag state
    bool m_isDragging = false;
    PatternID m_dragPatternId;
    
    // Improved drag logic
    bool m_dragPotential = false;
    NomadUI::NUIPoint m_dragStartPos;
    
    // Double-click detection
    double m_lastClickTime = 0.0;
    PatternID m_lastClickedPatternId;
    
    void renderHeader(NomadUI::NUIRenderer& renderer);
    void renderPatternList(NomadUI::NUIRenderer& renderer);
    void renderPatternItem(NomadUI::NUIRenderer& renderer, const PatternEntry& entry, float y, bool selected, bool hovered);
};

} // namespace Audio
} // namespace Nomad
