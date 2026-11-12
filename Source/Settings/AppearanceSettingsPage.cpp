// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AppearanceSettingsPage.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../Core/Preferences.h"

#include <string>
#include <vector>

namespace Aestra {

namespace {
/// The twelve Aestra Kit variants (FD-79), in the kit's order: Ember first, as the default.
const std::vector<AestraUI::NUIThemePresets::KitTheme>& themeList() { return AestraUI::NUIThemePresets::kitThemes(); }

/// Older preference values name Ember by its earlier keys.
std::string canonicalTheme(const std::string& name) {
    if (name == "Aestra-dark" || name == "aestra-dark") return "ember-dark";
    if (name == "Aestra-light") return "ember-light";
    if (name == "high-contrast-dark") return "ember-hc";
    return name;
}

std::string themeLabel(const AestraUI::NUIThemePresets::KitTheme& kit) {
    std::string mode = kit.modeName;
    if (!mode.empty()) mode[0] = static_cast<char>(mode[0] - 'A' + 'a');
    std::string label = std::string(kit.name) + " " + mode;
    if (std::string(kit.key) == "ember-dark") label += " (default)";
    return label;
}
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
    const auto& themes = themeList();
    for (size_t i = 0; i < themes.size(); ++i)
        m_themeDropdown->addItem(themeLabel(themes[i]), static_cast<int>(i));
    m_themeDropdown->setOnSelectionChanged([this](int index) {
        const auto& list = themeList();
        if (!m_syncingSelection && index >= 0 && index < static_cast<int>(list.size()))
            selectTheme(list[static_cast<size_t>(index)].key);
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
    const std::string key = canonicalTheme(themeName);
    const auto& list = themeList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (key == list[i].key)
            return static_cast<int>(i);
    }
    return 0;
}

void AppearanceSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    m_form.paint(renderer);
    renderChildren(renderer);
    if (m_themeDropdown && m_themeDropdown->isOpen()) {
        m_themeDropdown->renderDropdownList(renderer);
    }
}

void AppearanceSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

void AppearanceSettingsPage::layoutComponents() {
    m_themeLabel->setVisible(false);
    m_form.clear();
    m_form.section("THEME");
    m_form.row("Colours", m_themeDropdown);
    m_form.layout(getBounds());
}

} // namespace Aestra
