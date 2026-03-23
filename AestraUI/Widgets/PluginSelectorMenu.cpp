// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginSelectorMenu.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include <algorithm>

namespace AestraUI {

PluginSelectorMenu::PluginSelectorMenu() {
    setId("PluginSelectorMenu");
    m_isSearchActive = true;
}


void PluginSelectorMenu::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // Background and border (Deep Void / Menu Style)
    renderer.fillRect(b, theme.getColor("dropdown.background")); // Dark Glass
    renderer.strokeRect(b, 1.0f, theme.getColor("border"));

    // --- Search Bar Header ---
    NUIRect headerRect = {b.x, b.y, b.width, HEADER_H};
    renderer.fillRect(headerRect, theme.getColor("surfaceRaised")); // Slightly lighter for header
    
    // Search Box Background
    NUIRect searchBox = {b.x + 4, b.y + 4, b.width - 8, HEADER_H - 8};
    renderer.fillRoundedRect(searchBox, 4.0f, theme.getColor("inputBackground"));
    
    if (m_isSearchActive) {
        renderer.strokeRoundedRect(searchBox, 4.0f, 1.0f, theme.getColor("accentPrimary"));
    }

    // Search Text
    std::string displayStr = m_searchQuery;
    bool showCaret = m_isSearchActive && isFocused(); // Strict focus check for caret
    
    // DEBUG: Force visible color to rule out theme issues
    NUIColor debugTextColor = theme.getColor("textPrimary");
    
    if (displayStr.empty() && !m_isSearchActive) {
        renderer.drawText("Search...", {searchBox.x + 6, searchBox.y + 4}, 11.0f, theme.getColor("textDisabled"));
    } else {
        renderer.drawText(displayStr + (showCaret ? "|" : ""), {searchBox.x + 6, searchBox.y + 4}, 11.0f, debugTextColor);
    }

    // --- Plugin List ---
    const NUIColor textColor = theme.getColor("textPrimary");
    const NUIColor hoverBg = theme.getColor("accentPrimary").withAlpha(0.2f);
    
    // Draw Separator
    renderer.drawLine({b.x, b.y + HEADER_H}, {b.x + b.width, b.y + HEADER_H}, 1.0f, theme.getColor("border"));

    float y = b.y + HEADER_H;
    
    // Render filtered list
    for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
        NUIRect itemRect = {b.x, y, b.width, ITEM_H};
        
        // Clip check
        if (y + ITEM_H > b.bottom()) break;

        if (m_hoveredIndex == static_cast<int>(i)) {
            renderer.fillRect(itemRect, hoverBg);
        }

        renderer.drawText(m_filteredPlugins[i].name, {itemRect.x + 8.0f, itemRect.y + 6.0f}, 11.0f, textColor);
        y += ITEM_H;
    }
    
    if (m_filteredPlugins.empty()) {
       renderer.drawText("No results", {b.x + 8.0f, y + 6.0f}, 11.0f, theme.getColor("textDisabled"));
    }
}

bool PluginSelectorMenu::onMouseEvent(const NUIMouseEvent& event) {
    auto b = getBounds();
    
    // Close if clicked outside
    if (event.pressed && !b.contains(event.position)) {
        if (m_onClosed) m_onClosed();
        return true;
    }

    if (b.contains(event.position)) {
        if (event.pressed) setFocused(true);
        float relY = event.position.y - b.y;

        // Header Interaction
        if (relY < HEADER_H) {
            if (event.pressed) {
                 m_isSearchActive = true;
                 setFocused(true);
                 repaint();
            }
            return true;
        }

        // List Interaction
        int index = static_cast<int>((relY - HEADER_H) / ITEM_H);
        
        if (index >= 0 && index < static_cast<int>(m_filteredPlugins.size())) {
            if (m_hoveredIndex != index) {
                m_hoveredIndex = index;
                repaint();
            }

            if (event.pressed && event.button == NUIMouseButton::Left) {
                if (m_onPluginSelected) {
                    m_onPluginSelected(m_filteredPlugins[index].id);
                }
                return true;
            }
        } else if (m_hoveredIndex != -1) {
            m_hoveredIndex = -1;
            repaint();
        }
        return true;
    }

    return false;
}

bool PluginSelectorMenu::onKeyEvent(const NUIKeyEvent& event) {
    if (m_isSearchActive && event.pressed) {
        bool handled = false;

        // Text Input
        if (event.keyCode == NUIKeyCode::Backspace) {
            if (!m_searchQuery.empty()) {
                m_searchQuery.pop_back();
                updateFilter();
            }
            handled = true;
        } 
        else if (event.keyCode == NUIKeyCode::Enter) {
            // Select first filtered or hovered
            int pickIdx = (m_hoveredIndex != -1) ? m_hoveredIndex : 0;
            if (pickIdx >= 0 && pickIdx < static_cast<int>(m_filteredPlugins.size())) {
                if (m_onPluginSelected) m_onPluginSelected(m_filteredPlugins[pickIdx].id);
            }
            handled = true;
        }
        else if (event.keyCode == NUIKeyCode::Up) {
            m_hoveredIndex = std::max(0, m_hoveredIndex - 1);
            repaint();
            handled = true;
        }
        else if (event.keyCode == NUIKeyCode::Down) {
            m_hoveredIndex = std::min(static_cast<int>(m_filteredPlugins.size()) - 1, m_hoveredIndex + 1);
            repaint();
            handled = true;
        }
        else {
             char c = event.character;
             // Fallback: If character is 0 but we have a valid key code (A-Z, 0-9, Space)
             if (c == 0) {
                 if (event.keyCode >= NUIKeyCode::A && event.keyCode <= NUIKeyCode::Z) {
                      c = 'a' + (static_cast<int>(event.keyCode) - static_cast<int>(NUIKeyCode::A));
                      bool shift = (event.modifiers & NUIModifiers::Shift);
                      bool caps = (event.modifiers & NUIModifiers::CapsLock);
                      if (shift != caps) c = std::toupper(c);
                 }
                 else if (event.keyCode >= NUIKeyCode::Num0 && event.keyCode <= NUIKeyCode::Num9) {
                      c = '0' + (static_cast<int>(event.keyCode) - static_cast<int>(NUIKeyCode::Num0));
                 }
                 else if (event.keyCode == NUIKeyCode::Space) {
                      c = ' ';
                 }
             }

             if (c >= 32 && c <= 126) {
                m_searchQuery += (char)c;
                updateFilter();
                handled = true;
             }
        }
        if (handled) {
            repaint();
            return true;
        }
    }
    return false;
}

void PluginSelectorMenu::setPlugins(const std::vector<PluginListItem>& plugins) {
    m_allPlugins = plugins;
    
    // Sort by name initially
    std::sort(m_allPlugins.begin(), m_allPlugins.end(), [](const PluginListItem& a, const PluginListItem& b) {
        return a.name < b.name;
    });

    updateFilter();
}

void PluginSelectorMenu::updateFilter() {
    if (m_searchQuery.empty()) {
        m_filteredPlugins = m_allPlugins;
    } else {
        m_filteredPlugins.clear();
        std::string queryLower = m_searchQuery;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
        
        for (const auto& p : m_allPlugins) {
             std::string nameLower = p.name;
             std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
             
             if (nameLower.find(queryLower) != std::string::npos) {
                 m_filteredPlugins.push_back(p);
             }
        }
    }
    
    m_hoveredIndex = m_filteredPlugins.empty() ? -1 : 0; // Auto-select first result

    // Adjust size based on filtered content + Header
    float listH = m_filteredPlugins.empty() ? ITEM_H : (m_filteredPlugins.size() * ITEM_H);
    float h = std::min(MAX_H, listH + HEADER_H);
    setSize(200.0f, h + 4.0f); // + padding
    repaint();
}

void PluginSelectorMenu::setOnPluginSelected(std::function<void(const std::string& id)> callback) {
    m_onPluginSelected = std::move(callback);
}

void PluginSelectorMenu::setOnClosed(std::function<void()> callback) {
    m_onClosed = std::move(callback);
}

} // namespace AestraUI
