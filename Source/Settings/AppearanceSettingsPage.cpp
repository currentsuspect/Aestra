// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AppearanceSettingsPage.h"

namespace Nomad {

AppearanceSettingsPage::AppearanceSettingsPage() {
    createUI();
}

void AppearanceSettingsPage::createUI() {
    m_themeLabel = std::make_shared<NomadUI::NUILabel>();
    m_themeLabel->setText("UI Theme:");
    addChild(m_themeLabel);

    m_themeDropdown = std::make_shared<NomadUI::NUIDropdown>();
    m_themeDropdown->addItem("Nomad Dark (Default)", 0);
    m_themeDropdown->addItem("Nomad Light", 1);
    m_themeDropdown->addItem("Midnight Blue", 2);
    m_themeDropdown->addItem("High Contrast", 3);
    m_themeDropdown->setSelectedIndex(0);
    addChild(m_themeDropdown);
}

void AppearanceSettingsPage::onRender(NomadUI::NUIRenderer& renderer) {
    renderChildren(renderer);
}

void AppearanceSettingsPage::onResize(int width, int height) {
    NomadUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

void AppearanceSettingsPage::layoutComponents() {
    auto b = getBounds();
    float padding = 20.0f;
    float rowHeight = 30.0f;
    float x = b.x + padding;
    float y = b.y + padding;
    
    m_themeLabel->setBounds(NomadUI::NUIRect(x, y, 100, rowHeight));
    m_themeDropdown->setBounds(NomadUI::NUIRect(x + 110, y, 200, rowHeight));
}

} // namespace Nomad
