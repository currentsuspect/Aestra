#pragma once

#include "AccountApiClient.h"
#include "LocalAccountCache.h"

namespace Aestra {
namespace License {

enum class AccountServiceStatus {
    Success = 0,
    NotConfigured,
    SyncUnavailable,
    Unauthorized,
    InvalidResponse,
    RejectedSignature,
    CacheWriteFailed,
};

struct AccountServiceResult {
    AccountServiceStatus status = AccountServiceStatus::NotConfigured;
    std::string message;
};

struct AccountLoginStartServiceResult : AccountServiceResult {
    std::string challengeId;
    int64_t expiresAt = 0;
};

class AccountService {
public:
    AccountService(AccountApiClient& api, LocalAccountCache& cache, ILeaseInstaller& leaseInstaller);

    AccountLoginStartServiceResult loginStart(const std::string& email);
    AccountServiceResult loginVerify(const std::string& email, const std::string& challengeId, const std::string& code);
    AccountServiceResult refreshEntitlements();
    AccountServiceResult loadAccount();
    AccountServiceResult revokeSession(bool clearLocalWhenOffline);

private:
    AccountApiClient& m_api;
    LocalAccountCache& m_cache;
    ILeaseInstaller& m_leaseInstaller;

    AccountServiceResult saveSession(const LoginVerifyResult& login);
};

} // namespace License
} // namespace Aestra
