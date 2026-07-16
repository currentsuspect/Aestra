// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AppearanceSettingsPage.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../Core/Preferences.h"

#include <array>

namespace Aestra {

namespace {
constexpr std::array<const char*, 3> kThemeNames{"Aestra-dark", "Aestra-light", "high-contrast-dark"};
}

AppearanceSettingsPage::AppearanceSettingsPage() {
    createUI();
}

void AppearanceSettingsPage::createUI() {
    m_themeLabel = std::make_shared<AestraUI::NUILabel>();
    m_themeLabel->setText("UI Theme:");
    m_themeLabel->setFontSize(AestraUI::NUIThemeManager::getInstance().getFontSize("m"));
    addChild(m_themeLabel);

    m_themeDropdown = std::make_shared<AestraUI::NUIDropdown>();
    m_themeDropdown->addItem("Aestra Dark (Default)", 0);
    m_themeDropdown->addItem("Aestra Light", 1);
    m_themeDropdown->addItem("High Contrast Dark", 2);
    m_themeDropdown->setOnSelectionChanged([this](int index) {
        if (!m_syncingSelection && index >= 0 && index < static_cast<int>(kThemeNames.size()))
            selectTheme(kThemeNames[static_cast<size_t>(index)]);
    });
    addChild(m_themeDropdown);

    onShow();
}

void AppearanceSettingsPage::onShow() {
    auto& manager = AestraUI::NUIThemeManager::getInstance();
    m_originalTheme = manager.getActiveTheme();
    m_pendingTheme = m_originalTheme;
    m_dirty = false;
    m_syncingSelection = true;
    m_themeDropdown->setSelectedIndex(indexForTheme(m_pendingTheme));
    m_syncingSelection = false;
}

void AppearanceSettingsPage::selectTheme(const std::string& themeName) {
    auto& manager = AestraUI::NUIThemeManager::getInstance();
    if (!manager.setActiveTheme(themeName))
        return;
    m_pendingTheme = themeName;
    m_dirty = m_pendingTheme != m_originalTheme;
}

void AppearanceSettingsPage::applyChanges() {
    auto& preferences = Preferences::instance();
    preferences.theme = m_pendingTheme;
    preferences.save();
    m_originalTheme = m_pendingTheme;
    m_dirty = false;
}

void AppearanceSettingsPage::cancelChanges() {
    AestraUI::NUIThemeManager::getInstance().setActiveTheme(m_originalTheme);
    m_pendingTheme = m_originalTheme;
    m_syncingSelection = true;
    m_themeDropdown->setSelectedIndex(indexForTheme(m_pendingTheme));
    m_syncingSelection = false;
    m_dirty = false;
}

int AppearanceSettingsPage::indexForTheme(const std::string& themeName) const {
    for (size_t i = 0; i < kThemeNames.size(); ++i) {
        if (themeName == kThemeNames[i])
            return static_cast<int>(i);
    }
    return 0;
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
    const auto& theme = AestraUI::NUIThemeManager::getInstance().getCurrentTheme();
    float padding = theme.spacingM;
    float rowHeight = theme.layout.standardControlHeight;
    float x = b.x + padding;
    float y = b.y + padding;
    
    m_themeLabel->setBounds(AestraUI::NUIRect(x, y, 100, rowHeight));
    m_themeDropdown->setBounds(AestraUI::NUIRect(x + 110, y, 200, rowHeight));
}

} // namespace Aestra
