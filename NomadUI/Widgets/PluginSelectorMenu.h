// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "PluginBrowserPanel.h"
#include <vector>
#include <functional>
#include <string>

namespace NomadUI {

/**
 * @brief A simple vertical list menu for selecting plugins.
 */
class PluginSelectorMenu : public NUIComponent {
public:
    PluginSelectorMenu();
    ~PluginSelectorMenu() override = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setPlugins(const std::vector<PluginListItem>& plugins);
    void setOnPluginSelected(std::function<void(const std::string& id)> callback);
    void setOnClosed(std::function<void()> callback);

private:
    std::vector<PluginListItem> m_plugins;
    std::function<void(const std::string& id)> m_onPluginSelected;
    std::function<void()> m_onClosed;

    int m_hoveredIndex = -1;
    static constexpr float ITEM_H = 24.0f;
    static constexpr float MAX_H = 300.0f;
};

} // namespace NomadUI
