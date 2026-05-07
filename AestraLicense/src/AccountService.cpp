#include "AccountService.h"

#include "LicenseGate.h"

#include <ctime>

namespace Aestra {
namespace License {

namespace {
AccountServiceStatus fromApi(AccountApiStatus status) {
    switch (status) {
    case AccountApiStatus::Success:
        return AccountServiceStatus::Success;
    case AccountApiStatus::Unauthorized:
        return AccountServiceStatus::Unauthorized;
    case AccountApiStatus::NetworkUnavailable:
    case AccountApiStatus::ServerError:
        return AccountServiceStatus::SyncUnavailable;
    case AccountApiStatus::InvalidResponse:
        return AccountServiceStatus::InvalidResponse;
    case AccountApiStatus::NotConfigured:
    default:
        return AccountServiceStatus::NotConfigured;
    }
}

AccountServiceStatus fromRefresh(LicenseRefreshStatus status) {
    switch (status) {
    case LicenseRefreshStatus::Success:
        return AccountServiceStatus::Success;
    case LicenseRefreshStatus::Unauthorized:
        return AccountServiceStatus::Unauthorized;
    case LicenseRefreshStatus::SyncUnavailable:
        return AccountServiceStatus::SyncUnavailable;
    case LicenseRefreshStatus::RejectedSignature:
        return AccountServiceStatus::RejectedSignature;
    case LicenseRefreshStatus::InvalidResponse:
    default:
        return AccountServiceStatus::InvalidResponse;
    }
}

int64_t nowUnixSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}
} // namespace

AccountService::AccountService(AccountApiClient& api, LocalAccountCache& cache, ILeaseInstaller& leaseInstaller)
    : m_api(api), m_cache(cache), m_leaseInstaller(leaseInstaller) {}

AccountLoginStartServiceResult AccountService::loginStart(const std::string& email) {
    AccountLoginStartServiceResult out;
    if (email.empty()) {
        out.status = AccountServiceStatus::InvalidResponse;
        out.message = "Email is required.";
        return out;
    }

    const LoginStartResult result = m_api.loginStart(email);
    out.status = fromApi(result.status);
    out.message = result.message;
    out.challengeId = result.challengeId;
    out.expiresAt = result.expiresAt;
    return out;
}

AccountServiceResult AccountService::saveSession(const LoginVerifyResult& login) {
    AccountServiceResult out;
    if (login.identity.userId.empty() || login.sessionToken.empty()) {
        out.status = AccountServiceStatus::InvalidResponse;
        out.message = "Login response is missing account session fields.";
        return out;
    }

    LocalAccountRecord record;
    record.identity = login.identity;
    record.state = AccountSessionState::SignedInFresh;
    record.lastSyncUnix = nowUnixSeconds();
    record.sessionToken = login.sessionToken;
    record.hasIdentity = true;
    if (!m_cache.save(record)) {
        out.status = AccountServiceStatus::CacheWriteFailed;
        out.message = "Unable to persist account session.";
        return out;
    }

    out.status = AccountServiceStatus::Success;
    return out;
}

AccountServiceResult AccountService::loginVerify(const std::string& email, const std::string& challengeId,
                                                 const std::string& code) {
    const LoginVerifyResult result = m_api.loginVerify(email, challengeId, code);
    AccountServiceResult out;
    out.status = fromApi(result.status);
    out.message = result.message;
    if (out.status != AccountServiceStatus::Success) {
        return out;
    }
    return saveSession(result);
}

AccountServiceResult AccountService::loadAccount() {
    AccountServiceResult out;
    const LocalAccountCacheLoadResult cached = m_cache.load();
    if (cached.status != LocalAccountCacheLoadStatus::Loaded || cached.record.sessionToken.empty()) {
        out.status = AccountServiceStatus::Unauthorized;
        out.message = "No account session is available.";
        return out;
    }

    const AccountMeResult result = m_api.me(cached.record.sessionToken);
    out.status = fromApi(result.status);
    out.message = result.message;
    if (out.status == AccountServiceStatus::Unauthorized) {
        m_cache.clear();
    }
    return out;
}

AccountServiceResult AccountService::refreshEntitlements() {
    AccountServiceResult out;
    const LocalAccountCacheLoadResult cached = m_cache.load();
    if (cached.status != LocalAccountCacheLoadStatus::Loaded || cached.record.sessionToken.empty()) {
        out.status = AccountServiceStatus::Unauthorized;
        out.message = "No account session is available.";
        return out;
    }

    const LicenseRefreshResult refresh =
        m_api.refreshEntitlements(cached.record.sessionToken, currentDeviceHashForRefresh());
    out.status = fromRefresh(refresh.status);
    out.message = refresh.message;
    if (refresh.status == LicenseRefreshStatus::Unauthorized) {
        m_cache.clear();
        return out;
    }
    if (refresh.status != LicenseRefreshStatus::Success) {
        return out;
    }

    std::string installMessage;
    if (!m_leaseInstaller.installLeaseBlob(refresh.leaseBlob, installMessage)) {
        out.status = AccountServiceStatus::RejectedSignature;
        out.message = installMessage;
        return out;
    }

    LocalAccountRecord updated = cached.record;
    updated.state = AccountSessionState::SignedInFresh;
    updated.lastSyncUnix = nowUnixSeconds();
    if (!m_cache.save(updated)) {
        out.status = AccountServiceStatus::CacheWriteFailed;
        out.message = "Unable to persist refreshed account session.";
        return out;
    }

    out.status = AccountServiceStatus::Success;
    out.message = "Membership refreshed.";
    return out;
}

AccountServiceResult AccountService::revokeSession(bool clearLocalWhenOffline) {
    AccountServiceResult out;
    const LocalAccountCacheLoadResult cached = m_cache.load();
    if (cached.status != LocalAccountCacheLoadStatus::Loaded || cached.record.sessionToken.empty()) {
        m_cache.clear();
        out.status = AccountServiceStatus::Success;
        return out;
    }

    const AccountRevokeResult result = m_api.revoke(cached.record.sessionToken);
    out.status = fromApi(result.status);
    out.message = result.message;
    if (out.status == AccountServiceStatus::Success || out.status == AccountServiceStatus::Unauthorized ||
        (clearLocalWhenOffline && out.status == AccountServiceStatus::SyncUnavailable)) {
        m_cache.clear();
    }
    return out;
}

} // namespace License
} // namespace Aestra
