// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginSelectorMenu.h"
#include "NUIThemeSystem.h"
#include "NUIRenderer.h"
#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kMenuWidth = 292.0f;
constexpr float kListInset = 6.0f;
constexpr float kRowSpacing = 4.0f;
constexpr float kItemHeight = 24.0f;
constexpr float kRowPitch = kItemHeight + 10.0f;
}

PluginSelectorMenu::PluginSelectorMenu() {
    setId("PluginSelectorMenu");
    m_isSearchActive = true;
}


void PluginSelectorMenu::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    renderer.fillRoundedRect(b, 12.0f, NUIColor(0.08f, 0.09f, 0.13f, 0.985f));
    renderer.strokeRoundedRect(b, 12.0f, 1.0f, theme.getColor("border").withAlpha(0.85f));

    NUIRect headerRect = {b.x, b.y, b.width, HEADER_H};
    renderer.fillRoundedRect({headerRect.x + 1.0f, headerRect.y + 1.0f, headerRect.width - 2.0f, headerRect.height},
                             11.0f, NUIColor(0.17f, 0.19f, 0.27f, 0.92f));

    NUIRect searchBox = {b.x + 8, b.y + 6, b.width - 16, HEADER_H - 12};
    renderer.fillRoundedRect(searchBox, 7.0f, theme.getColor("inputBackground"));
    if (m_isSearchActive) {
        renderer.strokeRoundedRect(searchBox, 7.0f, 1.0f, theme.getColor("accentPrimary").withAlpha(0.8f));
    } else {
        renderer.strokeRoundedRect(searchBox, 7.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.06f));
    }

    std::string displayStr = m_searchQuery;
    bool showCaret = m_isSearchActive && isFocused();
    
    if (displayStr.empty() && !m_isSearchActive) {
        renderer.drawText("Search plugins...", {searchBox.x + 8, searchBox.y + 5}, 11.0f, theme.getColor("textDisabled"));
    } else {
        renderer.drawText(displayStr + (showCaret ? "|" : ""), {searchBox.x + 8, searchBox.y + 5}, 11.0f,
                          theme.getColor("textPrimary"));
    }

    const NUIColor textColor = theme.getColor("textPrimary");
    const NUIColor hoverBg = theme.getColor("accentPrimary").withAlpha(0.16f);
    const NUIColor rowBg = NUIColor(0.11f, 0.12f, 0.17f, 0.74f);
    const NUIColor metaColor = theme.getColor("textSecondary").withAlpha(0.78f);
    renderer.drawLine({b.x + 8.0f, b.y + HEADER_H}, {b.right() - 8.0f, b.y + HEADER_H}, 1.0f,
                      theme.getColor("border").withAlpha(0.55f));

    float y = b.y + HEADER_H + 6.0f;
    
    for (size_t i = 0; i < m_filteredPlugins.size(); ++i) {
        NUIRect itemRect = {b.x + kListInset, y, b.width - kListInset * 2.0f, ITEM_H + 6.0f};
        if (y + itemRect.height > b.bottom() - 6.0f) break;

        renderer.fillRoundedRect(itemRect, 8.0f, m_hoveredIndex == static_cast<int>(i) ? hoverBg : rowBg);
        if (m_hoveredIndex == static_cast<int>(i)) {
            renderer.strokeRoundedRect(itemRect, 8.0f, 1.0f, theme.getColor("accentPrimary").withAlpha(0.34f));
        } else {
            renderer.strokeRoundedRect(itemRect, 8.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.04f));
        }

        renderer.drawText(m_filteredPlugins[i].name, {itemRect.x + 10.0f, itemRect.y + 6.0f}, 11.0f, textColor);
        std::string meta = m_filteredPlugins[i].vendor;
        if (!m_filteredPlugins[i].formatStr.empty()) {
            meta += meta.empty() ? m_filteredPlugins[i].formatStr : " • " + m_filteredPlugins[i].formatStr;
        }
        renderer.drawText(meta, {itemRect.x + 10.0f, itemRect.y + 19.0f}, 8.5f, metaColor);
        y += itemRect.height + kRowSpacing;
    }
    
    if (m_filteredPlugins.empty()) {
       renderer.drawText("No matching plugins", {b.x + 12.0f, y + 8.0f}, 11.0f, theme.getColor("textDisabled"));
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
        float listY = relY - HEADER_H - 6.0f;
        int index = listY >= 0.0f ? static_cast<int>(listY / kRowPitch) : -1;
        
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
    float listH = m_filteredPlugins.empty() ? (ITEM_H + 12.0f) : (m_filteredPlugins.size() * kRowPitch);
    float h = std::min(MAX_H, listH + HEADER_H + 12.0f);
    setSize(kMenuWidth, h + 8.0f);
    repaint();
}

void PluginSelectorMenu::setOnPluginSelected(std::function<void(const std::string& id)> callback) {
    m_onPluginSelected = std::move(callback);
}

void PluginSelectorMenu::setOnClosed(std::function<void()> callback) {
    m_onClosed = std::move(callback);
}

} // namespace AestraUI
