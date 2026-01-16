// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIButton.h"
#include "NUILabel.h"
#include "ISettingsPage.h"
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace Aestra {

class SettingsDialog : public AestraUI::NUIComponent {
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
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    bool onKeyEvent(const AestraUI::NUIKeyEvent& event) override;
    
    // Callbacks
    void setOnClose(std::function<void()> callback) { m_onClose = callback; }

private:
    void createUI();
    void layoutComponents();
    void updateSidebar();

    bool m_visible;
    AestraUI::NUIRect m_dialogBounds;
    AestraUI::NUIRect m_sidebarBounds;
    AestraUI::NUIRect m_contentBounds;
    AestraUI::NUIRect m_closeButtonBounds; 
    bool m_closeButtonHovered;

    // Sidebar
    struct SidebarItem {
        std::string id;
        std::string title;
        bool hovered;
        bool active;
        AestraUI::NUIRect bounds;
    };
    std::vector<SidebarItem> m_sidebarItems;
    std::map<std::string, std::shared_ptr<ISettingsPage>> m_pages;
    std::shared_ptr<ISettingsPage> m_activePage;

    std::string m_activePageId;

    // Footer buttons
    std::shared_ptr<AestraUI::NUIButton> m_applyButton;
    std::shared_ptr<AestraUI::NUIButton> m_cancelButton;
    std::shared_ptr<AestraUI::NUIButton> m_okButton;

    // State
    std::function<void()> m_onClose;
    float m_blinkAnimation; // For clicking outside
};

} // namespace Aestra
