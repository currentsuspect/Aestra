#include "AccountApiClient.h"

#include "AestraJSON.h"

#include <cstdlib>
#include <ctime>
#include <sstream>
#include <utility>

namespace Aestra {
namespace License {

namespace {
std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string quoteJson(const std::string& value) {
    return "\"" + escapeJsonString(value) + "\"";
}

AccountApiStatus statusFromHttp(const HttpResponse& response) {
    if (response.status == 0) {
        return AccountApiStatus::NetworkUnavailable;
    }
    if (response.status == 401 || response.status == 403) {
        return AccountApiStatus::Unauthorized;
    }
    if (response.status >= 500) {
        return AccountApiStatus::ServerError;
    }
    if (response.status < 200 || response.status >= 300) {
        return AccountApiStatus::InvalidResponse;
    }
    return AccountApiStatus::Success;
}

bool readString(const JSON& json, const std::string& key, std::string& out) {
    if (!json.has(key) || !json[key].isString()) {
        return false;
    }
    out = json[key].asString();
    return true;
}

bool readInteger(const JSON& json, const std::string& key, int64_t& out) {
    if (!json.has(key) || !json[key].isNumber()) {
        return false;
    }
    out = static_cast<int64_t>(json[key].asNumber());
    return true;
}

std::string nowSecondsString() {
    return std::to_string(static_cast<int64_t>(std::time(nullptr)));
}
} // namespace

AccountApiClient::AccountApiClient(AccountApiConfig config, IHttpTransport& transport)
    : m_config(std::move(config)), m_transport(transport) {
    m_config.baseUrl = trimTrailingSlash(m_config.baseUrl);
}

HttpResponse AccountApiClient::sendJson(const std::string& method, const std::string& path, const std::string& body,
                                        const std::string& bearerToken) {
    HttpResponse unavailable;
    if (m_config.baseUrl.empty()) {
        unavailable.status = 0;
        unavailable.error = "Account API base URL is not configured.";
        return unavailable;
    }

    HttpRequest request;
    request.method = method;
    request.url = m_config.baseUrl + path;
    request.body = body;
    request.timeoutMs = m_config.timeoutMs;
    request.headers["content-type"] = "application/json";
    if (!bearerToken.empty()) {
        request.headers["authorization"] = "Bearer " + bearerToken;
    }
    return m_transport.send(request);
}

LoginStartResult AccountApiClient::loginStart(const std::string& email) {
    LoginStartResult result;
    const HttpResponse response = sendJson("POST", "/v1/account/login/start", "{\"email\":" + quoteJson(email) + "}");
    result.status = statusFromHttp(response);
    result.message = response.error;
    if (result.status != AccountApiStatus::Success) {
        return result;
    }

    JSON json = JSON::parse(response.body);
    if (!json.isObject() || !readString(json, "challenge_id", result.challengeId) ||
        !readInteger(json, "expires_at", result.expiresAt)) {
        result.status = AccountApiStatus::InvalidResponse;
        result.message = "Login start response is malformed.";
    }
    return result;
}

LoginVerifyResult AccountApiClient::loginVerify(const std::string& email, const std::string& challengeId,
                                                const std::string& code) {
    LoginVerifyResult result;
    const std::string body = "{\"email\":" + quoteJson(email) + ",\"challenge_id\":" + quoteJson(challengeId) +
                             ",\"code\":" + quoteJson(code) + "}";
    const HttpResponse response = sendJson("POST", "/v1/account/login/verify", body);
    result.status = statusFromHttp(response);
    result.message = response.error;
    if (result.status != AccountApiStatus::Success) {
        return result;
    }

    JSON json = JSON::parse(response.body);
    if (!json.isObject() || !json.has("account") || !json["account"].isObject() || !json.has("session") ||
        !json["session"].isObject()) {
        result.status = AccountApiStatus::InvalidResponse;
        result.message = "Login verify response is malformed.";
        return result;
    }

    JSON& account = json["account"];
    JSON& session = json["session"];
    if (!readString(account, "id", result.identity.userId) || !readString(account, "email", result.identity.email) ||
        !readString(session, "token", result.sessionToken) ||
        !readInteger(session, "expires_at", result.sessionExpiresAt)) {
        result.status = AccountApiStatus::InvalidResponse;
        result.message = "Login verify response is missing required fields.";
        return result;
    }
    result.identity.displayName.clear();
    result.identity.avatarUrl.clear();
    return result;
}

AccountMeResult AccountApiClient::me(const std::string& sessionToken) {
    AccountMeResult result;
    const HttpResponse response = sendJson("GET", "/v1/account/me", "", sessionToken);
    result.status = statusFromHttp(response);
    result.message = response.error;
    if (result.status != AccountApiStatus::Success) {
        return result;
    }

    JSON json = JSON::parse(response.body);
    if (!json.isObject() || !json.has("account") || !json["account"].isObject()) {
        result.status = AccountApiStatus::InvalidResponse;
        result.message = "Account me response is malformed.";
        return result;
    }

    JSON& account = json["account"];
    if (!readString(account, "id", result.identity.userId)) {
        result.status = AccountApiStatus::InvalidResponse;
        result.message = "Account me response is missing account id.";
        return result;
    }
    if (account.has("email") && account["email"].isString()) {
        result.identity.email = account["email"].asString();
    }
    if (json.has("entitlement") && json["entitlement"].isObject() && json["entitlement"].has("tier") &&
        json["entitlement"]["tier"].isString()) {
        result.tier = json["entitlement"]["tier"].asString();
    }
    return result;
}

AccountRevokeResult AccountApiClient::revoke(const std::string& sessionToken) {
    AccountRevokeResult result;
    const HttpResponse response = sendJson("POST", "/v1/account/session/revoke", "{}", sessionToken);
    result.status = statusFromHttp(response);
    result.message = response.error;
    return result;
}

LicenseRefreshResult AccountApiClient::refreshEntitlements(const std::string& sessionToken,
                                                           const std::string& deviceHash) {
    const std::string body = "{\"device_hash\":" + quoteJson(deviceHash) + ",\"issued_at\":" + nowSecondsString() + "}";
    const HttpResponse response = sendJson("POST", "/v1/account/entitlements/refresh", body, sessionToken);

    LicenseRefreshResult result;
    if (response.status == 0) {
        result.status = LicenseRefreshStatus::SyncUnavailable;
        result.message = response.error.empty() ? "Account refresh transport unavailable." : response.error;
        return result;
    }
    if (response.status == 401 || response.status == 403) {
        result.status = LicenseRefreshStatus::Unauthorized;
        result.message = "Account session is not authorized.";
        return result;
    }
    if (response.status >= 500) {
        result.status = LicenseRefreshStatus::SyncUnavailable;
        result.message = "Account refresh server error.";
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.status = LicenseRefreshStatus::InvalidResponse;
        result.message = "Account refresh request failed.";
        return result;
    }
    return LicenseRefreshClient::parseRefreshResponse(response.body);
}

AccountApiConfig accountApiConfigFromEnvironment() {
    AccountApiConfig config;
    // Default to Aestra's home. Use the canonical www host directly: the apex
    // (aestra.studio) 307-redirects to www, and pointing here avoids that hop.
    // AESTRA_ACCOUNT_API_BASE_URL overrides it (staging / local mock).
    config.baseUrl = "https://www.aestra.studio";
    if (const char* envUrl = std::getenv("AESTRA_ACCOUNT_API_BASE_URL")) {
        config.baseUrl = envUrl;
    }
    return config;
}

} // namespace License
} // namespace Aestra
