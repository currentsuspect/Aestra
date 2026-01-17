// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "GeneralSettingsPage.h"

namespace Aestra {

GeneralSettingsPage::GeneralSettingsPage() {
    createUI();
}

void GeneralSettingsPage::createUI() {
    m_projectsPathLabel = std::make_shared<AestraUI::NUILabel>();
    m_projectsPathLabel->setText("Projects Folder:");
    addChild(m_projectsPathLabel);

    m_projectsPathInput = std::make_shared<AestraUI::NUITextInput>();
    // Truncated for better UI presentation
    m_projectsPathInput->setText("C:/.../Projects/AESTRA_Projects");
    addChild(m_projectsPathInput);

    m_browseButton = std::make_shared<AestraUI::NUIButton>();
    m_browseButton->setText("Browse...");
    addChild(m_browseButton);
    
    m_autoSaveLabel = std::make_shared<AestraUI::NUILabel>();
    m_autoSaveLabel->setText("Auto-Save:");
    addChild(m_autoSaveLabel);
    
    m_autoSaveToggle = std::make_shared<AestraUI::NUIButton>();
    m_autoSaveToggle->setToggleable(true);
        m_autoSaveToggle->setToggled(true); // default ON
    syncAutoSaveLabel();
    m_autoSaveToggle->setOnToggle([this](bool enabled) {
        m_dirty = true;
        syncAutoSaveLabel();
        if (m_onAutoSaveToggled) m_onAutoSaveToggled(enabled);
    });
    addChild(m_autoSaveToggle);
}

void GeneralSettingsPage::syncAutoSaveLabel() {
    if (!m_autoSaveToggle) return;
    if (m_autoSaveToggle->isToggled()) {
        m_autoSaveToggle->setText("Enabled (5 min)");
    } else {
        m_autoSaveToggle->setText("Disabled");
    }
}

void GeneralSettingsPage::applyChanges() {
    m_dirty = false;
}

void GeneralSettingsPage::cancelChanges() {
    // Revert
    m_projectsPathInput->setText("C:/Users/Current/Documents/Projects/AESTRA_Projects");
    m_dirty = false;
}

void GeneralSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    renderChildren(renderer);
}

void GeneralSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height); // Store bounds
    layoutComponents();
}

void GeneralSettingsPage::layoutComponents() {
    auto b = getBounds();
    float padding = 20.0f;
    float rowHeight = 30.0f;
    float gap = 15.0f;
    float x = b.x + padding;
    float y = b.y + padding;
    
    // Row 1: Path
    m_projectsPathLabel->setBounds(AestraUI::NUIRect(x, y, 120, rowHeight));
    m_projectsPathInput->setBounds(AestraUI::NUIRect(x + 130, y, 300, rowHeight));
    m_browseButton->setBounds(AestraUI::NUIRect(x + 440, y, 100, rowHeight));
    
    y += rowHeight + gap;
    
    // Row 2: Auto-Save
    m_autoSaveLabel->setBounds(AestraUI::NUIRect(x, y, 120, rowHeight));
    m_autoSaveToggle->setBounds(AestraUI::NUIRect(x + 130, y, 150, rowHeight));
}

} // namespace Aestra
