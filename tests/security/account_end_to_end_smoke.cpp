#include "AccountApiClient.h"
#include "AccountService.h"
#include "AestraJSON.h"
#include "EntitlementStore.h"
#include "HttpTransport.h"
#include "LicenseGate.h"
#include "LocalAccountCache.h"
#include "MembershipViewModel.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

using namespace Aestra::License;
namespace fs = std::filesystem;

namespace {
constexpr int kSkip = 77;

std::string envString(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

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

std::string joinUrl(std::string baseUrl, const std::string& path) {
    while (!baseUrl.empty() && baseUrl.back() == '/') {
        baseUrl.pop_back();
    }
    return baseUrl + path;
}

HttpResponse sendJson(IHttpTransport& transport, const std::string& baseUrl, const std::string& path,
                      const std::string& body, const std::string& bearer = "") {
    HttpRequest request;
    request.method = "POST";
    request.url = joinUrl(baseUrl, path);
    request.body = body;
    request.timeoutMs = 5000;
    request.headers["content-type"] = "application/json";
    if (!bearer.empty()) {
        request.headers["authorization"] = "Bearer " + bearer;
    }
    return transport.send(request);
}

bool readString(Aestra::JSON& json, const std::string& key, std::string& out) {
    if (!json.has(key) || !json[key].isString()) {
        return false;
    }
    out = json[key].asString();
    return true;
}

bool configureIsolatedDataDir() {
    const fs::path root = fs::temp_directory_path() / "aestra_account_e2e_smoke";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) {
        return false;
    }
#ifdef _WIN32
    return _putenv_s("AESTRA_DATA_DIR", root.string().c_str()) == 0;
#else
    return setenv("AESTRA_DATA_DIR", root.string().c_str(), 1) == 0;
#endif
}

bool featureEnabled(const MembershipViewState& state, const std::string& label) {
    for (const MembershipFeatureRow& row : state.features) {
        if (row.label == label) {
            return row.enabled;
        }
    }
    return false;
}
} // namespace

int main() {
    const bool expectLive = envString("AESTRA_SMOKE_EXPECT_LIVE_WORKER") == "1";
    const std::string baseUrl = envString("AESTRA_ACCOUNT_API_BASE_URL");
    const std::string email = envString("AESTRA_SMOKE_EMAIL");
    const std::string adminKey = envString("AESTRA_SMOKE_ADMIN_KEY");
    std::string smokeTier = envString("AESTRA_SMOKE_TIER");
    if (smokeTier.empty()) {
        smokeTier = "founder";
    }

    if (!expectLive) {
        std::cout << "AccountEndToEndSmoke skipped: set AESTRA_SMOKE_EXPECT_LIVE_WORKER=1 to run.\n";
        return kSkip;
    }
    if (baseUrl.empty() || email.empty() || adminKey.empty()) {
        std::cerr << "failed: AESTRA_ACCOUNT_API_BASE_URL, AESTRA_SMOKE_EMAIL, and AESTRA_SMOKE_ADMIN_KEY are required\n";
        return 1;
    }
    if (smokeTier != "founder" && smokeTier != "supporter") {
        std::cerr << "failed: AESTRA_SMOKE_TIER must be founder or supporter\n";
        return 1;
    }
    if (!curlHttpTransportAvailable()) {
        std::cerr << "failed: libcurl transport is not available in this build\n";
        return 1;
    }
    if (!configureIsolatedDataDir()) {
        std::cerr << "failed: could not configure isolated AESTRA_DATA_DIR\n";
        return 1;
    }

    std::unique_ptr<IHttpTransport> transport = createDefaultHttpTransport();
    AccountApiConfig config;
    config.baseUrl = baseUrl;
    config.timeoutMs = 5000;
    AccountApiClient client(config, *transport);

    bool ok = true;

    const HttpResponse loginStart = sendJson(*transport, baseUrl, "/v1/account/login/start",
                                             "{\"email\":" + quoteJson(email) + "}");
    ok &= expect(loginStart.status == 200, "login/start returns 200");
    Aestra::JSON startJson = Aestra::JSON::parse(loginStart.body);
    std::string challengeId;
    std::string fixtureCode;
    ok &= expect(startJson.isObject(), "login/start response is JSON object");
    ok &= expect(readString(startJson, "challenge_id", challengeId), "login/start returns challenge_id");
    ok &= expect(readString(startJson, "fixture_code", fixtureCode),
                 "login/start returns fixture_code in fixture mailer mode");
    if (!ok) {
        return 1;
    }

    const fs::path cachePath = fs::temp_directory_path() / "aestra_account_e2e_smoke" / "account_cache.json";
    LocalAccountCache cache(cachePath);
    LicenseGateLeaseInstaller installer;
    AccountService service(client, cache, installer);

    const AccountServiceResult login = service.loginVerify(email, challengeId, fixtureCode);
    ok &= expect(login.status == AccountServiceStatus::Success, "login/verify stores native account session");
    const LocalAccountCacheLoadResult cachedLogin = cache.load();
    ok &= expect(cachedLogin.status == LocalAccountCacheLoadStatus::Loaded, "local session cache loads");
    ok &= expect(!cachedLogin.record.sessionToken.empty(), "local session token is present");
    const std::string sessionToken = cachedLogin.record.sessionToken;

    const AccountMeResult me = client.me(sessionToken);
    ok &= expect(me.status == AccountApiStatus::Success, "account/me succeeds with issued session");
    ok &= expect(!me.identity.userId.empty(), "account/me returns account id");

    const AccountServiceResult coreRefresh = service.refreshEntitlements();
    ok &= expect(coreRefresh.status == AccountServiceStatus::Success, "Core entitlement refresh installs signed lease");
    EntitlementStore entitlements;
    AccountSession coreSession(cache, entitlements);
    MembershipViewModel coreView(coreSession, entitlements);
    const MembershipViewState coreState = coreView.current();
    ok &= expect(coreState.tierLabel == "Aestra Core", "MembershipViewModel observes Core state");
    ok &= expect(coreState.signedIn, "MembershipViewModel observes signed-in account");

    const std::string grantBody =
        "{\"email\":" + quoteJson(email) + ",\"tier\":" + quoteJson(smokeTier) +
        ",\"status\":\"active\",\"source\":\"manual_grant\",\"source_ref\":\"admin:local-smoke\","
        "\"current_period_end\":null,\"grace_until\":null,\"reason\":\"local smoke grant\"}";
    const HttpResponse grant =
        sendJson(*transport, baseUrl, "/v1/admin/entitlements/grant", grantBody, adminKey);
    ok &= expect(grant.status == 200, "admin grant succeeds in smoke harness");

    const AccountServiceResult paidRefresh = service.refreshEntitlements();
    ok &= expect(paidRefresh.status == AccountServiceStatus::Success,
                 "paid entitlement refresh installs signed lease");
    MembershipViewModel paidView(coreSession, entitlements);
    const MembershipViewState paidState = paidView.current();
    const std::string expectedLabel = smokeTier == "founder" ? "Aestra Founder" : "Aestra Supporter";
    ok &= expect(paidState.tierLabel == expectedLabel, "MembershipViewModel observes paid entitlement");
    ok &= expect(featureEnabled(paidState, "Aestra Rumble"), "paid entitlement enables Rumble");
    if (smokeTier == "founder") {
        ok &= expect(featureEnabled(paidState, "Rumble Headless"), "Founder entitlement enables Rumble Headless");
    }

    const AccountServiceResult revoke = service.revokeSession(false);
    ok &= expect(revoke.status == AccountServiceStatus::Success, "session revoke succeeds");
    const AccountMeResult revokedMe = client.me(sessionToken);
    ok &= expect(revokedMe.status == AccountApiStatus::Unauthorized, "revoked session is unauthorized on account/me");
    const AccountServiceResult afterRevokeRefresh = service.refreshEntitlements();
    ok &= expect(afterRevokeRefresh.status == AccountServiceStatus::Unauthorized,
                 "native refresh is unauthorized after local session clear");

    if (!ok) {
        return 1;
    }
    std::cout << "Account end-to-end smoke passed.\n";
    return 0;
}
