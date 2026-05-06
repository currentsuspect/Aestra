#pragma once

#include "EntitlementProfile.h"

#include <string>

namespace Aestra {
namespace License {

class EntitlementStore;
class LocalAccountCache;

struct AccountIdentity {
    std::string userId;
    std::string email;
    std::string displayName;
    std::string avatarUrl;
};

enum class AccountSessionState {
    SignedOut = 0,
    SignedInCached,
    SignedInFresh,
    Expired,
    Invalid,
    SyncUnavailable,
};

enum class AccountRefreshResult {
    SyncUnavailable = 0,
};

struct AccountSessionSnapshot {
    AccountSessionState state = AccountSessionState::SignedOut;
    AccountIdentity identity;
    EntitlementProfile entitlement;
    bool signedIn = false;
    bool canAttemptRefresh = false;
    bool offline = true;
    std::string statusMessage;
};

class AccountSession {
public:
    AccountSession(LocalAccountCache& cache, EntitlementStore& entitlements);

    AccountSessionSnapshot current() const;
    AccountSessionSnapshot cachedSnapshot() const { return m_snapshot; }
    bool load();
    bool saveDisplayIdentity(const AccountIdentity& identity);
    void signOut();
    AccountRefreshResult refreshAsync();

    bool isSignedIn() const;
    AccountSessionState state() const;

private:
    LocalAccountCache& m_cache;
    EntitlementStore& m_entitlements;
    AccountSessionSnapshot m_snapshot;

    AccountSessionSnapshot buildSnapshot() const;
};

} // namespace License
} // namespace Aestra
