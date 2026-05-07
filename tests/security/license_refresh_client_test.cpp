#include "LicenseRefreshClient.h"
#include "HttpTransport.h"

#include <iostream>
#include <string>

using namespace Aestra::License;

namespace {
constexpr const char* kCanonical =
    "{\"license_id\":\"lic_refresh_1\",\"user_id\":\"user_refresh_1\",\"tier\":\"Supporter\",\"plugins\":[],"
    "\"features\":[\"rumble\"],\"device_hash\":\"device_refresh_1\",\"issued_at\":1700000000,"
    "\"expires_at\":1700604800,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}";

constexpr const char* kSignatureHex = "6570b63ace3cd4848ce82aa5afe4aa37120a86e6c31f44eef5dfaab350fe29d"
                                      "165476c64f1bfb1741b4ff4b2db1d44c9866edb96e1cf974be24c7b424c575506";

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
        return false;
    }
    return true;
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
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string responseJson(const std::string& canonical = kCanonical, const std::string& signatureHex = kSignatureHex,
                         const std::string& leaseBlob = "") {
    const std::string blob = leaseBlob.empty() ? canonical + "\n" + signatureHex : leaseBlob;
    return "{\"payload\":{},\"canonical\":\"" + escapeJsonString(canonical) + "\",\"signature_hex\":\"" + signatureHex +
           "\",\"lease_blob\":\"" + escapeJsonString(blob) +
           "\",\"key_id\":\"aestra-test-key\",\"format\":\"aestra-license-v1\"}";
}

bool testParsesUsableRefreshResponse() {
    const LicenseRefreshResult result = LicenseRefreshClient::parseRefreshResponse(responseJson());

    bool ok = true;
    ok &= expect(result.status == LicenseRefreshStatus::Success, "valid refresh response status");
    ok &= expect(result.canonical == kCanonical, "canonical field preserved");
    ok &= expect(result.signatureHex == kSignatureHex, "signature hex field preserved");
    ok &= expect(result.leaseBlob == std::string(kCanonical) + "\n" + kSignatureHex, "lease blob assembled by Worker");
    ok &= expect(result.keyId == "aestra-test-key", "key id preserved as metadata");
    ok &= expect(result.format == "aestra-license-v1", "format preserved");
    ok &= expect(LicenseRefreshClient::isUsableLeaseBlob(result), "valid refresh response is usable");
    return ok;
}

bool testRejectsMismatchedLeaseBlob() {
    const LicenseRefreshResult result =
        LicenseRefreshClient::parseRefreshResponse(responseJson(kCanonical, kSignatureHex, "tampered"));

    bool ok = true;
    ok &= expect(result.status == LicenseRefreshStatus::InvalidResponse, "mismatched lease blob rejected");
    ok &= expect(!LicenseRefreshClient::isUsableLeaseBlob(result), "mismatched lease blob is not usable");
    return ok;
}

bool testRejectsBadSignatureHex() {
    const LicenseRefreshResult result = LicenseRefreshClient::parseRefreshResponse(responseJson(kCanonical, "abcd"));

    bool ok = true;
    ok &= expect(result.status == LicenseRefreshStatus::InvalidResponse, "short signature rejected");
    ok &= expect(!LicenseRefreshClient::isUsableLeaseBlob(result), "short signature is not usable");
    return ok;
}

bool testRejectsUnsupportedFormat() {
    const std::string json = "{\"payload\":{},\"canonical\":\"" + escapeJsonString(kCanonical) +
                             "\",\"signature_hex\":\"" + kSignatureHex + "\",\"lease_blob\":\"" +
                             escapeJsonString(std::string(kCanonical) + "\n" + kSignatureHex) +
                             "\",\"format\":\"unexpected\"}";
    const LicenseRefreshResult result = LicenseRefreshClient::parseRefreshResponse(json);

    bool ok = true;
    ok &= expect(result.status == LicenseRefreshStatus::InvalidResponse, "unsupported format rejected");
    ok &= expect(!LicenseRefreshClient::isUsableLeaseBlob(result), "unsupported format is not usable");
    return ok;
}

bool testRefreshTransportRemainsUnavailable() {
    UnavailableHttpTransport transport;
    LicenseRefreshRequest request;
    request.userId = "user_refresh_1";
    request.deviceHash = "device_refresh_1";
    const LicenseRefreshResult result = LicenseRefreshClient::refresh(request, transport, "", "");

    bool ok = true;
    ok &= expect(result.status == LicenseRefreshStatus::SyncUnavailable, "refresh transport is still unavailable");
    ok &= expect(result.leaseBlob.empty(), "unavailable refresh does not return lease material");
    ok &= expect(!LicenseRefreshClient::isUsableLeaseBlob(result), "unavailable refresh is not usable");
    return ok;
}
} // namespace

int main() {
    bool ok = true;
    ok &= testParsesUsableRefreshResponse();
    ok &= testRejectsMismatchedLeaseBlob();
    ok &= testRejectsBadSignatureHex();
    ok &= testRejectsUnsupportedFormat();
    ok &= testRefreshTransportRemainsUnavailable();

    if (!ok) {
        return 1;
    }
    std::cout << "LicenseRefreshClient tests passed.\n";
    return 0;
}
