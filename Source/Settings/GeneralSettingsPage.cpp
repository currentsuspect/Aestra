// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "GeneralSettingsPage.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../Core/Preferences.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace Aestra {

namespace {
// The folder the library opens at (Documents/Aestra, as FileBrowser makes it), with the home folder shown as ~.
std::string aestraFolderForDisplay() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home) return "Documents/Aestra";
    const std::string full = (std::filesystem::path(home) / "Documents" / "Aestra").string();
#if defined(_WIN32)
    return full;
#else
    return "~/Documents/Aestra";
#endif
}
} // namespace

GeneralSettingsPage::GeneralSettingsPage() {
    createUI();
}

void GeneralSettingsPage::createUI() {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_projectsPathLabel = std::make_shared<AestraUI::NUILabel>();
    m_projectsPathLabel->setText("Aestra folder");
    m_projectsPathLabel->setFontSize(theme.getFontSize("m"));
    addChild(m_projectsPathLabel);

    m_projectsPathInput = std::make_shared<AestraUI::NUITextInput>();
    // Shows where the library lives. Choosing another folder is not built yet, so the field is read-only and
    // the Browse button stays hidden rather than doing nothing.
    m_projectsPathInput->setText(aestraFolderForDisplay());
    m_projectsPathInput->setReadOnly(true);
    addChild(m_projectsPathInput);

    m_browseButton = std::make_shared<AestraUI::NUIButton>();
    m_browseButton->setText("Browse");
    m_browseButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_browseButton->setVisible(false);
    addChild(m_browseButton);
    
    m_autoSaveLabel = std::make_shared<AestraUI::NUILabel>();
    m_autoSaveLabel->setText("Auto-Save:");
    m_autoSaveLabel->setFontSize(theme.getFontSize("m"));
    addChild(m_autoSaveLabel);
    
    // A switch, set from the stored preference (it used to start "on" whatever was saved).
    m_autoSaveToggle = std::make_shared<AestraUI::NUIToggle>();
    m_autoSaveToggle->setAnimated(false);
    m_autoSaveToggle->setOn(Preferences::instance().autoSaveEnabled);
    m_autoSaveToggle->setOnToggle([this](bool enabled) {
        m_dirty = true;
        syncAutoSaveLabel();
        if (m_onAutoSaveToggled) m_onAutoSaveToggled(enabled);
    });
    m_autoSaveDetail = std::make_shared<AestraUI::NUILabel>();
    m_autoSaveDetail->setFontSize(theme.getFontSize("m"));
    m_autoSaveDetail->setTextColor(theme.getColor("textSecondary"));
    addChild(m_autoSaveDetail);
    syncAutoSaveLabel();
    addChild(m_autoSaveToggle);
}

void GeneralSettingsPage::syncAutoSaveLabel() {
    if (!m_autoSaveToggle || !m_autoSaveDetail) return;
    if (!m_autoSaveToggle->isOn()) {
        m_autoSaveDetail->setText("Off");
        m_form.setHint(m_autoSaveToggle.get(), "Off");
        return;
    }
    const int seconds = std::clamp(Preferences::instance().autoSaveIntervalSeconds, 10, 3600);
    m_autoSaveDetail->setText(seconds % 60 == 0 ? "Every " + std::to_string(seconds / 60) + (seconds == 60 ? " minute" : " minutes")
                                                : "Every " + std::to_string(seconds) + " seconds");
    m_form.setHint(m_autoSaveToggle.get(), m_autoSaveDetail->getText());
}

void GeneralSettingsPage::applyChanges() {
    m_dirty = false;
}

void GeneralSettingsPage::cancelChanges() {
    m_dirty = false;
}

void GeneralSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    m_form.paint(renderer);
    renderer.setClipRect(m_form.area());
    renderChildren(renderer);
    renderer.clearClipRect();
}

void GeneralSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height); // Store bounds
    layoutComponents();
}

void GeneralSettingsPage::layoutComponents() {
    m_projectsPathLabel->setVisible(false);
    m_autoSaveLabel->setVisible(false);
    m_autoSaveDetail->setVisible(false); // the Auto-save row's hint
    constexpr float kPathWidth = 300.0f;

    m_form.clear();
    m_form.section("PROJECTS");
    m_form.row("Aestra folder", m_projectsPathInput, "", kPathWidth);
    m_form.row("Auto-save", m_autoSaveToggle, m_autoSaveDetail->getText(), 40.0f);
    m_form.layout(getBounds());

    // Browse sits beside the folder path, in the same row.
    const auto& path = m_projectsPathInput->getBounds();
    m_browseButton->setBounds(AestraUI::NUIRect(path.right() + 8.0f, path.y, 84.0f, path.height));
    m_browseButton->setVisible(m_projectsPathInput->isVisible());
}

} // namespace Aestra
