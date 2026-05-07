#pragma once

#include "AccountSession.h"
#include "HttpTransport.h"
#include "LicenseRefreshClient.h"

#include <memory>
#include <string>

namespace Aestra {
namespace License {

enum class AccountApiStatus {
    Success = 0,
    NotConfigured,
    NetworkUnavailable,
    Unauthorized,
    ServerError,
    InvalidResponse,
};

struct AccountApiConfig {
    std::string baseUrl;
    int timeoutMs = 5000;
};

struct LoginStartResult {
    AccountApiStatus status = AccountApiStatus::NotConfigured;
    std::string challengeId;
    int64_t expiresAt = 0;
    std::string message;
};

struct LoginVerifyResult {
    AccountApiStatus status = AccountApiStatus::NotConfigured;
    AccountIdentity identity;
    std::string sessionToken;
    int64_t sessionExpiresAt = 0;
    std::string message;
};

struct AccountMeResult {
    AccountApiStatus status = AccountApiStatus::NotConfigured;
    AccountIdentity identity;
    std::string tier;
    std::string message;
};

struct AccountRevokeResult {
    AccountApiStatus status = AccountApiStatus::NotConfigured;
    std::string message;
};

class AccountApiClient {
public:
    AccountApiClient(AccountApiConfig config, IHttpTransport& transport);

    LoginStartResult loginStart(const std::string& email);
    LoginVerifyResult loginVerify(const std::string& email, const std::string& challengeId, const std::string& code);
    AccountMeResult me(const std::string& sessionToken);
    AccountRevokeResult revoke(const std::string& sessionToken);
    LicenseRefreshResult refreshEntitlements(const std::string& sessionToken, const std::string& deviceHash);

    const AccountApiConfig& config() const { return m_config; }

private:
    AccountApiConfig m_config;
    IHttpTransport& m_transport;

    HttpResponse sendJson(const std::string& method, const std::string& path, const std::string& body,
                          const std::string& bearerToken = "");
};

AccountApiConfig accountApiConfigFromEnvironment();

} // namespace License
} // namespace Aestra
