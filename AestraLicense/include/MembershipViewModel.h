#pragma once

#include "AccountSession.h"
#include "EntitlementStore.h"

#include <string>
#include <vector>

namespace Aestra {
namespace License {

enum class MembershipDisplayTier {
    Core,
    Supporter,
    Founder,
};

enum class MembershipDisplayStatus {
    SignedOut,
    Verified,
    CachedOffline,
    SyncUnavailable,
    Expired,
    Invalid,
    Missing,
    Revoked,
    WrongDevice,
    ParseError,
};

struct MembershipFeatureRow {
    std::string label;
    bool enabled = false;
    std::string reason;
};

struct MembershipViewState {
    std::string tierLabel;
    std::string statusLabel;
    std::string accountLabel;
    std::string detailMessage;
    bool signedIn = false;
    bool verified = false;
    bool offline = true;
    bool canRefresh = false;
    bool canSignOut = false;
    std::vector<MembershipFeatureRow> features;
};

class MembershipViewModel {
public:
    MembershipViewModel(AccountSession& session, EntitlementStore& entitlements);

    MembershipViewState current() const;

    std::string tierLabel() const;
    std::string statusLabel() const;
    std::string detailMessage() const;

    bool canAccess(ProductFeature feature) const;

private:
    AccountSession& m_session;
    EntitlementStore& m_entitlements;
};

std::string membershipDisplayTierLabel(MembershipDisplayTier tier);
std::string membershipDisplayStatusLabel(MembershipDisplayStatus status);
std::string membershipDisplaySummary(const MembershipViewState& state, const std::string& lastRefreshMessage = "");
std::string membershipBadgeTierText(const MembershipViewState& state);
std::string membershipBadgeStatusText(const MembershipViewState& state);

} // namespace License
} // namespace Aestra
