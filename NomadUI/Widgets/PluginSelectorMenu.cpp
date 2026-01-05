// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginSelectorMenu.h"
#include "../Core/NUIThemeSystem.h"
#include "../Graphics/NUIRenderer.h"
#include <algorithm>

namespace NomadUI {

PluginSelectorMenu::PluginSelectorMenu() {
    setId("PluginSelectorMenu");
}

void PluginSelectorMenu::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // Background and border
    renderer.fillRect(b, theme.getColor("surfaceSecondary"));
    renderer.strokeRect(b, 1.0f, theme.getColor("border"));

    const NUIColor textColor = theme.getColor("textPrimary");
    const NUIColor hoverBg = theme.getColor("accentPrimary").withAlpha(0.2f);

    float y = b.y;
    for (size_t i = 0; i < m_plugins.size(); ++i) {
        NUIRect itemRect = {b.x, y, b.width, ITEM_H};
        
        if (m_hoveredIndex == static_cast<int>(i)) {
            renderer.fillRect(itemRect, hoverBg);
        }

        renderer.drawText(m_plugins[i].name, {itemRect.x + 8.0f, itemRect.y + 6.0f}, 11.0f, textColor);
        y += ITEM_H;
        
        if (y + ITEM_H > b.bottom()) break;
    }
}

bool PluginSelectorMenu::onMouseEvent(const NUIMouseEvent& event) {
    const auto b = getBounds();
    
    // Close if clicked outside
    if (event.pressed && !b.contains(event.position)) {
        if (m_onClosed) m_onClosed();
        return true;
    }

    if (b.contains(event.position)) {
        int index = static_cast<int>((event.position.y - b.y) / ITEM_H);
        if (index >= 0 && index < static_cast<int>(m_plugins.size())) {
            if (m_hoveredIndex != index) {
                m_hoveredIndex = index;
                repaint();
            }

            if (event.pressed && event.button == NUIMouseButton::Left) {
                if (m_onPluginSelected) {
                    m_onPluginSelected(m_plugins[index].id);
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

void PluginSelectorMenu::setPlugins(const std::vector<PluginListItem>& plugins) {
    m_plugins = plugins;
    
    // Sort by name
    std::sort(m_plugins.begin(), m_plugins.end(), [](const PluginListItem& a, const PluginListItem& b) {
        return a.name < b.name;
    });

    // Adjust size based on content
    float h = std::min(MAX_H, m_plugins.size() * ITEM_H);
    setSize(200.0f, h);
    repaint();
}

void PluginSelectorMenu::setOnPluginSelected(std::function<void(const std::string& id)> callback) {
    m_onPluginSelected = std::move(callback);
}

void PluginSelectorMenu::setOnClosed(std::function<void()> callback) {
    m_onClosed = std::move(callback);
}

} // namespace NomadUI
