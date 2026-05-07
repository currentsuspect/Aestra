// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MembershipSettingsPage.h"

#include "NUIThemeSystem.h"

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
#include "AccountSession.h"
#include "MembershipViewModel.h"
#endif

#include <sstream>

namespace Aestra {

namespace {
std::shared_ptr<AestraUI::NUILabel> makeMembershipLabel(float fontSize, float alpha = 1.0f) {
    auto label = std::make_shared<AestraUI::NUILabel>();
    label->setFontSize(fontSize);
    label->setEllipsize(true);
    label->setTextColor(AestraUI::NUIThemeManager::getInstance().getColor("textPrimary").withAlpha(alpha));
    return label;
}

std::string compactTierLabel(const std::string& tierLabel) {
    if (tierLabel == "Aestra Founder") {
        return "Founder";
    }
    if (tierLabel == "Aestra Supporter") {
        return "Supporter";
    }
    return "Core";
}

float statusBadgeWidth(const std::string& status) {
    const float estimated = 24.0f + static_cast<float>(status.size()) * 7.5f;
    if (estimated < 92.0f) {
        return 92.0f;
    }
    if (estimated > 230.0f) {
        return 230.0f;
    }
    return estimated;
}

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
std::string serviceStatusMessage(Aestra::License::AccountServiceStatus status) {
    switch (status) {
    case Aestra::License::AccountServiceStatus::Success:
        return "Synced now.";
    case Aestra::License::AccountServiceStatus::Unauthorized:
        return "Session invalid or expired. Sign in again.";
    case Aestra::License::AccountServiceStatus::SyncUnavailable:
        return "Sync unavailable. Last valid local membership was preserved.";
    case Aestra::License::AccountServiceStatus::RejectedSignature:
        return "Server response was rejected because the signed lease was invalid.";
    case Aestra::License::AccountServiceStatus::InvalidResponse:
        return "Server response was malformed. Last valid local membership was preserved.";
    case Aestra::License::AccountServiceStatus::CacheWriteFailed:
        return "Unable to update local account cache.";
    case Aestra::License::AccountServiceStatus::NotConfigured:
    default:
        return "Account API is not configured.";
    }
}
#endif
} // namespace

MembershipSettingsPage::MembershipSettingsPage()
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    : m_transport(Aestra::License::createDefaultHttpTransport())
#endif
{
    createUI();
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    Aestra::License::AccountApiConfig config = Aestra::License::accountApiConfigFromEnvironment();
    m_apiClient = std::make_unique<Aestra::License::AccountApiClient>(config, *m_transport);
    m_leaseInstaller = std::make_unique<Aestra::License::LicenseGateLeaseInstaller>();
    m_accountService =
        std::make_unique<Aestra::License::AccountService>(*m_apiClient, m_accountCache, *m_leaseInstaller);
#endif
    refreshDisplay();
}

void MembershipSettingsPage::createUI() {
    m_titleLabel = std::make_shared<AestraUI::NUILabel>();
    m_titleLabel->setText("Account");
    m_titleLabel->setFontSize(15.0f);
    addChild(m_titleLabel);

    m_accountLabel = makeMembershipLabel(12.0f, 0.78f);
    m_tierLabel = makeMembershipLabel(24.0f);
    m_statusLabel = makeMembershipLabel(13.0f);
    m_statusLabel->setBackgroundVisible(true);
    m_statusLabel->setBackgroundColor(AestraUI::NUIThemeManager::getInstance().getColor("accent").withAlpha(0.16f));
    m_statusLabel->setBorderVisible(true);
    m_statusLabel->setBorderColor(AestraUI::NUIThemeManager::getInstance().getColor("accent").withAlpha(0.34f));
    m_statusLabel->setBorderWidth(1.0f);
    m_verificationLabel = makeMembershipLabel(12.0f, 0.72f);
    m_syncLabel = makeMembershipLabel(12.0f, 0.72f);
    m_lastRefreshLabel = makeMembershipLabel(12.0f, 0.72f);
    m_detailLabel = makeMembershipLabel(13.0f, 0.80f);
    m_featuresTitleLabel = makeMembershipLabel(13.0f);
    m_featuresTitleLabel->setText("Features");

    addChild(m_accountLabel);
    addChild(m_tierLabel);
    addChild(m_statusLabel);
    addChild(m_verificationLabel);
    addChild(m_syncLabel);
    addChild(m_lastRefreshLabel);
    addChild(m_detailLabel);
    addChild(m_featuresTitleLabel);

    m_featureLabels.reserve(12);
    for (int i = 0; i < 12; ++i) {
        auto row = makeMembershipLabel(12.0f, 0.78f);
        m_featureLabels.push_back(row);
        addChild(row);
    }

    m_refreshButton = std::make_shared<AestraUI::NUIButton>();
    m_refreshButton->setText("Refresh");
    m_refreshButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_refreshButton->setOnClick([this]() { refreshAccount(); });
    addChild(m_refreshButton);

    m_signOutButton = std::make_shared<AestraUI::NUIButton>();
    m_signOutButton->setText("Sign Out");
    m_signOutButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_signOutButton->setOnClick([this]() { signOut(); });
    addChild(m_signOutButton);
}

void MembershipSettingsPage::onShow() {
    refreshDisplay();
}

void MembershipSettingsPage::refreshDisplay() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    Aestra::License::EntitlementStore entitlements;
    Aestra::License::AccountSession accountSession(m_accountCache, entitlements);
    Aestra::License::MembershipViewModel viewModel(accountSession, entitlements);
    const Aestra::License::MembershipViewState state = viewModel.current();

    m_accountLabel->setText("Account: " + state.accountLabel);
    m_tierLabel->setText(compactTierLabel(state.tierLabel));
    m_statusLabel->setText(state.statusLabel);
    m_verificationLabel->setText(std::string("Verification: ") +
                                 (state.verified ? "Verified signed lease" : "Core or unverified local state"));
    m_syncLabel->setText(std::string("Sync: ") + (state.offline ? "Local/offline cache" : "Online"));
    m_lastRefreshLabel->setText("Last refresh: " + m_lastRefreshMessage);
    m_detailLabel->setText(state.detailMessage);
    m_featuresTitleLabel->setText("Features");

    std::vector<std::string> rows;
    rows.reserve(m_featureLabels.size());
    rows.push_back("Available");
    for (const Aestra::License::MembershipFeatureRow& feature : state.features) {
        if (feature.enabled) {
            rows.push_back("+ " + feature.label);
        }
    }

    bool hasLocked = false;
    for (const Aestra::License::MembershipFeatureRow& feature : state.features) {
        if (!feature.enabled) {
            if (!hasLocked) {
                if (rows.size() < m_featureLabels.size()) {
                    rows.push_back("Locked");
                }
                hasLocked = true;
            }
            if (rows.size() < m_featureLabels.size()) {
                rows.push_back("- " + feature.label);
            }
        }
    }

    if (hasLocked && rows.size() < m_featureLabels.size()) {
        rows.push_back("Requires verified membership.");
    }

    for (std::size_t i = 0; i < m_featureLabels.size(); ++i) {
        if (i >= rows.size()) {
            m_featureLabels[i]->setText("");
            m_featureLabels[i]->setVisible(false);
            continue;
        }
        m_featureLabels[i]->setText(rows[i]);
        const bool heading = rows[i] == "Available" || rows[i] == "Locked";
        const bool note = rows[i] == "Requires verified membership.";
        m_featureLabels[i]->setFontSize(heading ? 12.0f : 11.5f);
        m_featureLabels[i]->setTextColor(
            AestraUI::NUIThemeManager::getInstance()
                .getColor("textPrimary")
                .withAlpha(heading ? 0.92f : (note ? 0.66f : 0.76f)));
        m_featureLabels[i]->setVisible(true);
    }

    m_refreshButton->setEnabled(state.canRefresh);
    m_signOutButton->setEnabled(state.canSignOut);
#else
    m_accountLabel->setText("Account: Signed out");
    m_tierLabel->setText("Core");
    m_statusLabel->setText("License services unavailable");
    m_verificationLabel->setText("Verification: Core or unverified local state");
    m_syncLabel->setText("Sync: Local/offline cache");
    m_lastRefreshLabel->setText("Last refresh: Not refreshed this session.");
    m_detailLabel->setText("Core access remains available.");
    m_featuresTitleLabel->setText("Features");
    for (std::size_t i = 0; i < m_featureLabels.size(); ++i) {
        m_featureLabels[i]->setText(i == 0 ? "+ Core DAW" : "");
        m_featureLabels[i]->setFontSize(11.5f);
        m_featureLabels[i]->setTextColor(
            AestraUI::NUIThemeManager::getInstance().getColor("textPrimary").withAlpha(0.76f));
        m_featureLabels[i]->setVisible(i == 0);
    }
    m_refreshButton->setEnabled(false);
    m_signOutButton->setEnabled(false);
#endif
    setDirty(true);
}

void MembershipSettingsPage::refreshAccount() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    if (!m_accountService) {
        m_lastRefreshMessage = "Account service is unavailable.";
    } else {
        const Aestra::License::AccountServiceResult result = m_accountService->refreshEntitlements();
        m_lastRefreshMessage = serviceStatusMessage(result.status);
    }
#else
    m_lastRefreshMessage = "License services unavailable.";
#endif
    refreshDisplay();
}

void MembershipSettingsPage::signOut() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    if (m_accountService) {
        const Aestra::License::AccountServiceResult result = m_accountService->revokeSession(true);
        m_lastRefreshMessage = result.status == Aestra::License::AccountServiceStatus::Success
                                   ? "Signed out locally."
                                   : serviceStatusMessage(result.status);
    }
#else
    m_lastRefreshMessage = "Signed out.";
#endif
    refreshDisplay();
}

void MembershipSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    renderChildren(renderer);
}

void MembershipSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

bool MembershipSettingsPage::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    return AestraUI::NUIComponent::onMouseEvent(event);
}

void MembershipSettingsPage::layoutComponents() {
    const AestraUI::NUIRect b = getBounds();
    const float padding = 20.0f;
    const float buttonWidth = 110.0f;
    const float buttonHeight = 32.0f;
    const float gap = 10.0f;
    const float x = b.x + padding;
    const float contentWidth = b.width - padding * 2.0f;
    float y = b.y + padding;

    m_titleLabel->setBounds(AestraUI::NUIRect(x, y, contentWidth, 26.0f));
    y += 34.0f;

    const float tierHeight = 36.0f;
    const float rowHeight = 19.0f;
    const float buttonY = b.bottom() - padding - buttonHeight;
    const float statusWidth = statusBadgeWidth(m_statusLabel->getText());
    const float statusX = x + contentWidth - statusWidth;
    const auto placeRow = [&](const std::shared_ptr<AestraUI::NUILabel>& label, float height) {
        label->setBounds(AestraUI::NUIRect(x, y, contentWidth, height));
        y += height;
    };

    m_tierLabel->setBounds(AestraUI::NUIRect(x, y, contentWidth - statusWidth - gap, tierHeight));
    m_statusLabel->setBounds(AestraUI::NUIRect(statusX, y + 5.0f, statusWidth, 24.0f));
    y += tierHeight + 4.0f;
    placeRow(m_accountLabel, rowHeight);
    placeRow(m_verificationLabel, rowHeight);
    placeRow(m_syncLabel, rowHeight);
    placeRow(m_lastRefreshLabel, rowHeight);
    y += 5.0f;
    placeRow(m_detailLabel, 24.0f);
    y += 9.0f;
    placeRow(m_featuresTitleLabel, rowHeight);

    const float maxFeatureBottom = buttonY - 14.0f;
    for (const std::shared_ptr<AestraUI::NUILabel>& featureLabel : m_featureLabels) {
        if (featureLabel->getText().empty()) {
            continue;
        }
        if (y + 18.0f > maxFeatureBottom) {
            featureLabel->setVisible(false);
            continue;
        }
        featureLabel->setVisible(true);
        placeRow(featureLabel, 18.0f);
    }

    m_refreshButton->setBounds(AestraUI::NUIRect(x, buttonY, buttonWidth, buttonHeight));
    m_signOutButton->setBounds(AestraUI::NUIRect(x + buttonWidth + gap, buttonY, buttonWidth, buttonHeight));
}

} // namespace Aestra
