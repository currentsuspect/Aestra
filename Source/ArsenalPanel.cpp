// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ArsenalPanel.h"
#include "PatternBrowserPanel.h" // For m_patternBrowser
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadCore/include/NomadLog.h"

using namespace NomadUI;

namespace Nomad {
namespace Audio {

ArsenalPanel::ArsenalPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("The Arsenal")
    , m_trackManager(std::move(trackManager))
{
    ensureDefaultPattern(); // Auto-create Pattern 1
    
    // Create default Unit 1 for immediate playback
    if (m_trackManager) {
        auto& unitMgr = m_trackManager->getUnitManager();
        if (unitMgr.getUnitCount() == 0) {
            Log::info("[Arsenal] Creating default Unit 1");
            unitMgr.createUnit("Unit 1", UnitGroup::Synth);
        }
    }
    
    createLayout();
    refreshUnits();
}

void ArsenalPanel::createLayout() {
    auto& theme = NUIThemeManager::getInstance();
    
    // Main scrolling list for units
    m_listContainer = std::make_shared<NUIComponent>();
    
    // ScrollView wrapper would go here, for now directly setting content
    setContent(m_listContainer);
}

void ArsenalPanel::refreshUnits() {
    if (!m_listContainer || !m_trackManager) return;
    auto& theme = NUIThemeManager::getInstance();
    
    // Clear previous children
    m_listContainer->removeAllChildren();
    m_unitRows.clear();
    
    // Build unit rows
    auto& unitMgr = m_trackManager->getUnitManager();
    auto unitIDs = unitMgr.getAllUnitIDs();
    
    for (size_t i = 0; i < unitIDs.size(); ++i) {
        auto row = std::make_shared<UnitRow>(m_trackManager, unitMgr, unitIDs[i], m_activePatternID);
        m_listContainer->addChild(row);
        m_unitRows.push_back(row);
    }
    
    // Add "Add Unit" button
    auto addBtn = std::make_shared<NUIButton>("+ Add Unit");
    addBtn->setBackgroundColor(theme.getColor("surfaceTertiary").withAlpha(0.5f));
    addBtn->setHoverColor(theme.getColor("surfaceTertiary"));
    addBtn->setTextColor(theme.getColor("textSecondary"));
    addBtn->setOnClick([this]() {
        onAddUnit();
    });
    m_listContainer->addChild(addBtn);
    
    layoutUnits();
    
    if (auto parent = getParent()) {
        parent->repaint();
    }
}

void ArsenalPanel::onAddUnit() {
    if (!m_trackManager) return;
    std::string name = "Unit " + std::to_string(m_trackManager->getUnitManager().getUnitCount() + 1);
    m_trackManager->getUnitManager().createUnit(name, UnitGroup::Synth);
    refreshUnits();
}

void ArsenalPanel::setActivePattern(PatternID patternId) {
    if (m_activePatternID == patternId) return;
    m_activePatternID = patternId;
    refreshUnits(); // Rebuild UI with new pattern context
}

void ArsenalPanel::ensureDefaultPattern() {
    if (!m_trackManager) return;
    auto& pm = m_trackManager->getPatternManager();
    
    // Check if Pattern 1 exists
    auto patterns = pm.getAllPatterns();
    for (const auto& p : patterns) {
        if (p->name == "Pattern 1") {
            m_activePatternID = p->id;
            return;
        }
    }
    
    // Create Pattern 1 if it doesn't exist
    Nomad::Audio::MidiPayload empty;
    m_activePatternID = pm.createMidiPattern("Pattern 1", 4.0, empty);
    
    // Refresh Pattern Browser to show Pattern 1
    if (m_patternBrowser) {
        m_patternBrowser->refreshPatterns();
    }
}

void ArsenalPanel::onRender(NUIRenderer& renderer) {
    WindowPanel::onRender(renderer);
}

void ArsenalPanel::layoutUnits() {
    if (!m_listContainer) return;
    
    NUIRect bounds = m_listContainer->getBounds();
    float width = bounds.width;
    float startY = bounds.y;
    
    float yPos = startY + 4.0f - m_scrollY;
    float spacing = 2.0f;
    float rowHeight = 28.0f;
    
    // Layout unit rows
    for (auto& row : m_unitRows) {
        if (row) {
            row->setBounds(NUIRect(bounds.x, yPos, width, rowHeight));
            yPos += rowHeight + spacing;
        }
    }
    
    // Add Unit button (last child)
    auto children = m_listContainer->getChildren();
    if (!children.empty()) {
        auto addBtn = children.back();
        bool isAddButton = m_unitRows.empty() || (addBtn != m_unitRows.back());
        if (addBtn && isAddButton) {
            addBtn->setBounds(NUIRect(bounds.x + 8, yPos + 4.0f, width - 16, 32.0f));
        }
    }
}

void ArsenalPanel::onResize(int width, int height) {
    WindowPanel::onResize(width, height);
    // m_listContainer is resized by WindowPanel logic usually, but let's ensure layout updates
    layoutUnits();
}

bool ArsenalPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (std::abs(event.wheelDelta) > 0.001f) {
        // Approx content height calculation
        float contentHeight = (m_unitRows.size() * (28.0f + 2.0f)) + 32.0f + 8.0f; 
        float viewportHeight = m_listContainer ? m_listContainer->getBounds().height : 100.0f;
        float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
        
        m_scrollY -= event.wheelDelta * 40.0f;
        m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
        
        layoutUnits();
        return true;
    }
    
    return WindowPanel::onMouseEvent(event);
}

} // namespace Audio
} // namespace Nomad
