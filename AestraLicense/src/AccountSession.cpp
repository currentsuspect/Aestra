#include "AccountSession.h"

#include "EntitlementStore.h"
#include "LocalAccountCache.h"

#include <algorithm>
#include <chrono>
#include <ctime>

namespace Aestra {
namespace License {

namespace {
constexpr int64_t kFreshSyncWindowSeconds = 3600;

int64_t nowUnixSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

bool isVerifiedEntitlement(EntitlementStatus status) {
    return status == EntitlementStatus::Valid || status == EntitlementStatus::Grace;
}

EntitlementProfile coreEntitlementWithStatus(EntitlementStatus status) {
    EntitlementProfile entitlement;
    entitlement.tier = MembershipTier::Core;
    entitlement.status = status;
    entitlement.verified = false;
    entitlement.offline = true;
    return entitlement;
}

std::string messageForEntitlement(const EntitlementProfile& entitlement) {
    switch (entitlement.status) {
    case EntitlementStatus::Valid:
        return "Membership verified.";
    case EntitlementStatus::Grace:
        return "Membership is available from offline cache.";
    case EntitlementStatus::Expired:
        return "Membership cache is expired; Core access remains available.";
    case EntitlementStatus::InvalidSignature:
        return "Membership signature is invalid; Core access remains available.";
    case EntitlementStatus::WrongDevice:
        return "Membership cache belongs to another device; Core access remains available.";
    case EntitlementStatus::Revoked:
        return "Membership cache was revoked; Core access remains available.";
    case EntitlementStatus::ParseError:
        return "Membership cache could not be read; Core access remains available.";
    case EntitlementStatus::Missing:
        return "No signed membership cache is available; Core access remains available.";
    case EntitlementStatus::Unknown:
    default:
        return "Membership status is unknown; Core access remains available.";
    }
}
} // namespace

AccountSession::AccountSession(LocalAccountCache& cache, EntitlementStore& entitlements)
    : m_cache(cache), m_entitlements(entitlements), m_snapshot(buildSnapshot()) {}

AccountSessionSnapshot AccountSession::current() const {
    return buildSnapshot();
}

bool AccountSession::load() {
    m_snapshot = buildSnapshot();
    return m_snapshot.state != AccountSessionState::Invalid;
}

bool AccountSession::saveDisplayIdentity(const AccountIdentity& identity) {
    if (identity.userId.empty()) {
        return false;
    }

    LocalAccountRecord record;
    record.identity = identity;
    record.state = AccountSessionState::SignedInFresh;
    record.lastSyncUnix = nowUnixSeconds();
    record.hasIdentity = true;
    const bool saved = m_cache.save(record);
    m_snapshot = buildSnapshot();
    return saved;
}

void AccountSession::signOut() {
    m_cache.clear();
    m_snapshot = buildSnapshot();
}

AccountRefreshResult AccountSession::refreshAsync() {
    m_snapshot = buildSnapshot();
    m_snapshot.state = AccountSessionState::SyncUnavailable;
    m_snapshot.canAttemptRefresh = true;
    m_snapshot.offline = true;
    m_snapshot.statusMessage = "Account refresh is not implemented in this build.";
    return AccountRefreshResult::SyncUnavailable;
}

bool AccountSession::isSignedIn() const {
    return current().signedIn;
}

AccountSessionState AccountSession::state() const {
    return current().state;
}

AccountSessionSnapshot AccountSession::buildSnapshot() const {
    AccountSessionSnapshot snapshot;
    snapshot.entitlement = m_entitlements.currentProfile();
    snapshot.offline = snapshot.entitlement.offline;
    snapshot.canAttemptRefresh = true;

    const LocalAccountCacheLoadResult cached = m_cache.load();
    if (cached.status == LocalAccountCacheLoadStatus::Missing) {
        snapshot.state = AccountSessionState::SignedOut;
        snapshot.signedIn = false;
        snapshot.canAttemptRefresh = false;
        snapshot.statusMessage = "Signed out. Core access remains available.";
        return snapshot;
    }

    if (cached.status == LocalAccountCacheLoadStatus::Malformed || !cached.record.hasIdentity) {
        snapshot.state = AccountSessionState::Invalid;
        snapshot.entitlement = coreEntitlementWithStatus(EntitlementStatus::ParseError);
        snapshot.signedIn = false;
        snapshot.statusMessage = "Local account cache is invalid; Core access remains available.";
        return snapshot;
    }

    snapshot.identity = cached.record.identity;
    snapshot.signedIn = true;

    if (!snapshot.entitlement.userId.empty() && snapshot.entitlement.userId != snapshot.identity.userId) {
        snapshot.state = AccountSessionState::Invalid;
        snapshot.signedIn = false;
        snapshot.statusMessage = "Local account identity does not match signed membership cache.";
        return snapshot;
    }

    if (!isVerifiedEntitlement(snapshot.entitlement.status)) {
        snapshot.state = snapshot.entitlement.status == EntitlementStatus::Expired
                             ? AccountSessionState::Expired
                             : AccountSessionState::SignedInCached;
        snapshot.statusMessage = messageForEntitlement(snapshot.entitlement);
        return snapshot;
    }

    const int64_t ageSeconds = std::max<int64_t>(0, nowUnixSeconds() - cached.record.lastSyncUnix);
    snapshot.state = ageSeconds <= kFreshSyncWindowSeconds ? AccountSessionState::SignedInFresh
                                                           : AccountSessionState::SignedInCached;
    snapshot.statusMessage = messageForEntitlement(snapshot.entitlement);
    return snapshot;
}

} // namespace License
} // namespace Aestra
