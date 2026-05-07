#include "MembershipViewModel.h"

#include <array>
#include <sstream>
#include <utility>

namespace Aestra {
namespace License {

namespace {
bool isVerifiedEntitlement(EntitlementStatus status) {
    return status == EntitlementStatus::Valid || status == EntitlementStatus::Grace;
}

MembershipDisplayTier displayTierFromMembership(MembershipTier tier) {
    switch (tier) {
    case MembershipTier::Supporter:
        return MembershipDisplayTier::Supporter;
    case MembershipTier::Founder:
        return MembershipDisplayTier::Founder;
    case MembershipTier::Core:
    default:
        return MembershipDisplayTier::Core;
    }
}

MembershipDisplayStatus displayStatusFromSnapshot(const AccountSessionSnapshot& snapshot) {
    if (snapshot.state == AccountSessionState::SyncUnavailable) {
        return MembershipDisplayStatus::SyncUnavailable;
    }
    if (snapshot.state == AccountSessionState::SignedOut) {
        return MembershipDisplayStatus::SignedOut;
    }
    if (snapshot.state == AccountSessionState::Invalid) {
        return MembershipDisplayStatus::Invalid;
    }

    switch (snapshot.entitlement.status) {
    case EntitlementStatus::Valid:
        return MembershipDisplayStatus::Verified;
    case EntitlementStatus::Grace:
        return MembershipDisplayStatus::CachedOffline;
    case EntitlementStatus::Expired:
        return MembershipDisplayStatus::Expired;
    case EntitlementStatus::Missing:
        return MembershipDisplayStatus::Missing;
    case EntitlementStatus::Revoked:
        return MembershipDisplayStatus::Revoked;
    case EntitlementStatus::WrongDevice:
        return MembershipDisplayStatus::WrongDevice;
    case EntitlementStatus::ParseError:
        return MembershipDisplayStatus::ParseError;
    case EntitlementStatus::InvalidSignature:
    case EntitlementStatus::Unknown:
    default:
        return MembershipDisplayStatus::Invalid;
    }
}

std::string accountLabelFromSnapshot(const AccountSessionSnapshot& snapshot) {
    if (!snapshot.signedIn) {
        return "Signed out";
    }
    if (!snapshot.identity.displayName.empty() && !snapshot.identity.email.empty()) {
        return snapshot.identity.displayName + " <" + snapshot.identity.email + ">";
    }
    if (!snapshot.identity.displayName.empty()) {
        return snapshot.identity.displayName;
    }
    if (!snapshot.identity.email.empty()) {
        return snapshot.identity.email;
    }
    if (!snapshot.identity.userId.empty()) {
        return snapshot.identity.userId;
    }
    return "Cached account";
}

std::string detailForStatus(MembershipDisplayStatus status) {
    switch (status) {
    case MembershipDisplayStatus::Verified:
        return "Membership is verified locally.";
    case MembershipDisplayStatus::CachedOffline:
        return "Membership is available from a signed offline cache.";
    case MembershipDisplayStatus::SyncUnavailable:
        return "Sync is unavailable in this build; local membership state is shown below.";
    case MembershipDisplayStatus::Expired:
        return "Membership cache is expired. Core access remains available.";
    case MembershipDisplayStatus::Invalid:
        return "Membership cache is invalid. Core access remains available.";
    case MembershipDisplayStatus::Missing:
        return "No signed membership cache is available. Core access remains available.";
    case MembershipDisplayStatus::Revoked:
        return "Membership cache was revoked. Core access remains available.";
    case MembershipDisplayStatus::WrongDevice:
        return "Membership cache belongs to another device. Core access remains available.";
    case MembershipDisplayStatus::ParseError:
        return "Membership cache could not be read. Core access remains available.";
    case MembershipDisplayStatus::SignedOut:
    default:
        return "Signed out. Core access remains available.";
    }
}

std::string unavailableReason(ProductFeature feature, MembershipDisplayStatus status) {
    if (status != MembershipDisplayStatus::Verified && status != MembershipDisplayStatus::CachedOffline &&
        status != MembershipDisplayStatus::SyncUnavailable) {
        return "Requires a verified signed membership cache.";
    }

    switch (feature) {
    case ProductFeature::Rumble:
        return "Requires Supporter or Founder membership.";
    case ProductFeature::RumbleHeadless:
    case ProductFeature::FounderBadge:
        return "Requires Founder membership.";
    case ProductFeature::PremiumPluginBundle:
    case ProductFeature::SupporterBadge:
        return "Requires Supporter or Founder membership.";
    case ProductFeature::CloudSync:
    case ProductFeature::EarlyAccess:
        return "Not available yet.";
    case ProductFeature::CoreDAW:
    default:
        return "";
    }
}

struct FeatureDefinition {
    ProductFeature feature;
    const char* label;
};

constexpr std::array<FeatureDefinition, 8> kFeatureDefinitions = {
    FeatureDefinition{ProductFeature::CoreDAW, "Core DAW"},
    FeatureDefinition{ProductFeature::Rumble, "Aestra Rumble"},
    FeatureDefinition{ProductFeature::RumbleHeadless, "Rumble Headless"},
    FeatureDefinition{ProductFeature::PremiumPluginBundle, "Premium Plugin Bundle"},
    FeatureDefinition{ProductFeature::SupporterBadge, "Supporter Badge"},
    FeatureDefinition{ProductFeature::FounderBadge, "Founder Badge"},
    FeatureDefinition{ProductFeature::CloudSync, "Cloud Sync"},
    FeatureDefinition{ProductFeature::EarlyAccess, "Early Access"},
};
} // namespace

std::string membershipDisplayTierLabel(MembershipDisplayTier tier) {
    switch (tier) {
    case MembershipDisplayTier::Supporter:
        return "Aestra Supporter";
    case MembershipDisplayTier::Founder:
        return "Aestra Founder";
    case MembershipDisplayTier::Core:
    default:
        return "Aestra Core";
    }
}

std::string membershipDisplayStatusLabel(MembershipDisplayStatus status) {
    switch (status) {
    case MembershipDisplayStatus::Verified:
        return "Verified";
    case MembershipDisplayStatus::CachedOffline:
        return "Cached offline";
    case MembershipDisplayStatus::SyncUnavailable:
        return "Sync unavailable";
    case MembershipDisplayStatus::Expired:
        return "Expired";
    case MembershipDisplayStatus::Invalid:
        return "Invalid";
    case MembershipDisplayStatus::Missing:
        return "Missing";
    case MembershipDisplayStatus::Revoked:
        return "Revoked";
    case MembershipDisplayStatus::WrongDevice:
        return "Wrong device";
    case MembershipDisplayStatus::ParseError:
        return "Unreadable cache";
    case MembershipDisplayStatus::SignedOut:
    default:
        return "Signed out";
    }
}

std::string membershipDisplaySummary(const MembershipViewState& state, const std::string& lastRefreshMessage) {
    std::ostringstream out;
    out << "Account: " << state.accountLabel << "\n"
        << "Tier: " << state.tierLabel << "\n"
        << "Status: " << state.statusLabel << "\n"
        << "Verification: " << (state.verified ? "Verified signed lease" : "Core or unverified local state") << "\n"
        << "Sync: " << (state.offline ? "Local/offline cache" : "Online") << "\n";
    if (!lastRefreshMessage.empty()) {
        out << "Last refresh: " << lastRefreshMessage << "\n";
    }
    out << "\n" << state.detailMessage << "\n\n"
        << "Features\n";
    for (const MembershipFeatureRow& row : state.features) {
        out << (row.enabled ? "Available: " : "Unavailable: ") << row.label;
        if (!row.enabled && !row.reason.empty()) {
            out << " - " << row.reason;
        }
        out << "\n";
    }
    return out.str();
}

std::string membershipBadgeTierText(const MembershipViewState& state) {
    if (state.tierLabel == "Aestra Founder") {
        return "Founder";
    }
    if (state.tierLabel == "Aestra Supporter") {
        return "Supporter";
    }
    return "Core";
}

std::string membershipBadgeStatusText(const MembershipViewState& state) {
    if (state.statusLabel == "Verified") {
        return "Verified";
    }
    if (state.statusLabel == "Cached offline") {
        return "Offline";
    }
    if (state.statusLabel == "Sync unavailable") {
        return "Sync unavailable";
    }
    if (state.statusLabel == "Signed out") {
        return "Signed out";
    }
    if (state.statusLabel == "Expired" || state.statusLabel == "Revoked" || state.statusLabel == "Invalid" ||
        state.statusLabel == "Wrong device" || state.statusLabel == "Unreadable cache") {
        return state.statusLabel;
    }
    return state.statusLabel.empty() ? "Unknown" : state.statusLabel;
}

MembershipViewModel::MembershipViewModel(AccountSession& session, EntitlementStore& entitlements)
    : m_session(session), m_entitlements(entitlements) {}

MembershipViewState MembershipViewModel::current() const {
    const AccountSessionSnapshot snapshot = m_session.current();
    const EntitlementProfile entitlement = m_entitlements.currentProfile();
    const MembershipDisplayStatus displayStatus = displayStatusFromSnapshot(snapshot);
    const bool verified = isVerifiedEntitlement(entitlement.status) && entitlement.verified;
    const MembershipDisplayTier displayTier =
        verified ? displayTierFromMembership(entitlement.tier) : MembershipDisplayTier::Core;

    MembershipViewState state;
    state.tierLabel = membershipDisplayTierLabel(displayTier);
    state.statusLabel = membershipDisplayStatusLabel(displayStatus);
    state.accountLabel = accountLabelFromSnapshot(snapshot);
    state.detailMessage = detailForStatus(displayStatus);
    state.signedIn = snapshot.signedIn;
    state.verified = verified;
    state.offline = snapshot.offline;
    state.canRefresh = snapshot.canAttemptRefresh;
    state.canSignOut = snapshot.signedIn;

    // Display only: UI reads this projection, while access remains EntitlementStore-backed.
    state.features.reserve(kFeatureDefinitions.size());
    for (const FeatureDefinition& definition : kFeatureDefinitions) {
        MembershipFeatureRow row;
        row.label = definition.label;
        row.enabled = m_entitlements.canAccess(definition.feature);
        if (!row.enabled) {
            row.reason = unavailableReason(definition.feature, displayStatus);
        }
        state.features.push_back(std::move(row));
    }

    return state;
}

std::string MembershipViewModel::tierLabel() const {
    return current().tierLabel;
}

std::string MembershipViewModel::statusLabel() const {
    return current().statusLabel;
}

std::string MembershipViewModel::detailMessage() const {
    return current().detailMessage;
}

bool MembershipViewModel::canAccess(ProductFeature feature) const {
    return m_entitlements.canAccess(feature);
}

} // namespace License
} // namespace Aestra
