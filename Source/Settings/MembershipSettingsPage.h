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

#include <functional>
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
    void onUpdate(double deltaTime) override;

    void setOnSignOutConfirmed(std::function<void()> callback) { m_onSignOutConfirmed = callback; }

private:
    void createUI();
    void layoutComponents();
    void refreshDisplay();
    void refreshAccount();
    void signOut();
    void startLogin();
    void verifyLogin();
    void confirmSignOut();

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
    std::string m_lastRefreshValue = "Not refreshed";
    bool m_lastRefreshFailed = false;
    std::string m_pendingLoginEmail;
    std::string m_pendingChallengeId;
    // Feedback for the sign-in flow (Send Code / Verify). Kept separate from
    // m_lastRefreshValue so a long status sentence isn't dumped into the SYNC card's
    // right-aligned "Last refresh" value, where it overflowed across the card.
    std::string m_signInMessage;
    float m_signInFormBottomY = 0.0f;

    // Redesign layout bounds (for hit testing)
    AestraUI::NUIRect m_founderCardBounds;
    AestraUI::NUIRect m_featuresCardBounds;
    AestraUI::NUIRect m_syncCardBounds;
    AestraUI::NUIRect m_actionsRowBounds;
    AestraUI::NUIRect m_btnRefreshBounds;
    AestraUI::NUIRect m_btnManageBounds;
    AestraUI::NUIRect m_btnSignOutBounds;
    bool m_btnRefreshHovered = false;
    bool m_btnManageHovered = false;
    bool m_btnSignOutHovered = false;

    // Refresh loading state
    bool m_isRefreshing = false;

    // Inline error note below actions row
    std::string m_actionErrorMessage;
    float m_actionErrorTimer = 0.0f;

    // Persisted account state (mirrored from updateFromState) so the render/hit
    // paths can show account actions only when they actually apply. Without this
    // the Actions row (Refresh / Manage / Sign out) drew in every state, so the
    // signed-out view showed a nonsensical "Sign out" next to the sign-in form.
    bool m_signedIn = false;
    bool m_canSignOut = false;

    // Sign-out confirmation overlay
    bool m_showingSignOutConfirm = false;
    AestraUI::NUIRect m_confirmDialogBounds;
    AestraUI::NUIRect m_confirmCancelBtnBounds;
    AestraUI::NUIRect m_confirmSignOutBtnBounds;
    bool m_confirmCancelHovered = false;
    bool m_confirmSignOutHovered = false;

    // Callback fired after confirmed sign-out (parent should close dialog + show activation screen)
    std::function<void()> m_onSignOutConfirmed;

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    std::unique_ptr<Aestra::License::IHttpTransport> m_transport;
    std::unique_ptr<Aestra::License::AccountApiClient> m_apiClient;
    std::unique_ptr<Aestra::License::LicenseGateLeaseInstaller> m_leaseInstaller;
    std::unique_ptr<Aestra::License::AccountService> m_accountService;
    Aestra::License::LocalAccountCache m_accountCache;
#endif
};

} // namespace Aestra
