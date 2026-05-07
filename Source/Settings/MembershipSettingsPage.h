// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ISettingsPage.h"
#include "NUIButton.h"
#include "NUILabel.h"
#include "NUITextInput.h"

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
#include "AccountApiClient.h"
#include "AccountService.h"
#include "EntitlementStore.h"
#include "HttpTransport.h"
#include "LicenseGate.h"
#include "LocalAccountCache.h"
#endif

#include <memory>
#include <string>
#include <vector>

namespace Aestra {

class MembershipSettingsPage : public ISettingsPage {
public:
    MembershipSettingsPage();
    ~MembershipSettingsPage() override = default;

    std::string getPageID() const override { return "membership"; }
    std::string getTitle() const override { return "Membership"; }

    void onShow() override;
    void applyChanges() override {}
    void cancelChanges() override { refreshDisplay(); }
    bool hasUnsavedChanges() const override { return false; }
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;

private:
    void createUI();
    void layoutComponents();
    void refreshDisplay();
    void refreshAccount();
    void signOut();
    void startLogin();
    void verifyLogin();

    std::shared_ptr<AestraUI::NUILabel> m_titleLabel;
    std::shared_ptr<AestraUI::NUILabel> m_accountLabel;
    std::shared_ptr<AestraUI::NUILabel> m_tierLabel;
    std::shared_ptr<AestraUI::NUILabel> m_statusLabel;
    std::shared_ptr<AestraUI::NUILabel> m_verificationLabel;
    std::shared_ptr<AestraUI::NUILabel> m_syncLabel;
    std::shared_ptr<AestraUI::NUILabel> m_lastRefreshLabel;
    std::shared_ptr<AestraUI::NUILabel> m_detailLabel;
    std::shared_ptr<AestraUI::NUILabel> m_featuresTitleLabel;
    std::vector<std::shared_ptr<AestraUI::NUILabel>> m_featureLabels;
    std::shared_ptr<AestraUI::NUILabel> m_signInTitleLabel;
    std::shared_ptr<AestraUI::NUITextInput> m_emailInput;
    std::shared_ptr<AestraUI::NUITextInput> m_codeInput;
    std::shared_ptr<AestraUI::NUIButton> m_startLoginButton;
    std::shared_ptr<AestraUI::NUIButton> m_verifyLoginButton;
    std::shared_ptr<AestraUI::NUIButton> m_refreshButton;
    std::shared_ptr<AestraUI::NUIButton> m_signOutButton;
    std::string m_lastRefreshMessage = "Not refreshed this session.";
    std::string m_pendingLoginEmail;
    std::string m_pendingChallengeId;

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    std::unique_ptr<Aestra::License::IHttpTransport> m_transport;
    std::unique_ptr<Aestra::License::AccountApiClient> m_apiClient;
    std::unique_ptr<Aestra::License::LicenseGateLeaseInstaller> m_leaseInstaller;
    std::unique_ptr<Aestra::License::AccountService> m_accountService;
    Aestra::License::LocalAccountCache m_accountCache;
#endif
};

} // namespace Aestra
