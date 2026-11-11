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
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><polyline points="9 12 12 15 16 10"/></svg>)";
    } else if (std::strcmp(name, "mail") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/><polyline points="22,6 12,13 2,6"/></svg>)";
    } else if (std::strcmp(name, "clock") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/></svg>)";
    } else if (std::strcmp(name, "check") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>)";
    } else if (std::strcmp(name, "lock") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>)";
    } else if (std::strcmp(name, "info") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><line x1="12" y1="16" x2="12" y2="12"/><line x1="12" y1="8" x2="12.01" y2="8"/></svg>)";
    } else if (std::strcmp(name, "refresh") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/></svg>)";
    } else if (std::strcmp(name, "database") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/></svg>)";
    } else if (std::strcmp(name, "key") == 0) {
        // Solid key. The stroked Feather key loses its teeth entirely at the
        // 14-15px this page draws at — what survived was a ring with a diagonal
        // stub, which reads as the Mars symbol, not a key.
        svg = R"(<svg viewBox="0 0 24 24"><path fill="currentColor" fill-rule="evenodd" d="M8.2 4.2a5.8 5.8 0 1 0 0 11.6 5.8 5.8 0 0 0 0-11.6zm0 3.5a2.3 2.3 0 1 1 0 4.6 2.3 2.3 0 0 1 0-4.6z"/><path fill="currentColor" d="M13.4 8.2H22V15.2H20V11.8H17.6V14.4H15.6V11.8H13.4Z"/></svg>)";
    } else if (std::strcmp(name, "laptop") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="2" y1="20" x2="22" y2="20"/></svg>)";
    } else if (std::strcmp(name, "external-link") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/><polyline points="15 3 21 3 21 9"/><line x1="10" y1="14" x2="21" y2="3"/></svg>)";
    } else if (std::strcmp(name, "log-out") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg>)";
    } else if (std::strcmp(name, "monitor") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"/><line x1="8" y1="21" x2="16" y2="21"/><line x1="12" y1="17" x2="12" y2="21"/></svg>)";
    } else if (std::strcmp(name, "activity") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/></svg>)";
    } else if (std::strcmp(name, "headphones") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 18v-6a9 9 0 0 1 18 0v6"/><path d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"/></svg>)";
    } else if (std::strcmp(name, "layers") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>)";
    } else if (std::strcmp(name, "award") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="8" r="7"/><polyline points="8.21 13.89 7 23 12 20 17 23 15.79 13.88"/></svg>)";
    } else if (std::strcmp(name, "crown") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 4l3 12h14l3-12-6 7-4-7-4 7-6-7zm3 16h14"/></svg>)";
    } else if (std::strcmp(name, "cloud") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M18 10h-1.26A8 8 0 1 0 9 20h9a5 5 0 0 0 0-10z"/></svg>)";
    } else if (std::strcmp(name, "zap") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>)";
    } else if (std::strcmp(name, "file-badge") == 0) {
        svg = R"(<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M14.5 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7.5L14.5 2z"/><polyline points="14 2 14 8 20 8"/><circle cx="12" cy="15" r="2"/><path d="M12 13v-2"/><path d="M12 17v2"/></svg>)";
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

// Draw a leading icon + label as one row, both vertically centred on centreY.
// calculateTextY() centres the full line box against a zero-height rect at
// centreY, exactly how the SYNC-card rows position their icon (at the row centre)
// and label — those sit level to sub-pixel. Prefer this over hand-tuned Y offsets
// like `cy + 2`, which measured ~5px low against the geometrically centred icon.
inline void drawIconLabel(AestraUI::NUIRenderer& renderer, const char* iconName,
                          const std::string& label, float iconCx, float textX,
                          float centreY, float iconSize, float fontSize,
                          const AestraUI::NUIColor& iconColor,
                          const AestraUI::NUIColor& textColor) {
    drawMembershipIcon(renderer, iconName, iconCx, centreY, iconSize, iconColor);
    const AestraUI::NUIRect rowRect(textX, centreY, 0.0f, 0.0f);
    renderer.drawText(label, {textX, renderer.calculateTextY(rowRect, fontSize)}, fontSize, textColor);
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
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    m_titleLabel = std::make_shared<AestraUI::NUILabel>();
    m_titleLabel->setText("Account");
    m_titleLabel->setFontSize(theme.getFontSize("xl"));
    addChild(m_titleLabel);

    m_accountLabel = makeMembershipLabel(theme.getFontSize("m"), 0.78f);
    m_tierLabel = makeMembershipLabel(theme.getFontSize("display-s"));
    m_statusLabel = makeMembershipLabel(theme.getFontSize("m"));
    m_statusLabel->setBackgroundVisible(true);
    m_statusLabel->setBackgroundColor(AestraUI::NUIThemeManager::getInstance().getColor("accent").withAlpha(0.16f));
    m_statusLabel->setBorderVisible(true);
    m_statusLabel->setBorderColor(AestraUI::NUIThemeManager::getInstance().getColor("accent").withAlpha(0.34f));
    m_statusLabel->setBorderWidth(1.0f);
    m_verificationLabel = makeMembershipLabel(theme.getFontSize("m"), 0.72f);
    m_syncLabel = makeMembershipLabel(theme.getFontSize("m"), 0.72f);
    m_lastRefreshLabel = makeMembershipLabel(theme.getFontSize("m"), 0.72f);
    m_detailLabel = makeMembershipLabel(theme.getFontSize("m"), 0.80f);
    m_featuresTitleLabel = makeMembershipLabel(theme.getFontSize("l"));
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
        auto row = makeMembershipLabel(theme.getFontSize("m"), 0.78f);
        m_featureLabels.push_back(row);
        addChild(row);
    }

    m_signInTitleLabel = makeMembershipLabel(theme.getFontSize("l"));
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
    m_startLoginButton->setText("Send code");
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
    m_signOutButton->setText("Sign out");
    m_signOutButton->setStyle(AestraUI::NUIButton::Style::Secondary);
    m_signOutButton->setOnClick([this]() { confirmSignOut(); });
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

    m_accountLabel->setText(state.accountLabel);
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
        m_featureLabels[i]->setFontSize(AestraUI::NUIThemeManager::getInstance().getFontSize(heading ? "m" : "s"));
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
    m_accountLabel->setText("Signed out");
    m_tierLabel->setText("Core");
    m_statusLabel->setText("License services unavailable");
    m_verificationLabel->setText("Verification: Core or unverified local state");
    m_syncLabel->setText("Sync: Local/offline cache");
    m_lastRefreshLabel->setText("Last refresh: Not refreshed this session.");
    m_detailLabel->setText("Core access remains available.");
    m_featuresTitleLabel->setText("Features");
    for (std::size_t i = 0; i < m_featureLabels.size(); ++i) {
        m_featureLabels[i]->setText(i == 0 ? "+ Core DAW" : "");
        m_featureLabels[i]->setFontSize(AestraUI::NUIThemeManager::getInstance().getFontSize("s"));
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
    m_signInMessage = "Account sign-in isn't available in this build.";
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
    m_signInMessage = "Account sign-in isn't available in this build.";
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
    m_confirmPressedButton = -1;

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

// Board 2's form. FD-57: Aestra and its plugins are free, so this page is about the account, not a tier card.
void MembershipSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    auto& theme = AestraUI::NUIThemeManager::getInstance();
    const auto& props = theme.getCurrentTheme();
    const AestraUI::NUIRect b = getBounds();
    if (b.isEmpty())
        return;
    const auto textPrimary = theme.getColor("textPrimary");
    const auto textSecondary = theme.getColor("textSecondary");
    const auto textMuted = theme.getColor("textMuted");
    const auto textSuccess = theme.getColor("success");
    const auto textDanger = theme.getColor("error");

    m_form.paint(renderer);
    renderer.setClipRect(m_form.area());
    renderChildren(renderer);

    // What membership means today, in plain words.
    if (m_aboutItem != SIZE_MAX) {
        const auto about = m_form.boundsOf(m_aboutItem);
        const char* lines[] = {
            "Aestra, every plugin it makes, and Muse on your own computer are free for everyone.",
            "Supporter, when it opens, pays for what costs us money to run: shared projects,",
            "online storage and the parts of Muse that run on our servers.",
        };
        float y = about.y + 12.0f;
        for (const char* line : lines) {
            renderer.drawText(line, {about.x, y}, 12.5f, textSecondary);
            y += 18.0f;
        }
    }
    // Feedback from Send code / Verify / Refresh, under the rows it came from.
    const std::string& message = !m_signInMessage.empty() ? m_signInMessage : m_actionErrorMessage;
    if (!message.empty() && m_messageItem != SIZE_MAX) {
        const auto row = m_form.boundsOf(m_messageItem);
        const bool positive =
            message.find("sent") != std::string::npos || message.find("Signed in") != std::string::npos;
        renderer.drawText(message, {row.x, renderer.calculateTextY(row, 12.0f)}, 12.0f,
                          positive ? textSuccess : (m_actionErrorMessage == message ? textDanger : textMuted));
    }
    renderer.clearClipRect();
    (void)textPrimary;

    // --- 6. SIGN-OUT CONFIRMATION OVERLAY ---
    if (m_showingSignOutConfirm) {
        // Dim overlay
        renderer.fillRect(b, theme.getColor("overlay"));

        // Dialog
        auto& db = m_confirmDialogBounds;
        renderer.fillRoundedRect(db, props.radiusL, theme.getColor("elevatedPanel"));
        renderer.strokeRoundedRect(db, props.radiusL, props.layout.dividerWidth, theme.getColor("borderStrong"));

        float dx = db.x + 24.0f;
        float dy = db.y + 28.0f;
        renderer.drawText("Sign out of Aestra?", {dx, dy}, props.fontSizeXL, textPrimary);
        dy += 26.0f;
        renderer.drawText("Your local license cache will be cleared. You'll need to sign in",
                          {dx, dy}, props.fontSizeM, textSecondary);
        renderer.drawText("again to access your membership features.",
                          {dx, dy + 16.0f}, props.fontSizeM, textSecondary);

        // Cancel button
        {
            const auto& btn = m_confirmCancelBtnBounds;
            auto btnBg = m_confirmPressedButton == 0 ? theme.getColor("controlPressed")
                                                     : m_confirmCancelHovered ? theme.getColor("controlHover")
                                                                               : theme.getColor("controlBackground");
            renderer.fillRoundedRect(btn, props.radiusM, btnBg);
            renderer.strokeRoundedRect(btn, props.radiusM, props.layout.dividerWidth,
                                       theme.getColor("borderStrong"));
            renderer.drawTextCentered("Cancel", btn, props.fontSizeM, textSecondary);
        }

        // Sign out button (danger)
        {
            const auto& btn = m_confirmSignOutBtnBounds;
            auto btnBg = m_confirmPressedButton == 1 ? textDanger.withAlpha(0.26f)
                                                     : m_confirmSignOutHovered ? textDanger.withAlpha(0.18f)
                                                                                 : textDanger.withAlpha(0.08f);
            auto btnBorder = textDanger.withAlpha(0.45f);
            renderer.fillRoundedRect(btn, props.radiusM, btnBg);
            renderer.strokeRoundedRect(btn, props.radiusM, props.layout.dividerWidth, btnBorder);
            renderer.drawTextCentered("Sign out", btn, props.fontSizeM, textDanger);
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

        if (event.button == AestraUI::NUIMouseButton::Left && event.pressed) {
            m_confirmPressedButton = m_confirmCancelHovered ? 0
                                     : m_confirmSignOutHovered ? 1
                                     : !m_confirmDialogBounds.contains(mx, my) ? 2
                                                                                : -1;
            repaint();
            return true;
        }
        if (event.button == AestraUI::NUIMouseButton::Left && event.released) {
            const int armedButton = m_confirmPressedButton;
            m_confirmPressedButton = -1;
            if ((armedButton == 0 && m_confirmCancelHovered) ||
                (armedButton == 2 && !m_confirmDialogBounds.contains(mx, my))) {
                m_showingSignOutConfirm = false;
                repaint();
                return true;
            }
            if (armedButton == 1 && m_confirmSignOutHovered) {
                m_showingSignOutConfirm = false;
                signOut();
                if (m_onSignOutConfirmed) m_onSignOutConfirmed();
                return true;
            }
            repaint();
            return true;
        }
        return true; // Consume all mouse events while modal is open
    }

    if (AestraUI::NUIComponent::onMouseEvent(event))
        return true;
    if (m_form.onMouseWheel(event)) {
        repaint();
        return true;
    }
    return false;
}

void MembershipSettingsPage::layoutComponents() {
    // The form draws every label; these components only carry state now.
    for (auto* label : {&m_titleLabel, &m_verificationLabel, &m_syncLabel, &m_lastRefreshLabel, &m_detailLabel,
                        &m_featuresTitleLabel, &m_signInTitleLabel}) {
        if (*label)
            (*label)->setVisible(false);
    }
    for (auto& f : m_featureLabels) {
        if (f)
            f->setVisible(false);
    }
    const bool signedIn = m_signedIn;
    for (auto& c : std::initializer_list<std::shared_ptr<AestraUI::NUIComponent>>{
             m_emailInput, m_codeInput, m_startLoginButton, m_verifyLoginButton}) {
        c->setVisible(!signedIn);
    }
    for (auto& c : std::initializer_list<std::shared_ptr<AestraUI::NUIComponent>>{
             m_accountLabel, m_tierLabel, m_statusLabel, m_refreshButton, m_signOutButton}) {
        c->setVisible(signedIn);
    }

    m_form.clear();
    m_form.section("ACCOUNT");
    if (signedIn) {
        m_form.row("Signed in as", m_accountLabel, "", 320.0f);
        m_form.row("Membership", m_tierLabel, "", 220.0f);
        m_form.row("Status", m_statusLabel, "last refresh: " + m_lastRefreshValue, 220.0f);
        m_form.row("", m_refreshButton, "", 96.0f);
    } else {
        m_form.row("Email", m_emailInput, "", 260.0f);
        m_form.row("Code", m_codeInput, "", 260.0f);
    }
    m_messageItem = m_form.block(nullptr, 30.0f);
    m_form.section("WHAT IS FREE");
    m_aboutItem = m_form.block(nullptr, 70.0f);
    m_form.layout(getBounds());

    // Second buttons beside their row's control.
    const auto place = [](const std::shared_ptr<AestraUI::NUIComponent>& beside,
                          const std::shared_ptr<AestraUI::NUIComponent>& button, float width) {
        const auto& r = beside->getBounds();
        button->setBounds(AestraUI::NUIRect(r.right() + 8.0f, r.y, width, r.height));
        button->setVisible(beside->isVisible());
    };
    if (signedIn) {
        place(m_refreshButton, m_signOutButton, 96.0f);
    } else {
        place(m_emailInput, m_startLoginButton, 104.0f);
        place(m_codeInput, m_verifyLoginButton, 104.0f);
    }
}

} // namespace Aestra
