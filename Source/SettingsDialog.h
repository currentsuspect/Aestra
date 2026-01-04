// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Widgets/NUIButton.h"
#include "../NomadUI/Core/NUILabel.h"
#include "ISettingsPage.h"
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace Nomad {

class SettingsDialog : public NomadUI::NUIComponent {
public:
    SettingsDialog();
    ~SettingsDialog() override = default;

    // Window management
    void show();
    void hide();
    bool isVisible() const { return m_visible; }

    // Page management
    void addPage(std::shared_ptr<ISettingsPage> page);
    void setActivePage(const std::string& pageID);

    const std::string& getActivePageID() const { return m_activePageId; }

    // NUIComponent overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const NomadUI::NUIKeyEvent& event) override;
    
    // Callbacks
    void setOnClose(std::function<void()> callback) { m_onClose = callback; }

private:
    void createUI();
    void layoutComponents();
    void updateSidebar();

    bool m_visible;
    NomadUI::NUIRect m_dialogBounds;
    NomadUI::NUIRect m_sidebarBounds;
    NomadUI::NUIRect m_contentBounds;
    NomadUI::NUIRect m_closeButtonBounds; 
    bool m_closeButtonHovered;

    // Sidebar
    struct SidebarItem {
        std::string id;
        std::string title;
        bool hovered;
        bool active;
        NomadUI::NUIRect bounds;
    };
    std::vector<SidebarItem> m_sidebarItems;
    std::map<std::string, std::shared_ptr<ISettingsPage>> m_pages;
    std::shared_ptr<ISettingsPage> m_activePage;

    std::string m_activePageId;

    // Footer buttons
    std::shared_ptr<NomadUI::NUIButton> m_applyButton;
    std::shared_ptr<NomadUI::NUIButton> m_cancelButton;
    std::shared_ptr<NomadUI::NUIButton> m_okButton;

    // State
    std::function<void()> m_onClose;
    float m_blinkAnimation; // For clicking outside
};

} // namespace Nomad
