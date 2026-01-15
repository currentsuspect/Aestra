// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ISettingsPage.h"
#include "../NomadUI/Core/NUILabel.h"
#include "../NomadUI/Widgets/NUIDropdown.h"

namespace Nomad {

class AppearanceSettingsPage : public ISettingsPage {
public:
    AppearanceSettingsPage();
    ~AppearanceSettingsPage() override = default;

    std::string getPageID() const override { return "appearance"; }
    std::string getTitle() const override { return "Appearance"; }

    void applyChanges() override { m_dirty = false; }
    void cancelChanges() override { m_dirty = false; }
    bool hasUnsavedChanges() const override { return m_dirty; }

    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    
private:
    void createUI();
    void layoutComponents();

    bool m_dirty = false;
    
    std::shared_ptr<NomadUI::NUILabel> m_themeLabel;
    std::shared_ptr<NomadUI::NUIDropdown> m_themeDropdown;
};

} // namespace Nomad
