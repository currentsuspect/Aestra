#include "LicenseRefreshClient.h"

#include "AestraJSON.h"

#include <algorithm>
#include <cstddef>

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
} // namespace

LicenseRefreshResult LicenseRefreshClient::refresh(const LicenseRefreshRequest& request) const {
    (void)request;

    LicenseRefreshResult result;
    result.status = LicenseRefreshStatus::SyncUnavailable;
    result.message = "Membership refresh transport is not configured in this build.";
    return result;
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
