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
    : WindowPanel("THE ARSENAL")
    , m_trackManager(std::move(trackManager))
{
    // setId("ArsenalPanel"); // Handled by WindowPanel title
    ensureDefaultPattern(); // Auto-create Pattern 1
    
    // Create default Unit 1 for immediate playback
    if (m_trackManager) {
        auto& unitMgr = m_trackManager->getUnitManager();
        if (unitMgr.getUnitCount() == 0) {
            unitMgr.createUnit("Sampler 1", UnitGroup::Drums);
        }
    }

    // Modern list container
    m_listContainer = std::make_shared<NomadUI::NUIComponent>();
    m_listContainer->setId("ArsenalListContainer");
    
    // Set as the content of the WindowPanel
    setContent(m_listContainer);

    refreshUnits();

    // Create color picker (initially hidden)
    m_colorPicker = std::make_shared<UnitColorPicker>();
    m_colorPicker->setOnColorSelected([this](uint32_t color) {
        if (m_colorPickerTargetUnit > 0) {
            m_trackManager->getUnitManager().setUnitColor(m_colorPickerTargetUnit, color);
            refreshUnits();
        }
    });
    
    // ScrollView wrapper would go here, for now directly setting content
}

void ArsenalPanel::createLayout() {
    // This method is no longer needed as layout is handled by WindowPanel and setContent
    // The content of this method has been moved to the constructor or is handled by the base class.
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
        
        // Set step count
        row->setStepCount(m_stepCount);
        
        // Set up callbacks for drag-drop and color picker
        row->setOnDragStart([this](UnitID id) {
            onUnitDragStart(id);
        });
        
        row->setOnRequestColorPicker([this, i, unitIDs]() {
            // Get the row bounds for positioning
            if (i < m_unitRows.size()) {
                auto bounds = m_unitRows[i]->getBounds();
                showColorPicker(unitIDs[i], NUIPoint(bounds.x + 30, bounds.y + bounds.height));
            }
        });
        
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

void ArsenalPanel::setStepCount(int count) {
    if (count != 16 && count != 32 && count != 64) {
        count = 16; // Default to 16 if invalid
    }
    m_stepCount = count;
    
    // Update all unit rows
    for (auto& row : m_unitRows) {
        if (row) {
            row->setStepCount(count);
        }
    }
    
    repaint();
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
    // Custom rendering is now handled by the WindowPanel base class.
    // Only render the color picker if it's visible.
    if (m_colorPicker && m_colorPicker->isShowing()) {
        m_colorPicker->onRender(renderer);
    }
    // The base WindowPanel::onRender will handle its own background, title, and children rendering.
    WindowPanel::onRender(renderer);
}

void ArsenalPanel::layoutUnits() {
    if (!m_listContainer) return;
    
    NUIRect bounds = m_listContainer->getBounds();
    float width = bounds.width;
    float startY = bounds.y;
    
    // Units start with a small padding within the container
    float yPos = startY + 6.0f - m_scrollY;
    float spacing = 4.0f;        // Increased from 2px
    float rowHeight = 42.0f;     // Increased from 28px - matches UnitRow::ROW_HEIGHT
    
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
            addBtn->setBounds(NUIRect(bounds.x + 8, yPos + 4.0f, width - 16, 36.0f));
        }
    }
}

void ArsenalPanel::onResize(int width, int height) {
    // Custom resize logic is now handled by the WindowPanel base class.
    // We only need to ensure our internal content (m_listContainer) is laid out.
    WindowPanel::onResize(width, height); // Call base class first
    layoutUnits(); // Layout units within the content area
}

// === Drag-Drop Callbacks ===

void ArsenalPanel::onUnitDragStart(UnitID unitId) {
    m_isDragging = true;
    m_draggedUnitId = unitId;
    m_dropTargetIndex = -1;
    Log::info("[Arsenal] Started dragging Unit " + std::to_string(unitId));
}

void ArsenalPanel::onUnitDrop(UnitID unitId, int dropIndex) {
    if (!m_trackManager) return;
    
    m_trackManager->getUnitManager().reorderUnit(unitId, static_cast<size_t>(dropIndex));
    m_isDragging = false;
    m_draggedUnitId = 0;
    m_dropTargetIndex = -1;
    
    refreshUnits();
    Log::info("[Arsenal] Dropped Unit " + std::to_string(unitId) + " at index " + std::to_string(dropIndex));
}

void ArsenalPanel::showColorPicker(UnitID unitId, NUIPoint position) {
    m_colorPickerTargetUnit = unitId;
    if (m_colorPicker) {
        m_colorPicker->showAt(position);
        repaint();
    }
}

// === Copy/Paste Operations ===

void ArsenalPanel::copySelectedPattern() {
    if (!m_trackManager || m_selectedUnitId == 0 || !m_activePatternID.isValid()) return;
    
    auto* pattern = m_trackManager->getPatternManager().getPattern(m_activePatternID);
    if (!pattern || !pattern->isMidi()) return;
    
    auto& midi = std::get<MidiPayload>(pattern->payload);
    
    PatternClipboard clip;
    clip.sourceUnitId = m_selectedUnitId;
    
    // Copy notes belonging to selected unit
    for (const auto& note : midi.notes) {
        if (note.unitId == m_selectedUnitId || note.unitId == 0) {
            clip.notes.push_back(note);
        }
    }
    
    m_clipboard = clip;
    Log::info("[Arsenal] Copied " + std::to_string(clip.notes.size()) + " notes from Unit " + std::to_string(m_selectedUnitId));
}

void ArsenalPanel::pastePattern() {
    if (!m_trackManager || !m_clipboard.has_value() || m_selectedUnitId == 0 || !m_activePatternID.isValid()) return;
    
    auto& pm = m_trackManager->getPatternManager();
    
    pm.applyPatch(m_activePatternID, [this](PatternSource& p) {
        if (!p.isMidi()) return;
        auto& midi = std::get<MidiPayload>(p.payload);
        
        // Paste notes with remapped unitId
        for (auto note : m_clipboard->notes) {
            note.unitId = m_selectedUnitId;
            midi.notes.push_back(note);
        }
    });
    
    refreshUnits();
    Log::info("[Arsenal] Pasted " + std::to_string(m_clipboard->notes.size()) + " notes to Unit " + std::to_string(m_selectedUnitId));
}

// === Event Handlers ===

bool ArsenalPanel::onMouseEvent(const NUIMouseEvent& event) {
    // Handle color picker first if visible
    if (m_colorPicker && m_colorPicker->isShowing()) {
        if (m_colorPicker->onMouseEvent(event)) {
            repaint();
            return true;
        }
        // Click outside closes picker
        if (event.pressed && !m_colorPicker->getBounds().contains(event.position)) {
            m_colorPicker->hide();
            repaint();
        }
    }

    // Handle drag-drop logic for reordering units
    if (m_isDragging) {
        auto bounds = m_listContainer->getBounds();
        float rowHeight = 42.0f;
        float spacing = 4.0f;
        
        // Calculate which drop zone we're over
        float localY = event.position.y - bounds.y + m_scrollY - 6.0f;
        int dropIndex = static_cast<int>((localY + (rowHeight + spacing) / 2) / (rowHeight + spacing));
        dropIndex = std::clamp(dropIndex, 0, static_cast<int>(m_unitRows.size()));
        
        if (dropIndex != m_dropTargetIndex) {
            m_dropTargetIndex = dropIndex;
            repaint();
        }
        
        // Release to drop
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            onUnitDrop(m_draggedUnitId, m_dropTargetIndex);
            return true;
        }
    }
    
    // Scroll handling
    if (std::abs(event.wheelDelta) > 0.001f && getBounds().contains(event.position)) {
        float contentHeight = (m_unitRows.size() * (42.0f + 4.0f)) + 40.0f + 12.0f; 
        float viewportHeight = m_listContainer ? m_listContainer->getBounds().height : 100.0f;
        float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
        
        m_scrollY -= event.wheelDelta * 40.0f;
        m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
        
        layoutUnits();
        return true;
    }
    
    // Track which unit is selected (for copy/paste)
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_unitRows.size(); ++i) {
            if (m_unitRows[i] && m_unitRows[i]->getBounds().contains(event.position)) {
                m_selectedUnitId = m_unitRows[i]->getUnitId();
                break;
            }
        }
    }
    
    return WindowPanel::onMouseEvent(event);
}

bool ArsenalPanel::onKeyEvent(const NUIKeyEvent& event) {
    if (!event.pressed) return false;
    
    // Check for Ctrl modifier
    bool isCtrl = (event.modifiers & NUIModifiers::Ctrl);
    
    // Ctrl+C: Copy
    if (isCtrl && (event.keyCode == NUIKeyCode::C)) {
        copySelectedPattern();
        return true;
    }
    
    // Ctrl+V: Paste
    if (isCtrl && (event.keyCode == NUIKeyCode::V)) {
        pastePattern();
        return true;
    }
    
    return false;
}

} // namespace Audio
} // namespace Nomad
