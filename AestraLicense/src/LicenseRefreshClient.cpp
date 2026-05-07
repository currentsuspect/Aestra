#include "LicenseRefreshClient.h"

#include "AestraJSON.h"
#include "HttpTransport.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <sstream>

namespace Aestra {
namespace License {

namespace {
constexpr size_t kSignatureHexLength = 128;
constexpr const char* kExpectedFormat = "aestra-license-v1";

bool isHexSignature(const std::string& value) {
    if (value.size() != kSignatureHexLength) {
        return false;
    }

    return std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    });
}

bool readRequiredString(const JSON& object, const std::string& key, std::string& out, std::string& message) {
    if (!object.has(key) || !object[key].isString()) {
        message = "Refresh response is missing string field: " + key;
        return false;
    }

    out = object[key].asString();
    if (out.empty()) {
        message = "Refresh response has empty string field: " + key;
        return false;
    }
    return true;
}

LicenseRefreshResult invalidResult(const std::string& message) {
    LicenseRefreshResult result;
    result.status = LicenseRefreshStatus::InvalidResponse;
    result.message = message;
    return result;
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

std::string nowSecondsString() {
    return std::to_string(static_cast<int64_t>(std::time(nullptr)));
}
} // namespace

LicenseRefreshResult LicenseRefreshClient::refresh(const LicenseRefreshRequest& request, IHttpTransport& transport,
                                                  const std::string& baseUrl, const std::string& sessionToken) {
    LicenseRefreshResult result;
    if (baseUrl.empty()) {
        result.status = LicenseRefreshStatus::SyncUnavailable;
        result.message = "Account API base URL is not configured.";
        return result;
    }
    if (sessionToken.empty()) {
        result.status = LicenseRefreshStatus::Unauthorized;
        result.message = "No account session is available for refresh.";
        return result;
    }

    std::string base = baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }

    std::ostringstream body;
    body << "{\"device_hash\":" << quoteJson(request.deviceHash) << ",\"issued_at\":" << nowSecondsString() << "}";

    HttpRequest httpRequest;
    httpRequest.method = "POST";
    httpRequest.url = base + "/v1/account/entitlements/refresh";
    httpRequest.body = body.str();
    httpRequest.timeoutMs = 5000;
    httpRequest.headers["content-type"] = "application/json";
    httpRequest.headers["authorization"] = "Bearer " + sessionToken;

    const HttpResponse response = transport.send(httpRequest);

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

    return parseRefreshResponse(response.body);
}

LicenseRefreshResult LicenseRefreshClient::parseRefreshResponse(const std::string& jsonText) {
    if (jsonText.empty()) {
        return invalidResult("Refresh response is empty.");
    }

    const JSON response = JSON::parse(jsonText);
    if (!response.isObject()) {
        return invalidResult("Refresh response must be a JSON object.");
    }

    LicenseRefreshResult result;
    if (!readRequiredString(response, "canonical", result.canonical, result.message)) {
        return invalidResult(result.message);
    }
    if (!readRequiredString(response, "signature_hex", result.signatureHex, result.message)) {
        return invalidResult(result.message);
    }
    if (!readRequiredString(response, "lease_blob", result.leaseBlob, result.message)) {
        return invalidResult(result.message);
    }
    if (!readRequiredString(response, "format", result.format, result.message)) {
        return invalidResult(result.message);
    }

    if (response.has("key_id")) {
        if (!response["key_id"].isString()) {
            return invalidResult("Refresh response key_id must be a string when present.");
        }
        result.keyId = response["key_id"].asString();
    }

    if (response.has("payload")) {
        if (!response["payload"].isObject()) {
            return invalidResult("Refresh response payload must be an object when present.");
        }
        result.payload = response["payload"].toString();
    }

    if (result.format != kExpectedFormat) {
        return invalidResult("Refresh response format is unsupported.");
    }
    if (!isHexSignature(result.signatureHex)) {
        return invalidResult("Refresh response signature_hex must be a 128-character hex string.");
    }
    if (result.leaseBlob != result.canonical + "\n" + result.signatureHex) {
        return invalidResult("Refresh response lease_blob does not match canonical payload and signature.");
    }

    result.status = LicenseRefreshStatus::Success;
    result.message = "Membership refresh response is usable.";
    return result;
}

bool LicenseRefreshClient::isUsableLeaseBlob(const LicenseRefreshResult& result) {
    return result.status == LicenseRefreshStatus::Success && !result.leaseBlob.empty();
}

} // namespace License
} // namespace Aestra
