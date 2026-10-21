// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Settings > Updates: which build you are on, which channel you follow, whether Aestra looks by itself, and what the last
// look found. Choices save as they are made (they are not part of the dialog's Apply/Cancel). Installing an update is a
// separate step that follows.

#include "ISettingsPage.h"
#include "NUIButton.h"
#include "NUITextInput.h"
#include "NUICoreWidgets.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {

class UpdateNotifier;

class UpdatesSettingsPage : public ISettingsPage {
public:
    explicit UpdatesSettingsPage(std::shared_ptr<UpdateNotifier> notifier);
    ~UpdatesSettingsPage() override = default;

    std::string getPageID() const override { return "updates"; }
    std::string getTitle() const override { return "Updates"; }

    void applyChanges() override {}
    void cancelChanges() override {}
    bool hasUnsavedChanges() const override { return false; }

    void onShow() override;
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;

private:
    void createUI();
    void layoutComponents();
    void syncFromSettings();
    void chooseChannel(const std::string& channel);
    void commitKey(bool thenCheck);
    void checkNow();
    void onInstallClicked();
    bool installVisible() const;

    std::shared_ptr<UpdateNotifier> m_notifier;
    std::shared_ptr<AestraUI::NUIButton> m_checkButton;
    std::shared_ptr<AestraUI::NUIButton> m_stableButton;
    std::shared_ptr<AestraUI::NUIButton> m_earlyButton;
    std::shared_ptr<AestraUI::NUITextInput> m_keyInput;
    std::shared_ptr<AestraUI::NUIToggle> m_autoToggle;
    std::shared_ptr<AestraUI::NUIButton> m_installButton; // Download and install / Restart and update / Try again
    std::shared_ptr<AestraUI::NUIButton> m_cancelButton;

    bool m_checking = false;
    double m_checkingFor = 0.0;
    uint64_t m_resultCountAtCheck = 0;
    uint64_t m_ignoreResultsBefore = 0; ///< results older than the last channel switch are about another channel
    bool m_early = false;

    AestraUI::NUIRect m_statusCard, m_installCard, m_channelCard, m_autoCard, m_notesCard;
    bool m_installShown = false;
    float m_keyRowY = 0.0f;
};

} // namespace Aestra
