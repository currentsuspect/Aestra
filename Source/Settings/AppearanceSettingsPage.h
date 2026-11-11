// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ISettingsPage.h"
#include "NUIDropdown.h"
#include "NUILabel.h"
#include "SettingsForm.h"

namespace Aestra {

class AppearanceSettingsPage : public ISettingsPage {
public:
    AppearanceSettingsPage();
    ~AppearanceSettingsPage() override = default;

    std::string getPageID() const override { return "appearance"; }
    std::string getTitle() const override { return "Look"; }

    void applyChanges() override;
    void cancelChanges() override;
    bool hasUnsavedChanges() const override { return m_dirty; }
    void onShow() override;

    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    
private:
    SettingsForm m_form;
    void createUI();
    void layoutComponents();
    void selectTheme(const std::string& themeName);
    int indexForTheme(const std::string& themeName) const;

    bool m_dirty = false;
    bool m_syncingSelection = false;
    std::string m_originalTheme;
    std::string m_pendingTheme;
    
    std::shared_ptr<AestraUI::NUILabel> m_themeLabel;
    std::shared_ptr<AestraUI::NUIDropdown> m_themeDropdown;
};

} // namespace Aestra
