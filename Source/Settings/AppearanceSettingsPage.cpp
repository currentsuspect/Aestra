// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AppearanceSettingsPage.h"

namespace Aestra {

AppearanceSettingsPage::AppearanceSettingsPage() {
    createUI();
}

void AppearanceSettingsPage::createUI() {
    m_themeLabel = std::make_shared<AestraUI::NUILabel>();
    m_themeLabel->setText("UI Theme:");
    addChild(m_themeLabel);

    m_themeDropdown = std::make_shared<AestraUI::NUIDropdown>();
    m_themeDropdown->addItem("Aestra Dark (Default)", 0);
    m_themeDropdown->addItem("Aestra Light", 1);
    m_themeDropdown->addItem("Midnight Blue", 2);
    m_themeDropdown->addItem("High Contrast", 3);
    m_themeDropdown->setSelectedIndex(0);
    addChild(m_themeDropdown);
}

void AppearanceSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    renderChildren(renderer);
}

void AppearanceSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

void AppearanceSettingsPage::layoutComponents() {
    auto b = getBounds();
    float padding = 20.0f;
    float rowHeight = 30.0f;
    float x = b.x + padding;
    float y = b.y + padding;
    
    m_themeLabel->setBounds(AestraUI::NUIRect(x, y, 100, rowHeight));
    m_themeDropdown->setBounds(AestraUI::NUIRect(x + 110, y, 200, rowHeight));
}

} // namespace Aestra
