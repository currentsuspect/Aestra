// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ISettingsPage.h"
#include "NUILabel.h"
#include "NUITextInput.h"
#include "NUIButton.h"
#include <functional>

namespace Nomad {

class GeneralSettingsPage : public ISettingsPage {
public:
    GeneralSettingsPage();
    ~GeneralSettingsPage() override = default;

    std::string getPageID() const override { return "general"; }
    std::string getTitle() const override { return "General"; }

    void applyChanges() override;
    void cancelChanges() override;
    bool hasUnsavedChanges() const override { return m_dirty; }

    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;

    void setOnAutoSaveToggled(std::function<void(bool)> callback) { m_onAutoSaveToggled = std::move(callback); }
    
private:
    void createUI();
    void layoutComponents();

    void syncAutoSaveLabel();

    bool m_dirty = false;
    
    std::shared_ptr<NomadUI::NUILabel> m_projectsPathLabel;
    std::shared_ptr<NomadUI::NUITextInput> m_projectsPathInput;
    std::shared_ptr<NomadUI::NUIButton> m_browseButton;
    
    std::shared_ptr<NomadUI::NUILabel> m_autoSaveLabel;
    std::shared_ptr<NomadUI::NUIButton> m_autoSaveToggle;

    std::function<void(bool)> m_onAutoSaveToggled;
};

} // namespace Nomad
