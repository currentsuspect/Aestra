// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PatternBrowserPanel.h"
#include "TrackManager.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadCore/include/NomadLog.h"
#include <chrono>

namespace Nomad {
namespace Audio {

PatternBrowserPanel::PatternBrowserPanel(TrackManager* trackManager)
    : m_trackManager(trackManager)
    , m_headerHeight(40.0f) // Standard header height
    , m_itemHeight(32.0f)   // Standard item height
{
    setId("PatternBrowserPanel");
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // Cache theme colors (matching FileBrowser color scheme)
    m_backgroundColor = themeManager.getColor("backgroundSecondary");  // #1b1b1f
    m_textColor = themeManager.getColor("textPrimary");                // #e6e6eb
    m_borderColor = themeManager.getColor("interfaceBorder");          // #2e2e35
    m_selectedColor = themeManager.getColor("primary");                // Use Theme Primary!
    
    // Initialize SVG icons
    m_addIcon = std::make_shared<NomadUI::NUIIcon>();
    // Modern Boxed Plus
    const char* addSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="3" width="18" height="18" rx="4" ry="4"/><line x1="12" y1="8" x2="12" y2="16"/><line x1="8" y1="12" x2="16" y2="12"/></svg>)";
    m_addIcon->loadSVG(addSvg);
    m_addIcon->setIconSize(16, 16);
    m_addIcon->setColor(m_textColor);
    
    m_copyIcon = std::make_shared<NomadUI::NUIIcon>();
    // Modern Duplicate (Offset layers)
    const char* copySvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></svg>)";
    m_copyIcon->loadSVG(copySvg);
    m_copyIcon->setIconSize(16, 16);
    m_copyIcon->setColor(m_textColor);
    
    m_trashIcon = std::make_shared<NomadUI::NUIIcon>();
    // Modern Trash (Lid separated)
    const char* trashSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/></svg>)";
    m_trashIcon->loadSVG(trashSvg);
    m_trashIcon->setIconSize(16, 16);
    m_trashIcon->setColor(themeManager.getColor("error").withAlpha(0.9f));
    
    m_midiIcon = std::make_shared<NomadUI::NUIIcon>();
    // Modern MIDI (Piano Roll / Note representation)
    const char* midiSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>)";
    m_midiIcon->loadSVG(midiSvg);
    m_midiIcon->setIconSize(16, 16);
    m_midiIcon->setColor(m_selectedColor);
    
    m_audioIcon = std::make_shared<NomadUI::NUIIcon>();
    // Modern Audio Waveform
    const char* audioSvg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12h3l3-6 4 12 4-8 3 4h3"/></svg>)";
    m_audioIcon->loadSVG(audioSvg);
    m_audioIcon->setIconSize(16, 16);
    m_audioIcon->setColor(m_selectedColor);
    
    // Create icon-based buttons (empty labels - we render icons on top)
    m_createButton = std::make_shared<NomadUI::NUIButton>("");
    m_createButton->setOnClick([this]() {
        if (m_trackManager) {
            MidiPayload payload;
            auto id = m_trackManager->getPatternManager().createMidiPattern("New Pattern", 4.0, payload);
            refreshPatterns();
            m_selectedPatternId = id;
            if (m_onPatternSelected) m_onPatternSelected(id);
        }
    });
    addChild(m_createButton);
    
    m_duplicateButton = std::make_shared<NomadUI::NUIButton>("");
    m_duplicateButton->setOnClick([this]() {
        if (m_trackManager && m_selectedPatternId.isValid()) {
            auto id = m_trackManager->getPatternManager().clonePattern(m_selectedPatternId);
            refreshPatterns();
            m_selectedPatternId = id;
            if (m_onPatternSelected) m_onPatternSelected(id);
        }
    });
    addChild(m_duplicateButton);
    
    addChild(m_duplicateButton);
    
    m_deleteButton = std::make_shared<NomadUI::NUIButton>("");
    m_deleteButton->setOnClick([this]() {
        if (m_trackManager && m_selectedPatternId.isValid()) {
            m_trackManager->getPatternManager().removePattern(m_selectedPatternId);
            m_selectedPatternId = PatternID();
            refreshPatterns();
        }
    });
    addChild(m_deleteButton);
    
    // Make toolbar buttons transparent to match professional DAW style
    // Use Icon style AND explicitly disable border/bg to override any defaults
    NomadUI::NUIColor transparent(0, 0, 0, 0);
    
    m_createButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_createButton->setBorderEnabled(false);
    m_createButton->setBackgroundColor(transparent);
    
    m_duplicateButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_duplicateButton->setBorderEnabled(false);
    m_duplicateButton->setBackgroundColor(transparent);

    m_deleteButton->setStyle(NomadUI::NUIButton::Style::Icon);
    m_deleteButton->setBorderEnabled(false);
    m_deleteButton->setBackgroundColor(transparent);

    // Force parent repaint on mouse move to ensure manual icon rendering updates instantly
    // NUIButton handles internal hover state, we just need to trigger a redraw of the panel.
    auto bindHover = [this](std::shared_ptr<NomadUI::NUIButton> btn) {
        btn->onMouseMove = [this](const NomadUI::NUIMouseEvent&) { repaint(); };
    };
    bindHover(m_createButton);
    bindHover(m_duplicateButton);
    bindHover(m_deleteButton);
    
    refreshPatterns();
}

void PatternBrowserPanel::refreshPatterns() {
    m_patterns.clear();
    
    if (!m_trackManager) return;
    
    auto allPatterns = m_trackManager->getPatternManager().getAllPatterns();
    for (const auto& p : allPatterns) {
        PatternEntry entry;
        entry.id = p->id;
        entry.name = p->name;
        entry.isMidi = p->isMidi();
        entry.lengthBeats = p->lengthBeats;
        entry.mixerChannel = p->getMixerChannel();
        m_patterns.push_back(entry);
    }
    
    repaint();
}

void PatternBrowserPanel::onResize(int width, int height) {
    NUIComponent::onResize(width, height);
    
    auto b = getBounds();
    float bw = 28.0f;  // Square icon buttons
    float bh = 24.0f;
    float spacing = 4.0f;
    float y = 4.0f;
    float x = b.x + spacing;
    
    m_createButton->setBounds(NomadUI::NUIRect(x, b.y + y, bw, bh));
    x += bw + spacing;
    m_duplicateButton->setBounds(NomadUI::NUIRect(x, b.y + y, bw, bh));
    x += bw + spacing;
    m_deleteButton->setBounds(NomadUI::NUIRect(x, b.y + y, bw, bh));
}

void PatternBrowserPanel::onRender(NomadUI::NUIRenderer& renderer) {
    auto b = getBounds();
    
    // Background - match file browser exactly
    renderer.fillRoundedRect(b, 8, m_backgroundColor);
    
    // Header
    renderHeader(renderer);
    
    // Pattern list
    renderPatternList(renderer);
    
    // Main border
    renderer.strokeRoundedRect(b, 8, 1, m_borderColor);
    
    // Inner black border for cleaner look (matching FileBrowser)
    NomadUI::NUIRect innerBounds(b.x + 1, b.y + 1, b.width - 2, b.height - 2);
    renderer.strokeRoundedRect(innerBounds, 7, 1, NomadUI::NUIColor(0.0f, 0.0f, 0.0f, 0.4f));
    
    NUIComponent::onRender(renderer);
}

void PatternBrowserPanel::renderHeader(NomadUI::NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    
    // Standard header background (Darker, reliable)
    NomadUI::NUIRect headerRect(b.x, b.y, b.width, m_headerHeight);
    renderer.fillRoundedRect(headerRect, 8, theme.getColor("backgroundSecondary").darkened(0.2f));
    
    // Bottom separator for header
    renderer.fillRect(NomadUI::NUIRect(b.x, b.y + m_headerHeight - 1, b.width, 1), 
                     theme.getColor("borderSubtle"));
    
    // Render icons on top of buttons
    // Render icons - Force center vertically in the header
    float iconSize = 14.0f;
    // Precisely center icon in the header (e.g. 40px height)
    float iconY = b.y + (m_headerHeight - iconSize) * 0.5f;
    
    // For X, center in the button's allotted width (assuming 28px width buttons)
    // Buttons are at specific X positions managed by layout, we can trust btnBounds.x
    // but we use the button width from bounds.
    
    if (m_createButton) {
        auto btnBounds = m_createButton->getBounds();
        float iconX = btnBounds.x + (btnBounds.width - iconSize) * 0.5f;
        m_addIcon->setBounds(NomadUI::NUIRect(iconX, iconY, iconSize, iconSize));
        // Boosted hover: Use very bright version of accent
        m_addIcon->setColor(m_createButton->isHovered() ? theme.getColor("accentPrimary").lightened(0.2f) : theme.getColor("textSecondary").withAlpha(0.8f));
        m_addIcon->onRender(renderer);
    }
    if (m_duplicateButton) {
        auto btnBounds = m_duplicateButton->getBounds();
        float iconX = btnBounds.x + (btnBounds.width - iconSize) * 0.5f;
        m_copyIcon->setBounds(NomadUI::NUIRect(iconX, iconY, iconSize, iconSize));
        m_copyIcon->setColor(m_duplicateButton->isHovered() ? theme.getColor("accentPrimary").lightened(0.2f) : theme.getColor("textSecondary").withAlpha(0.8f));
        m_copyIcon->onRender(renderer);
    }
    if (m_deleteButton) {
        auto btnBounds = m_deleteButton->getBounds();
        float iconX = btnBounds.x + (btnBounds.width - iconSize) * 0.5f;
        m_trashIcon->setBounds(NomadUI::NUIRect(iconX, iconY, iconSize, iconSize));
        // Delete button flashes bright solid red
        m_trashIcon->setColor(m_deleteButton->isHovered() ? theme.getColor("error").lightened(0.1f) : theme.getColor("error").withAlpha(0.5f));
        m_trashIcon->onRender(renderer);
    }
    
    // standard Title - Uppercase, 12px, vertically centered
    // Buttons end at: 4 + 28 + 4 + 28 + 4 + 28 = 96px relative to x.
    float titleX = b.x + 104.0f; 
    
    // Vertically center (Header 40, Font 12).
    // Use renderer's robust centering logic AND round to nearest pixel for sharp text
    // Borrowed exact math from FileBrowser::drawButton
    float titleY = std::round(renderer.calculateTextY(NomadUI::NUIRect(0, b.y, 0, m_headerHeight), 12.0f));
    
    renderer.drawText("PATTERNS", NomadUI::NUIPoint(titleX, titleY), 
                     12.0f, theme.getColor("textSecondary"));
}

void PatternBrowserPanel::renderPatternList(NomadUI::NUIRenderer& renderer) {
    auto b = getBounds();
    
    float listStartY = b.y + m_headerHeight;
    float listHeight = b.height - m_headerHeight;
    
    // Render list items (manual culling since no clipping available)
    float y = listStartY - m_scrollOffset;
    for (const auto& entry : m_patterns) {
        if (y + m_itemHeight > listStartY && y < listStartY + listHeight) {
            bool selected = (entry.id == m_selectedPatternId);
            bool hovered = (entry.id == m_hoveredPatternId);
            renderPatternItem(renderer, entry, y, selected, hovered);
        }
        y += m_itemHeight;
    }

    
    // Empty state
    if (m_patterns.empty()) {
        renderer.drawText("No patterns", 
                         NomadUI::NUIPoint(b.x + 10.0f, listStartY + 10.0f), 
                         11.0f, m_textColor.withAlpha(0.5f));
    }
}

void PatternBrowserPanel::renderPatternItem(NomadUI::NUIRenderer& renderer, 
                                            const PatternEntry& entry, float y, 
                                            bool selected, bool hovered) {
    auto b = getBounds();
    auto& theme = NomadUI::NUIThemeManager::getInstance();
    
    // Stretch full width, no padding gap
    NomadUI::NUIRect itemRect(b.x, y, b.width, m_itemHeight);
    
    // Background (Use standard theme colors)
    if (selected) {
        // === ACTIVE PATTERN: More obvious highlight ===
        // Stronger fill (40% alpha instead of 25%)
        renderer.fillRoundedRect(itemRect, 4, theme.getColor("primary").withAlpha(0.40f));
        // Thicker, brighter border (2px, 70% alpha)
        renderer.strokeRoundedRect(itemRect, 4, 2.0f, theme.getColor("primary").withAlpha(0.70f));
        // Left accent bar for extra visibility (4px wide)
        NomadUI::NUIRect accentBar(itemRect.x, itemRect.y + 2, 4.0f, itemRect.height - 4);
        renderer.fillRoundedRect(accentBar, 2, theme.getColor("primary"));
    } else if (hovered) {
        // Hover style
        renderer.fillRoundedRect(itemRect, 4, theme.getColor("hover").withAlpha(0.1f));
    }
    
    // Type icon using NUIIcon
    float iconX = itemRect.x + 8; // Standard indent
    float iconY = y + (m_itemHeight - 16) / 2;
    
    // Ensure icons use theme colors (white/secondary) unless selected
    NomadUI::NUIColor iconColor = selected ? theme.getColor("primary") : theme.getColor("textSecondary");
    m_midiIcon->setColor(iconColor);
    m_audioIcon->setColor(iconColor);

    if (entry.isMidi) {
        m_midiIcon->setBounds(NomadUI::NUIRect(iconX, iconY, 16, 16));
        m_midiIcon->onRender(renderer);
    } else {
        m_audioIcon->setBounds(NomadUI::NUIRect(iconX, iconY, 16, 16));
        m_audioIcon->onRender(renderer);
    }
    
    // Name (12px standard font)
    NomadUI::NUIColor textColor = selected ? theme.getColor("textPrimary") : theme.getColor("textSecondary");
    renderer.drawText(entry.name, NomadUI::NUIPoint(itemRect.x + 32, y + 9), 
                     12.0f, textColor); // Vertical center (32-12)/2 approx
    
    // Mixer routing indicator (if custom routing set)
    if (entry.mixerChannel >= 0) {
        std::string routeStr = ">" + std::to_string(entry.mixerChannel + 1);  // 1-based for display
        float routeX = itemRect.x + itemRect.width - 60;
        renderer.drawText(routeStr, NomadUI::NUIPoint(routeX, y + 9), 
                         11.0f, theme.getColor("accentCyan"));  // Use theme accent
    }
    
    // Length (right aligned)
    std::string lengthStr = std::to_string(static_cast<int>(entry.lengthBeats)) + "b";
    renderer.drawText(lengthStr, NomadUI::NUIPoint(itemRect.x + itemRect.width - 25, y + 9), 
                     11.0f, theme.getColor("textDisabled"));
}

bool PatternBrowserPanel::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    auto b = getBounds();
    auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
    
    // 1. Handle active drag updates
    // 1. Handle active drag updates - DELEGATED TO GLOBAL MAIN LOOP
    // We only need to return true if dragging to prevent other interactions
    if (dragManager.isDragging()) {
        return true; 
    }
    
    // Check if in list area
    // Robust check: Compare event Y relative to panel Y
    float relativeY = event.position.y - b.y;
    bool inListArea = relativeY > m_headerHeight;
    
    // Ensure we are horizontally within bounds too
    if (event.position.x < b.x || event.position.x > b.x + b.width) {
        inListArea = false; 
    }
    
    if (inListArea) {
        // Find hovered item
        float listScrollY = relativeY - m_headerHeight + m_scrollOffset;
        int itemIndex = static_cast<int>(listScrollY / m_itemHeight);
        
        if (itemIndex >= 0 && itemIndex < static_cast<int>(m_patterns.size())) {
            m_hoveredPatternId = m_patterns[itemIndex].id;
            
            // Mouse Press - Initialize Drag / Select
            if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
                // Get current time for double-click detection
                auto now = std::chrono::steady_clock::now();
                double currentTime = std::chrono::duration<double>(now.time_since_epoch()).count();
                
                // Double-click check
                bool isDoubleClick = (m_lastClickedPatternId == m_patterns[itemIndex].id) &&
                                    (currentTime - m_lastClickTime < 0.4);
                
                m_selectedPatternId = m_patterns[itemIndex].id;
                if (m_onPatternSelected) m_onPatternSelected(m_selectedPatternId);
                
                if (isDoubleClick) {
                    // Double-click: open pattern in editor
                    if (m_onPatternDoubleClick) m_onPatternDoubleClick(m_selectedPatternId);
                    m_dragPotential = false; // Cancel drag on double click
                } else {
                    // Single click: Potential drag start
                    m_dragPotential = true;
                    m_dragStartPos = event.position;
                    m_dragPatternId = m_selectedPatternId;
                }
                
                m_lastClickTime = currentTime;
                m_lastClickedPatternId = m_patterns[itemIndex].id;
                
                repaint();
                return true;
            }
        } else {
            m_hoveredPatternId = PatternID();
        }
        
        // Drag Initiation Check
        if (m_dragPotential && itemIndex >= 0 && itemIndex < static_cast<int>(m_patterns.size())) {
             float dx = event.position.x - m_dragStartPos.x;
             float dy = event.position.y - m_dragStartPos.y;
             float dist = std::sqrt(dx * dx + dy * dy);
             
             if (dist >= dragManager.getDragThreshold()) {
                 // Start Drag!
                 NomadUI::DragData dragData;
                 dragData.type = NomadUI::DragDataType::Pattern;
                 
                 // Find pattern name and data
                 for(const auto& p : m_patterns) {
                     if(p.id == m_dragPatternId) {
                         dragData.displayName = p.name;
                         break;
                     }
                 }
                 
                 // Pass PatternID value as string (safest) or customData
                 dragData.customData = m_dragPatternId;
                 dragData.previewWidth = 120.0f;
                 dragData.previewHeight = m_itemHeight;
                 dragData.accentColor = m_selectedColor;
                 
                 dragManager.beginDrag(dragData, m_dragStartPos, this);
                 m_isDragging = true;
                 m_dragPotential = false;
                 
                 // Legacy callback (optional)
                 if (m_onPatternDragStart) m_onPatternDragStart(m_dragPatternId);
                 
                 return true;
             }
        }
    }
    
    // Mouse Release
    if (!event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
        m_dragPotential = false;
    }
    
    repaint();
    return NUIComponent::onMouseEvent(event);
}

} // namespace Audio
} // namespace Nomad
