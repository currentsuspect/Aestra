// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "MembershipSettingsPage.h"

#include "NUIIcon.h"
#include "NUIThemeSystem.h"

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
#include "AccountSession.h"
#include "MembershipViewModel.h"
#endif

#include <algorithm>
#include <ctime>
#include <cstring>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

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

std::string getCurrentTimeString() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(&tm, "Synced · %H:%M");
    return ss.str();
}

// --- Membership icon cache (NUIICON) ---
inline std::shared_ptr<AestraUI::NUIIcon> NUIICON(const char* name) {
    static std::unordered_map<std::string, std::shared_ptr<AestraUI::NUIIcon>> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;

    const char* svg = "";
    if (std::strcmp(name, "shield-check") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><polyline points="9 12 12 15 16 10"/></svg>)";
    } else if (std::strcmp(name, "mail") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/><polyline points="22,6 12,13 2,6"/></svg>)";
    } else if (std::strcmp(name, "clock") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>)";
    } else if (std::strcmp(name, "check") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>)";
    } else if (std::strcmp(name, "lock") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>)";
    } else if (std::strcmp(name, "info") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>)";
    } else if (std::strcmp(name, "refresh") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg>)";
    } else if (std::strcmp(name, "database") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/></svg>)";
    } else if (std::strcmp(name, "key") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.78 7.78 5.5 5.5 0 0 1 7.78-7.78zm0 0L15.5 7.5M19 11l2 2"/></svg>)";
    } else if (std::strcmp(name, "laptop") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="2" y1="20" x2="22" y2="20"/></svg>)";
    } else if (std::strcmp(name, "external-link") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>)";
    } else if (std::strcmp(name, "log-out") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>)";
    } else if (std::strcmp(name, "monitor") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>)";
    } else if (std::strcmp(name, "activity") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>)";
    } else if (std::strcmp(name, "headphones") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/></svg>)";
    } else if (std::strcmp(name, "layers") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>)";
    } else if (std::strcmp(name, "award") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="7"/><polyline points="8.21 13.89 7 23 12 20 17 23 15.79 13.88"/></svg>)";
    } else if (std::strcmp(name, "crown") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 4l3 12h14l3-12-6 7-4-7-4 7-6-7zm3 16h14"/></svg>)";
    } else if (std::strcmp(name, "cloud") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/></svg>)";
    } else if (std::strcmp(name, "zap") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>)";
    } else if (std::strcmp(name, "file-badge") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5L14.5 2z"/><polyline points="14 2 14 8 20 8"/><circle cx="12" cy="15" r="2"/><path d="M12 13v-2"/><path d="M12 17v2"/></svg>)";
    }

    auto icon = std::make_shared<AestraUI::NUIIcon>(svg);
    icon->setColorFromTheme("textPrimary");
    cache[name] = icon;
    return icon;
}

inline void drawMembershipIcon(AestraUI::NUIRenderer& renderer, const char* name, float cx, float cy, float size, const AestraUI::NUIColor& color) {
    auto icon = NUIICON(name);
    icon->setIconSize(size, size);
    icon->setColor(color);
    icon->setBounds(AestraUI::NUIRect(cx - size * 0.5f, cy - size * 0.5f, size, size));
    icon->onRender(renderer);
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

    m_signInTitleLabel = makeMembershipLabel(13.0f);
    m_signInTitleLabel->setText("Sign in");
    addChild(m_signInTitleLabel);

    m_emailInput = std::make_shared<AestraUI::NUITextInput>();
    m_emailInput->setInputType(AestraUI::NUITextInput::InputType::Email);
    m_emailInput->setPlaceholderText("Email");
    m_emailInput->setMaxLength(254);
    m_emailInput->setOnReturnKey([this]() { startLogin(); });
    addChild(m_emailInput);

    m_codeInput = std::make_shared<AestraUI::NUITextInput>();
    m_codeInput->setPlaceholderText("Login code");
    m_codeInput->setMaxLength(32);
    m_codeInput->setOnReturnKey([this]() { verifyLogin(); });
    addChild(m_codeInput);

    m_startLoginButton = std::make_shared<AestraUI::NUIButton>();
    m_startLoginButton->setText("Send Code");
    m_startLoginButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_startLoginButton->setOnClick([this]() { startLogin(); });
    addChild(m_startLoginButton);

    m_verifyLoginButton = std::make_shared<AestraUI::NUIButton>();
    m_verifyLoginButton->setText("Verify");
    m_verifyLoginButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_verifyLoginButton->setOnClick([this]() { verifyLogin(); });
    addChild(m_verifyLoginButton);

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
    layoutComponents();
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
    m_lastRefreshLabel->setText("Last refresh: " + m_lastRefreshValue);
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

    m_signedIn = state.signedIn;
    m_canSignOut = state.canSignOut;
    m_refreshButton->setEnabled(state.canRefresh);
    m_signOutButton->setEnabled(state.canSignOut);
    const bool showSignIn = !state.signedIn;
    m_signInTitleLabel->setVisible(showSignIn);
    m_emailInput->setVisible(showSignIn);
    m_codeInput->setVisible(showSignIn);
    m_startLoginButton->setVisible(showSignIn);
    m_verifyLoginButton->setVisible(showSignIn);
    m_startLoginButton->setEnabled(showSignIn);
    m_verifyLoginButton->setEnabled(showSignIn && !m_pendingChallengeId.empty());
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
    m_signedIn = false;
    m_canSignOut = false;
    m_refreshButton->setEnabled(false);
    m_signOutButton->setEnabled(false);
    m_signInTitleLabel->setVisible(false);
    m_emailInput->setVisible(false);
    m_codeInput->setVisible(false);
    m_startLoginButton->setVisible(false);
    m_verifyLoginButton->setVisible(false);
#endif
    layoutComponents();
    setDirty(true);
}

void MembershipSettingsPage::refreshAccount() {
    if (m_isRefreshing) return;
    m_isRefreshing = true;
    m_lastRefreshFailed = false;
    m_actionErrorMessage.clear();
    repaint();

#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    if (!m_accountService) {
        m_lastRefreshValue = "Failed to sync";
        m_lastRefreshFailed = true;
        m_actionErrorMessage = "Could not reach license server. Try again later.";
        m_actionErrorTimer = 6.0f;
    } else {
        m_lastRefreshValue = "Syncing…";
        repaint();
        const Aestra::License::AccountServiceResult result = m_accountService->refreshEntitlements();
        if (result.status == Aestra::License::AccountServiceStatus::Success) {
            m_lastRefreshValue = getCurrentTimeString();
            m_lastRefreshFailed = false;
        } else if (result.status == Aestra::License::AccountServiceStatus::RejectedSignature ||
                   result.status == Aestra::License::AccountServiceStatus::Unauthorized) {
            // Lease invalid/revoked: treat as sign-out
            m_lastRefreshValue = "Failed to sync";
            m_lastRefreshFailed = true;
            m_actionErrorMessage = "Your license could not be verified. Please sign in again.";
            m_actionErrorTimer = 6.0f;
            signOut();
            if (m_onSignOutConfirmed) m_onSignOutConfirmed();
            m_isRefreshing = false;
            refreshDisplay();
            return;
        } else {
            m_lastRefreshValue = "Failed to sync";
            m_lastRefreshFailed = true;
            m_actionErrorMessage = "Could not reach license server. Try again later.";
            m_actionErrorTimer = 6.0f;
        }
    }
#else
    m_lastRefreshValue = "Failed to sync";
    m_lastRefreshFailed = true;
    m_actionErrorMessage = "Could not reach license server. Try again later.";
    m_actionErrorTimer = 6.0f;
#endif
    m_isRefreshing = false;
    refreshDisplay();
}

void MembershipSettingsPage::startLogin() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    const std::string email = m_emailInput ? m_emailInput->getText() : "";
    if (!m_accountService) {
        m_signInMessage = "Account service is unavailable.";
    } else if (email.empty()) {
        m_signInMessage = "Email is required.";
    } else {
        const Aestra::License::AccountLoginStartServiceResult result = m_accountService->loginStart(email);
        if (result.status == Aestra::License::AccountServiceStatus::Success && !result.challengeId.empty()) {
            m_pendingLoginEmail = email;
            m_pendingChallengeId = result.challengeId;
            m_signInMessage = "Login code sent. Enter it below.";
        } else {
            m_pendingChallengeId.clear();
            m_signInMessage = serviceStatusMessage(result.status);
        }
    }
#else
    m_signInMessage = "License services unavailable.";
#endif
    refreshDisplay();
}

void MembershipSettingsPage::verifyLogin() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    const std::string email =
        !m_pendingLoginEmail.empty() ? m_pendingLoginEmail : (m_emailInput ? m_emailInput->getText() : "");
    const std::string code = m_codeInput ? m_codeInput->getText() : "";
    if (!m_accountService) {
        m_signInMessage = "Account service is unavailable.";
    } else if (email.empty() || m_pendingChallengeId.empty() || code.empty()) {
        m_signInMessage = "Email, challenge, and code are required.";
    } else {
        const Aestra::License::AccountServiceResult login =
            m_accountService->loginVerify(email, m_pendingChallengeId, code);
        if (login.status != Aestra::License::AccountServiceStatus::Success) {
            m_signInMessage = serviceStatusMessage(login.status);
        } else {
            m_pendingChallengeId.clear();
            const Aestra::License::AccountServiceResult refresh = m_accountService->refreshEntitlements();
            m_signInMessage = refresh.status == Aestra::License::AccountServiceStatus::Success
                                       ? "Signed in and synced now."
                                       : "Signed in. " + serviceStatusMessage(refresh.status);
        }
    }
#else
    m_signInMessage = "License services unavailable.";
#endif
    refreshDisplay();
}

void MembershipSettingsPage::signOut() {
#if defined(AESTRA_HAS_LICENSE_GATE) && AESTRA_HAS_LICENSE_GATE
    if (m_accountService) {
        const Aestra::License::AccountServiceResult result = m_accountService->revokeSession(true);
        m_lastRefreshValue = result.status == Aestra::License::AccountServiceStatus::Success
                                   ? "Signed out locally."
                                   : serviceStatusMessage(result.status);
    }
    m_pendingChallengeId.clear();
    m_pendingLoginEmail.clear();
#else
    m_lastRefreshValue = "Signed out.";
#endif
    m_showingSignOutConfirm = false;
    refreshDisplay();
}

void MembershipSettingsPage::confirmSignOut() {
    m_showingSignOutConfirm = true;
    m_confirmCancelHovered = false;
    m_confirmSignOutHovered = false;

    const AestraUI::NUIRect b = getBounds();
    const float dialogW = 400.0f;
    const float dialogH = 160.0f;
    m_confirmDialogBounds = AestraUI::NUIRect(
        b.x + (b.width - dialogW) * 0.5f,
        b.y + (b.height - dialogH) * 0.5f,
        dialogW, dialogH);

    const float btnW = 110.0f;
    const float btnH = 34.0f;
    const float btnGap = 10.0f;
    const float btnY = m_confirmDialogBounds.bottom() - 24.0f - btnH;
    float totalBtnW = 2.0f * btnW + btnGap;
    float btnStartX = m_confirmDialogBounds.x + (dialogW - totalBtnW) * 0.5f;
    m_confirmCancelBtnBounds = AestraUI::NUIRect(btnStartX, btnY, btnW, btnH);
    m_confirmSignOutBtnBounds = AestraUI::NUIRect(btnStartX + btnW + btnGap, btnY, btnW, btnH);

    repaint();
}

void MembershipSettingsPage::onUpdate(double deltaTime) {
    if (m_actionErrorTimer > 0.0f) {
        m_actionErrorTimer -= static_cast<float>(deltaTime);
        if (m_actionErrorTimer <= 0.0f) {
            m_actionErrorMessage.clear();
            repaint();
        }
    }
}

void MembershipSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const AestraUI::NUIRect b = getBounds();
    if (b.isEmpty()) return;

    // --- Theme aliases ---
    const auto bgPrimary = theme.getColor("backgroundPrimary");
    const auto bgSecondary = theme.getColor("backgroundSecondary");
    const auto bgTertiary = theme.getColor("surfaceTertiary");
    const auto borderFaint = theme.getColor("borderSubtle");
    const auto borderSecondary = theme.getColor("border");
    const auto textPrimary = theme.getColor("textPrimary");
    const auto textSecondary = theme.getColor("textSecondary");
    const auto textTertiary = theme.getColor("textDisabled");
    const auto textSuccess = theme.getColor("success");
    const auto textDanger = theme.getColor("error");

    // --- 1. FOUNDER CARD ---
    {
        const auto& cb = m_founderCardBounds;
        const float r = 12.0f;
        renderer.fillRoundedRect(cb, r, bgTertiary);
        renderer.strokeRoundedRect(cb, r, 0.5f, borderFaint);

        // Decorative circles (bottom-right, clipped by card bounds)
        renderer.strokeCircle({cb.right() + 28.0f, cb.bottom() + 28.0f}, 50.0f, 0.5f,
                              borderFaint.withAlpha(0.3f));
        renderer.strokeCircle({cb.right() + 48.0f, cb.bottom() + 48.0f}, 70.0f, 0.5f,
                              borderFaint.withAlpha(0.2f));

        float cx = cb.x + 24.0f;
        float cy = cb.y + 20.0f;

        // Tier label
        renderer.drawText("MEMBERSHIP TIER", {cx, cy}, 11.0f, textTertiary);
        cy += 16.0f;

        // Tier name
        renderer.drawText(m_tierLabel->getText(), {cx, cy}, 26.0f, textPrimary);
        cy += 38.0f;

        // Verified pill (top-right)
        {
            const float pillW = 78.0f;
            const float pillH = 22.0f;
            const float px = cb.right() - pillW - 20.0f;
            const float py = cb.y + 20.0f;
            renderer.fillRoundedRect(AestraUI::NUIRect(px, py, pillW, pillH), 999.0f,
                                     theme.getColor("success").withAlpha(0.14f));
            renderer.strokeRoundedRect(AestraUI::NUIRect(px, py, pillW, pillH), 999.0f, 0.5f,
                                       theme.getColor("success").withAlpha(0.35f));
            drawMembershipIcon(renderer, "shield-check", px + 14.0f, py + pillH * 0.5f, 14.0f, textSuccess);
            renderer.drawText("Verified", {px + 24.0f, py + 5.0f}, 11.0f, textSuccess);
        }

        // Account meta rows
        drawMembershipIcon(renderer, "mail", cx + 7.0f, cy + 7.0f, 15.0f, textSecondary.withAlpha(0.6f));
        renderer.drawText(m_accountLabel->getText().empty() ? "currentsuspect@gmail.com"
                                                            : m_accountLabel->getText(),
                          {cx + 20.0f, cy + 2.0f}, 13.0f, textSecondary);
        cy += 22.0f;
        drawMembershipIcon(renderer, "file-badge", cx + 7.0f, cy + 7.0f, 15.0f, textSecondary.withAlpha(0.6f));
        renderer.drawText(m_verificationLabel->getText().empty() ? "Signed lease \u00b7 verified locally"
                                                               : m_verificationLabel->getText(),
                          {cx + 20.0f, cy + 2.0f}, 13.0f, textSecondary);
        cy += 22.0f;
        drawMembershipIcon(renderer, "clock", cx + 7.0f, cy + 7.0f, 15.0f, textSecondary.withAlpha(0.6f));
        renderer.drawText("Last refresh: this session", {cx + 20.0f, cy + 2.0f}, 13.0f, textSecondary);
    }

    // --- 2. FEATURES CARD ---
    {
        const auto& cb = m_featuresCardBounds;
        const float r = 12.0f;
        renderer.fillRoundedRect(cb, r, bgPrimary);
        renderer.strokeRoundedRect(cb, r, 0.5f, borderFaint);

        float fx = cb.x + 18.0f;
        float fy = cb.y + 16.0f;
        renderer.drawText("FEATURES", {fx, fy}, 11.0f, textTertiary);
        fy += 24.0f;

        struct FeatureChip { const char* label; const char* iconName; const char* statusIcon; bool unlocked; };
        FeatureChip chips[] = {
            {"Core DAW", "monitor", "check", true},
            {"Aestra Rumble", "activity", "check", true},
            {"Rumble Headless", "headphones", "check", true},
            {"Plugin Bundle", "layers", "check", true},
            {"Supporter Badge", "award", "check", true},
            {"Founder Badge", "crown", "check", true},
            {"Cloud Sync", "cloud", "lock", false},
            {"Early Access", "zap", "lock", false},
        };
        const float chipH = 30.0f;
        const float chipGap = 8.0f;
        const float colW = (cb.width - 36.0f - chipGap) * 0.5f;

        for (int i = 0; i < 8; ++i) {
            float col = i % 2;
            float row = i / 2;
            float chipX = fx + col * (colW + chipGap);
            float chipY = fy + row * (chipH + chipGap);
            AestraUI::NUIRect chipRect(chipX, chipY, colW, chipH);

            // Entire locked chip at 0.45 opacity
            auto chipBg = chips[i].unlocked ? bgTertiary : bgTertiary.withAlpha(0.45f);
            auto chipBorder = chips[i].unlocked ? borderFaint : borderFaint.withAlpha(0.45f);
            renderer.fillRoundedRect(chipRect, 8.0f, chipBg);
            renderer.strokeRoundedRect(chipRect, 8.0f, 0.5f, chipBorder);

            auto chipTextCol = chips[i].unlocked ? textPrimary : textPrimary.withAlpha(0.45f);
            float midY = chipY + chipH * 0.5f;
            float ix = chipX + 10.0f;

            if (chips[i].unlocked) {
                drawMembershipIcon(renderer, chips[i].statusIcon, ix, midY, 14.0f, textSuccess);
            } else {
                drawMembershipIcon(renderer, chips[i].statusIcon, ix, midY, 14.0f, textTertiary.withAlpha(0.45f));
            }

            auto iconCol = chips[i].unlocked ? textSecondary : textSecondary.withAlpha(0.45f);
            drawMembershipIcon(renderer, chips[i].iconName, ix + 20.0f, midY, 14.0f, iconCol);
            renderer.drawText(chips[i].label, {ix + 36.0f, midY - 6.5f}, 13.0f, chipTextCol);
        }

        // Footer note
        float footY = fy + 4.0f * (chipH + chipGap) + 10.0f;
        drawMembershipIcon(renderer, "info", fx + 7.0f, footY + 7.0f, 14.0f, textTertiary);
        renderer.drawText("Locked features require verified membership", {fx + 22.0f, footY + 3.0f}, 11.0f, textTertiary);
    }

    // --- 3. SYNC & SESSION CARD ---
    {
        const auto& cb = m_syncCardBounds;
        const float r = 12.0f;
        renderer.fillRoundedRect(cb, r, bgPrimary);
        renderer.strokeRoundedRect(cb, r, 0.5f, borderFaint);

        float sx = cb.x + 18.0f;
        float sy = cb.y + 16.0f;
        renderer.drawText("SYNC & SESSION", {sx, sy}, 11.0f, textTertiary);
        sy += 28.0f;

        struct SyncRow { const char* iconName; const char* label; const char* value; bool muted; };
        SyncRow rows[] = {
            {"refresh", "Last refresh", m_lastRefreshValue.c_str(), true},
            {"database", "Sync mode", "Local / offline cache", false},
            {"key", "License type", "Signed lease", false},
            {"laptop", "Machine", "This device", false},
        };
        const float rowH = 36.0f;
        for (int i = 0; i < 4; ++i) {
            float ry = sy + i * rowH;
            if (i < 3) {
                renderer.drawLine({sx, ry + rowH}, {cb.right() - 18.0f, ry + rowH}, 0.5f, borderFaint);
            }
            drawMembershipIcon(renderer, rows[i].iconName, sx + 7.0f, ry + rowH * 0.5f, 15.0f, textSecondary.withAlpha(0.6f));
            renderer.drawText(rows[i].label, {sx + 22.0f, ry + 9.0f}, 13.0f, textSecondary);
            auto valCol = (i == 0 && m_lastRefreshFailed) ? textDanger
                          : rows[i].muted               ? textTertiary
                                                        : textPrimary;
            auto valSize = renderer.measureText(rows[i].value, 13.0f);
            renderer.drawText(rows[i].value, {cb.right() - 18.0f - valSize.width, ry + 9.0f}, 13.0f, valCol);
        }
        // TODO: bind Sync mode row to actual sync state when Cloud Sync is implemented
    }

    // --- 4. ACTIONS ROW ---
    // Refresh / Manage / Sign out only make sense for an active account. Drawing
    // them while signed out put a nonsensical "Sign out" beside the sign-in form.
    if (m_signedIn) {
        const auto drawActionBtn = [&](const AestraUI::NUIRect& rect, bool hovered,
                                        const char* label, const char* iconName, bool danger) {
            auto borderCol = danger ? textDanger.withAlpha(0.35f) : borderSecondary.withAlpha(0.45f);
            auto textCol = danger ? textDanger : textSecondary;
            auto bg = danger ? AestraUI::NUIColor::transparent() : bgSecondary.withAlpha(0.5f);
            if (hovered) {
                bg = danger ? textDanger.withAlpha(0.12f) : bgSecondary.withAlpha(0.85f);
                borderCol = danger ? textDanger.withAlpha(0.55f) : borderSecondary.withAlpha(0.65f);
            }
            if (!danger || hovered) {
                renderer.fillRoundedRect(rect, 8.0f, bg);
            }
            renderer.strokeRoundedRect(rect, 8.0f, 0.5f, borderCol);

            // Measure content block width (icon + gap + text)
            auto textSize = renderer.measureText(label, 13.0f);
            float contentW = 15.0f + 6.0f + textSize.width;
            float contentX = rect.x + (rect.width - contentW) * 0.5f;
            float midY = rect.y + rect.height * 0.5f;
            drawMembershipIcon(renderer, iconName, contentX + 7.5f, midY, 15.0f, textCol);
            renderer.drawText(label, {contentX + 21.0f, midY - 6.5f}, 13.0f, textCol);
        };

        const char* refreshLabel = m_isRefreshing ? "Refreshing\u2026" : "Refresh license";
        drawActionBtn(m_btnRefreshBounds, m_btnRefreshHovered, refreshLabel, "refresh", false);
        drawActionBtn(m_btnManageBounds, m_btnManageHovered, "Manage account", "external-link", false);
        drawActionBtn(m_btnSignOutBounds, m_btnSignOutHovered, "Sign out", "log-out", true);

        // Inline error below actions row
        if (!m_actionErrorMessage.empty()) {
            float errY = m_actionsRowBounds.bottom() + 8.0f;
            renderer.drawText(m_actionErrorMessage, {m_actionsRowBounds.x, errY}, 12.0f, textDanger);
        }
    }

    // --- 5. SIGN-IN UI (if needed, rendered above the new design) ---
    if (m_signInTitleLabel && m_signInTitleLabel->isVisible()) {
        renderChildren(renderer);

        // Sign-in feedback (Send Code / Verify) on its own line under the form,
        // left-aligned so it can't overflow like it did in the SYNC card. "Sent"/
        // "Signed in" reads as positive; everything else is a plain notice.
        if (!m_signInMessage.empty()) {
            const bool positive = m_signInMessage.find("sent") != std::string::npos ||
                                  m_signInMessage.find("Signed in") != std::string::npos;
            const auto msgColor = positive ? AestraUI::NUIColor(0.45f, 0.78f, 0.55f, 1.0f) // green
                                            : textSecondary;
            renderer.drawText(m_signInMessage, {b.x + 20.0f, m_signInFormBottomY + 10.0f}, 12.0f, msgColor);
        }
    }

    // --- 6. SIGN-OUT CONFIRMATION OVERLAY ---
    if (m_showingSignOutConfirm) {
        // Dim overlay
        renderer.fillRect(b, AestraUI::NUIColor(0.0f, 0.0f, 0.0f, 0.5f));

        // Dialog
        auto& db = m_confirmDialogBounds;
        renderer.fillRoundedRect(db, 10.0f, bgSecondary);
        renderer.strokeRoundedRect(db, 10.0f, 0.5f, borderFaint);

        float dx = db.x + 24.0f;
        float dy = db.y + 28.0f;
        renderer.drawText("Sign out of Aestra?", {dx, dy}, 15.0f, textPrimary);
        dy += 26.0f;
        renderer.drawText("Your local license cache will be cleared. You'll need to sign in",
                          {dx, dy}, 12.0f, textSecondary);
        renderer.drawText("again to access your membership features.",
                          {dx, dy + 16.0f}, 12.0f, textSecondary);

        // Cancel button
        {
            const auto& btn = m_confirmCancelBtnBounds;
            auto btnBg = m_confirmCancelHovered ? bgTertiary : bgSecondary;
            auto btnBorder = borderFaint;
            renderer.fillRoundedRect(btn, 6.0f, btnBg);
            renderer.strokeRoundedRect(btn, 6.0f, 0.5f, btnBorder);
            renderer.drawTextCentered("Cancel", btn, 13.0f, textSecondary);
        }

        // Sign out button (danger)
        {
            const auto& btn = m_confirmSignOutBtnBounds;
            auto btnBg = m_confirmSignOutHovered ? textDanger.withAlpha(0.18f) : textDanger.withAlpha(0.08f);
            auto btnBorder = textDanger.withAlpha(0.45f);
            renderer.fillRoundedRect(btn, 6.0f, btnBg);
            renderer.strokeRoundedRect(btn, 6.0f, 0.5f, btnBorder);
            renderer.drawTextCentered("Sign out", btn, 13.0f, textDanger);
        }
    }
}

void MembershipSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

bool MembershipSettingsPage::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    float mx = event.position.x;
    float my = event.position.y;

    // Sign-out confirmation overlay takes priority
    if (m_showingSignOutConfirm) {
        // Update hover states based on current position
        bool oldCancel = m_confirmCancelHovered;
        bool oldSignOut = m_confirmSignOutHovered;
        m_confirmCancelHovered = m_confirmCancelBtnBounds.contains(mx, my);
        m_confirmSignOutHovered = m_confirmSignOutBtnBounds.contains(mx, my);
        if (oldCancel != m_confirmCancelHovered || oldSignOut != m_confirmSignOutHovered) {
            repaint();
        }

        if (event.pressed) {
            if (m_confirmCancelBtnBounds.contains(mx, my)) {
                m_showingSignOutConfirm = false;
                repaint();
                return true;
            }
            if (m_confirmSignOutBtnBounds.contains(mx, my)) {
                m_showingSignOutConfirm = false;
                signOut();
                if (m_onSignOutConfirmed) m_onSignOutConfirmed();
                return true;
            }
            // Click outside dialog = cancel
            if (!m_confirmDialogBounds.contains(mx, my)) {
                m_showingSignOutConfirm = false;
                repaint();
                return true;
            }
        }
        return true; // Consume all mouse events while modal is open
    }

    // Update hover states for redesigned action buttons on every event
    bool oldRefresh = m_btnRefreshHovered;
    bool oldManage = m_btnManageHovered;
    bool oldSignOut = m_btnSignOutHovered;
    m_btnRefreshHovered = m_btnRefreshBounds.contains(mx, my);
    m_btnManageHovered = m_btnManageBounds.contains(mx, my);
    m_btnSignOutHovered = m_btnSignOutBounds.contains(mx, my);
    if (oldRefresh != m_btnRefreshHovered || oldManage != m_btnManageHovered ||
        oldSignOut != m_btnSignOutHovered) {
        repaint();
    }

    // Action buttons only exist (and only hit-test) while signed in — mirrors the
    // render gate so a signed-out click can't trigger an invisible button.
    if (event.pressed && m_signedIn) {
        if (m_btnRefreshBounds.contains(mx, my)) {
            if (!m_isRefreshing) {
                refreshAccount();
            }
            return true;
        }
        if (m_btnManageBounds.contains(mx, my)) {
            // Open account management URL in default browser
            std::system("xdg-open https://aestra.studio/account &");
            return true;
        }
        if (m_btnSignOutBounds.contains(mx, my)) {
            confirmSignOut();
            return true;
        }
    }
    return AestraUI::NUIComponent::onMouseEvent(event);
}

void MembershipSettingsPage::layoutComponents() {
    const AestraUI::NUIRect b = getBounds();
    const float hPad = 20.0f;
    const float vPad = 16.0f;
    const float cardGap = 16.0f;
    const float x = b.x + hPad;
    const float contentW = b.width - hPad * 2.0f;
    float y = b.y + vPad;

    // Founder card (full width)
    const float founderH = 142.0f;
    m_founderCardBounds = AestraUI::NUIRect(x, y, contentW, founderH);
    y += founderH + cardGap;

    // Features + Sync cards (side by side)
    const float cardW = (contentW - cardGap) * 0.5f;
    const float chipH = 30.0f;
    const float chipGap = 8.0f;
    const float chipRows = 4.0f;
    const float featuresCardH = 16.0f + 24.0f + (chipRows * chipH + (chipRows - 1.0f) * chipGap) + 10.0f + 14.0f + 16.0f;
    const float syncCardH = 16.0f + 28.0f + 4.0f * 36.0f + 16.0f;
    const float cardH = std::max(featuresCardH, syncCardH);
    m_featuresCardBounds = AestraUI::NUIRect(x, y, cardW, cardH);
    m_syncCardBounds = AestraUI::NUIRect(x + cardW + cardGap, y, cardW, cardH);
    y += cardH + cardGap;

    // Actions row
    const float actionH = 36.0f;
    m_actionsRowBounds = AestraUI::NUIRect(x, y, contentW, actionH);

    const float btnGap = 8.0f;
    const float btnW = (contentW - btnGap * 2.0f) / 3.0f;
    m_btnRefreshBounds = AestraUI::NUIRect(x, y, btnW, actionH);
    m_btnManageBounds = AestraUI::NUIRect(x + btnW + btnGap, y, btnW, actionH);
    // Last button fills exactly to the right edge
    float lastX = x + 2.0f * (btnW + btnGap);
    m_btnSignOutBounds = AestraUI::NUIRect(lastX, y, (x + contentW) - lastX, actionH);

    // Position sign-in UI below the action row when visible
    const bool showSignIn = m_signInTitleLabel && m_signInTitleLabel->isVisible();
    if (showSignIn) {
        const float rowHeight = 19.0f;
        const float buttonHeight = 32.0f;
        const float sBtnGap = 8.0f;
        const float buttonWidth = 110.0f;
        // The sign-in form and the account Actions row are mutually exclusive
        // (sign-in shows only while signed out, when the Actions row is hidden), so
        // the form takes the Actions row's slot instead of sitting a whole row
        // below the now-empty space — which left it hanging too low. The small
        // lift seats the "Sign in" heading just under the cards.
        float signY = b.y + vPad + founderH + cardGap + cardH + cardGap - 10.0f;
        const float inputWidth = std::max(180.0f, contentW - buttonWidth - sBtnGap);
        m_signInTitleLabel->setBounds(AestraUI::NUIRect(x, signY, contentW, rowHeight));
        m_signInTitleLabel->setVisible(true);
        signY += rowHeight + 4.0f;
        m_emailInput->setBounds(AestraUI::NUIRect(x, signY, inputWidth, buttonHeight));
        m_emailInput->setVisible(true);
        m_startLoginButton->setBounds(AestraUI::NUIRect(x + inputWidth + sBtnGap, signY, buttonWidth, buttonHeight));
        m_startLoginButton->setVisible(true);
        signY += buttonHeight + sBtnGap;
        m_codeInput->setBounds(AestraUI::NUIRect(x, signY, inputWidth, buttonHeight));
        m_codeInput->setVisible(true);
        m_verifyLoginButton->setBounds(AestraUI::NUIRect(x + inputWidth + sBtnGap, signY, buttonWidth, buttonHeight));
        m_verifyLoginButton->setVisible(true);
        m_signInFormBottomY = signY + buttonHeight; // status line renders below this
    } else {
        if (m_signInTitleLabel) m_signInTitleLabel->setVisible(false);
        if (m_emailInput) m_emailInput->setVisible(false);
        if (m_startLoginButton) m_startLoginButton->setVisible(false);
        if (m_codeInput) m_codeInput->setVisible(false);
        if (m_verifyLoginButton) m_verifyLoginButton->setVisible(false);
    }

    // Legacy: hide old children that are no longer visually used. The panel draws
    // its header/info/features/actions itself; these child widgets predate that and
    // are only kept for state plumbing. Any left visible get drawn by
    // renderChildren() at their default (0,0) bounds — that stray "Account: Signed
    // out" was leaking into the window's top-left corner.
    m_titleLabel->setVisible(false);
    m_accountLabel->setVisible(false);
    m_tierLabel->setVisible(false);
    m_statusLabel->setVisible(false);
    m_verificationLabel->setVisible(false);
    m_syncLabel->setVisible(false);
    m_lastRefreshLabel->setVisible(false);
    m_detailLabel->setVisible(false);
    m_featuresTitleLabel->setVisible(false);
    for (auto& fl : m_featureLabels) {
        if (fl) fl->setVisible(false);
    }
    m_refreshButton->setVisible(false);
    m_signOutButton->setVisible(false);
    m_featuresTitleLabel->setVisible(false);
    for (auto& f : m_featureLabels) f->setVisible(false);
}

} // namespace Aestra
