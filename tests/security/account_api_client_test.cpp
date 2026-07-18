#include "AccountApiClient.h"
#include "AccountService.h"
#include "EntitlementStore.h"
#include "LicenseGate.h"
#include "LocalAccountCache.h"
#include "MembershipViewModel.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace Aestra::License;
namespace fs = std::filesystem;

namespace {
class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(const char* name) : m_name(name) {
        if (const char* value = std::getenv(name)) {
            m_hadValue = true;
            m_value = value;
        }
    }

    ~ScopedEnvironmentVariable() { restore(); }

    void unset() const {
#ifdef _WIN32
        _putenv_s(m_name.c_str(), "");
#else
        unsetenv(m_name.c_str());
#endif
    }

private:
    void restore() const {
#ifdef _WIN32
        _putenv_s(m_name.c_str(), m_hadValue ? m_value.c_str() : "");
#else
        if (m_hadValue) {
            setenv(m_name.c_str(), m_value.c_str(), 1);
        } else {
            unsetenv(m_name.c_str());
        }
#endif
    }

    std::string m_name;
    bool m_hadValue = false;
    std::string m_value;
};

struct CapturedRequest {
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

#ifndef _WIN32
class LoopbackHttpServer {
public:
    LoopbackHttpServer(int statusCode, std::string responseBody)
        : m_statusCode(statusCode), m_responseBody(std::move(responseBody)) {
        m_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd < 0) {
            return;
        }

        int reuse = 1;
        ::setsockopt(m_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return;
        }
        if (::listen(m_fd, 1) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return;
        }

        socklen_t length = sizeof(address);
        if (::getsockname(m_fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            ::close(m_fd);
            m_fd = -1;
            return;
        }
        m_port = ntohs(address.sin_port);
        m_thread = std::thread([this] { serveOnce(); });
    }

    ~LoopbackHttpServer() {
        if (m_fd >= 0) {
            ::shutdown(m_fd, SHUT_RDWR);
            ::close(m_fd);
            m_fd = -1;
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    bool ok() const { return m_fd >= 0 && m_port > 0; }
    std::string baseUrl() const { return "http://127.0.0.1:" + std::to_string(m_port); }
    const CapturedRequest& request() const { return m_request; }

private:
    void serveOnce() {
        const int client = ::accept(m_fd, nullptr, nullptr);
        if (client < 0) {
            return;
        }

        std::string raw;
        char buffer[1024];
        while (raw.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                ::close(client);
                return;
            }
            raw.append(buffer, static_cast<size_t>(n));
        }

        const size_t headerEnd = raw.find("\r\n\r\n");
        parseHeaders(raw.substr(0, headerEnd));
        size_t contentLength = 0;
        auto contentLengthIt = m_request.headers.find("content-length");
        if (contentLengthIt != m_request.headers.end()) {
            contentLength = static_cast<size_t>(std::stoul(contentLengthIt->second));
        }

        const size_t bodyStart = headerEnd + 4;
        if (raw.size() > bodyStart) {
            m_request.body = raw.substr(bodyStart);
        }
        while (m_request.body.size() < contentLength) {
            const ssize_t n = ::recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) {
                break;
            }
            m_request.body.append(buffer, static_cast<size_t>(n));
        }
        if (m_request.body.size() > contentLength) {
            m_request.body.resize(contentLength);
        }

        const std::string response = "HTTP/1.1 " + std::to_string(m_statusCode) + " OK\r\nContent-Type: "
                                     "application/json\r\nContent-Length: " +
                                     std::to_string(m_responseBody.size()) + "\r\nConnection: close\r\n\r\n" +
                                     m_responseBody;
        ::send(client, response.data(), response.size(), 0);
        ::close(client);
    }

    void parseHeaders(const std::string& headers) {
        std::istringstream stream(headers);
        std::string line;
        if (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::istringstream firstLine(line);
            firstLine >> m_request.method >> m_request.url;
        }

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            const size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') {
                value.erase(value.begin());
            }
            for (char& ch : name) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            m_request.headers[name] = value;
        }
    }

    int m_fd = -1;
    int m_port = 0;
    int m_statusCode = 200;
    std::string m_responseBody;
    CapturedRequest m_request;
    std::thread m_thread;
};
#endif

class FakeTransport final : public IHttpTransport {
public:
    HttpResponse next;
    std::vector<CapturedRequest> requests;

    HttpResponse send(const HttpRequest& request) override {
        requests.push_back({request.method, request.url, request.headers, request.body});
        return next;
    }
};

class FakeLeaseInstaller final : public ILeaseInstaller {
public:
    bool accept = true;
    std::string installed;

    bool installLeaseBlob(const std::string& leaseBlob, std::string& message) override {
        installed = leaseBlob;
        if (!accept) {
            message = "bad signature";
            return false;
        }
        return true;
    }
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
        return false;
    }
    return true;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

fs::path testPath(const std::string& name) {
    const fs::path root = fs::temp_directory_path() / "aestra_account_api_client_tests";
    fs::create_directories(root);
    return root / (name + ".json");
}

std::string refreshJson(const std::string& signatureHex = std::string(128, 'a')) {
    const std::string canonical =
        "{\"license_id\":\"lic_api\",\"user_id\":\"acct_api\",\"tier\":\"Founder\",\"plugins\":[],"
        "\"features\":[],\"device_hash\":\"device_api\",\"issued_at\":1700000000,"
        "\"expires_at\":1700604800,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}";
    return "{\"payload\":{},\"canonical\":\"" + escapeJsonString(canonical) + "\",\"signature_hex\":\"" + signatureHex +
           "\",\"lease_blob\":\"" + escapeJsonString(canonical + "\n" + signatureHex) +
           "\",\"key_id\":\"test\",\"format\":\"aestra-license-v1\"}";
}

AccountApiClient makeClient(FakeTransport& transport) {
    AccountApiConfig config;
    config.baseUrl = "https://license.example.test/";
    return AccountApiClient(config, transport);
}

bool testRequestConstruction() {
    FakeTransport transport;
    transport.next.status = 200;
    transport.next.body = "{\"ok\":true,\"challenge_id\":\"lc_1\",\"expires_at\":4102444800}";
    AccountApiClient client = makeClient(transport);

    bool ok = true;
    const LoginStartResult loginStart = client.loginStart("artist@example.test");
    ok &= expect(loginStart.status == AccountApiStatus::Success, "loginStart success");
    ok &= expect(transport.requests.back().method == "POST", "loginStart POST");
    ok &= expect(transport.requests.back().url == "https://license.example.test/v1/account/login/start",
                 "loginStart path");
    ok &= expect(transport.requests.back().body.find("artist@example.test") != std::string::npos, "loginStart body");

    transport.next.body =
        "{\"ok\":true,\"account\":{\"id\":\"acct_1\",\"email\":\"artist@example.test\",\"status\":\"active\"},"
        "\"session\":{\"token\":\"session_secret_1\",\"expires_at\":4102444800}}";
    const LoginVerifyResult verify = client.loginVerify("artist@example.test", "lc_1", "123456");
    ok &= expect(verify.status == AccountApiStatus::Success, "loginVerify success");
    ok &= expect(transport.requests.back().url == "https://license.example.test/v1/account/login/verify",
                 "loginVerify path");
    ok &= expect(transport.requests.back().body.find("123456") != std::string::npos, "loginVerify body");

    transport.next.body = "{\"ok\":true,\"account\":{\"id\":\"acct_1\",\"email\":\"artist@example.test\"},"
                          "\"entitlement\":{\"tier\":\"Core\"}}";
    client.me("session_secret_1");
    ok &= expect(transport.requests.back().headers["authorization"] == "Bearer session_secret_1", "me bearer header");
    ok &= expect(transport.requests.back().url == "https://license.example.test/v1/account/me", "me path");

    client.revoke("session_secret_1");
    ok &=
        expect(transport.requests.back().headers["authorization"] == "Bearer session_secret_1", "revoke bearer header");
    ok &= expect(transport.requests.back().url == "https://license.example.test/v1/account/session/revoke",
                 "revoke path");

    transport.next.body = refreshJson();
    client.refreshEntitlements("session_secret_1", "device_api");
    ok &= expect(transport.requests.back().headers["authorization"] == "Bearer session_secret_1",
                 "refresh bearer header");
    ok &= expect(transport.requests.back().url == "https://license.example.test/v1/account/entitlements/refresh",
                 "refresh path");

    for (const CapturedRequest& request : transport.requests) {
        ok &= expect(request.url.find("/v1/admin/") == std::string::npos, "native client must not call admin routes");
    }
    return ok;
}

bool testServiceStoresSessionAndRefreshesLease() {
    const fs::path path = testPath("service");
    fs::remove(path);
    LocalAccountCache cache(path);
    FakeTransport transport;
    AccountApiClient client = makeClient(transport);
    FakeLeaseInstaller installer;
    AccountService service(client, cache, installer);

    bool ok = true;
    transport.next.status = 200;
    transport.next.body =
        "{\"ok\":true,\"account\":{\"id\":\"acct_1\",\"email\":\"artist@example.test\",\"status\":\"active\"},"
        "\"session\":{\"token\":\"session_secret_1\",\"expires_at\":4102444800}}";
    const AccountServiceResult login = service.loginVerify("artist@example.test", "lc_1", "123456");
    ok &= expect(login.status == AccountServiceStatus::Success, "service loginVerify success");
    const LocalAccountCacheLoadResult loaded = cache.load();
    ok &= expect(loaded.status == LocalAccountCacheLoadStatus::Loaded, "session cache loads");
    ok &= expect(loaded.record.sessionToken == "session_secret_1", "session token is stored for refresh");
    ok &= expect(readText(path).find("session_secret_1") != std::string::npos, "session token persisted in cache");

    transport.next.body = refreshJson();
    const AccountServiceResult refresh = service.refreshEntitlements();
    ok &= expect(refresh.status == AccountServiceStatus::Success, "service refresh success");
    ok &= expect(!installer.installed.empty(), "lease installer received lease blob");
    ok &= expect(transport.requests.back().headers["authorization"] == "Bearer session_secret_1",
                 "service refresh uses bearer token");
    return ok;
}

bool testServiceFailureBoundaries() {
    const fs::path path = testPath("failures");
    fs::remove(path);
    LocalAccountCache cache(path);
    FakeTransport transport;
    AccountApiClient client = makeClient(transport);
    FakeLeaseInstaller installer;
    AccountService service(client, cache, installer);

    bool ok = true;
    const AccountServiceResult missingSession = service.refreshEntitlements();
    ok &= expect(missingSession.status == AccountServiceStatus::Unauthorized, "missing session is unauthorized");

    LocalAccountRecord record;
    record.identity.userId = "acct_1";
    record.identity.email = "artist@example.test";
    record.sessionToken = "session_secret_2";
    record.state = AccountSessionState::SignedInFresh;
    record.lastSyncUnix = 123;
    record.hasIdentity = true;
    cache.save(record);

    transport.next.status = 0;
    transport.next.error = "network down";
    const AccountServiceResult offline = service.refreshEntitlements();
    ok &=
        expect(offline.status == AccountServiceStatus::SyncUnavailable, "network unavailable maps to sync unavailable");
    ok &= expect(cache.load().status == LocalAccountCacheLoadStatus::Loaded, "network failure preserves session");

    transport.next.status = 401;
    transport.next.error.clear();
    const AccountServiceResult unauthorized = service.refreshEntitlements();
    ok &= expect(unauthorized.status == AccountServiceStatus::Unauthorized, "401 maps to unauthorized");
    ok &= expect(cache.load().status == LocalAccountCacheLoadStatus::Missing, "401 clears local session");

    cache.save(record);
    transport.next.status = 200;
    transport.next.body = refreshJson("bad");
    const AccountServiceResult malformed = service.refreshEntitlements();
    ok &= expect(malformed.status == AccountServiceStatus::InvalidResponse, "malformed refresh rejected");
    ok &= expect(cache.load().status == LocalAccountCacheLoadStatus::Loaded, "malformed response preserves session");

    cache.save(record);
    transport.next.body = refreshJson();
    installer.accept = false;
    const AccountServiceResult rejected = service.refreshEntitlements();
    ok &= expect(rejected.status == AccountServiceStatus::RejectedSignature, "bad signature rejected by installer");
    ok &= expect(cache.load().status == LocalAccountCacheLoadStatus::Loaded, "bad signature preserves session");
    return ok;
}

bool testCurlTransportLoopback() {
    if (!curlHttpTransportAvailable()) {
        std::cout << "libcurl transport unavailable in this build; skipping loopback HTTP transport test.\n";
        return true;
    }
#ifdef _WIN32
    std::cout << "loopback HTTP transport test is not implemented on Windows; skipping.\n";
    return true;
#else
    bool ok = true;
    std::unique_ptr<IHttpTransport> transport = createDefaultHttpTransport();

    {
        LoopbackHttpServer server(201, "{\"ok\":true}");
        if (!server.ok()) {
            std::cout << "loopback sockets unavailable; skipping concrete HTTP transport loopback test.\n";
            return true;
        }
        HttpRequest request;
        request.method = "POST";
        request.url = server.baseUrl() + "/echo";
        request.body = "{\"hello\":\"world\"}";
        request.timeoutMs = 1000;
        request.headers["content-type"] = "application/json";
        request.headers["authorization"] = "Bearer loopback_secret";

        const HttpResponse response = transport->send(request);
        ok &= expect(response.status == 201, "curl POST preserves HTTP status");
        ok &= expect(response.body == "{\"ok\":true}", "curl POST preserves response body");
        ok &= expect(server.request().method == "POST", "curl POST method sent");
        ok &= expect(server.request().url == "/echo", "curl POST path sent");
        ok &= expect(server.request().body == "{\"hello\":\"world\"}", "curl POST body sent");
        ok &= expect(server.request().headers.at("authorization") == "Bearer loopback_secret",
                     "curl authorization header sent");
    }

    {
        LoopbackHttpServer server(503, "{\"error\":\"down\"}");
        if (!server.ok()) {
            std::cout << "loopback sockets unavailable; skipping concrete HTTP non-2xx test.\n";
            return true;
        }
        HttpRequest request;
        request.method = "GET";
        request.url = server.baseUrl() + "/status";
        request.timeoutMs = 1000;

        const HttpResponse response = transport->send(request);
        ok &= expect(response.status == 503, "curl GET preserves non-2xx HTTP status");
        ok &= expect(response.body == "{\"error\":\"down\"}", "curl GET preserves non-2xx body");
        ok &= expect(server.request().method == "GET", "curl GET method sent");
        ok &= expect(server.request().url == "/status", "curl GET path sent");
    }

    {
        HttpRequest request;
        request.method = "GET";
        request.url = "http://127.0.0.1:1/unavailable";
        request.timeoutMs = 200;
        request.headers["authorization"] = "Bearer should_not_appear";

        const HttpResponse response = transport->send(request);
        ok &= expect(response.status == 0, "curl unavailable maps to transport status 0");
        ok &= expect(response.error.find("should_not_appear") == std::string::npos,
                     "curl transport error redacts bearer token");
    }

    return ok;
#endif
}

bool testAccountApiClientWithCurlTransport() {
    if (!curlHttpTransportAvailable()) {
        std::cout << "libcurl transport unavailable in this build; skipping account API curl integration test.\n";
        return true;
    }
#ifdef _WIN32
    std::cout << "account API curl integration test is not implemented on Windows; skipping.\n";
    return true;
#else
    bool ok = true;
    std::unique_ptr<IHttpTransport> transport = createDefaultHttpTransport();

    {
        LoopbackHttpServer server(200, "{\"ok\":true,\"challenge_id\":\"lc_loopback\",\"expires_at\":4102444800}");
        if (!server.ok()) {
            std::cout << "loopback sockets unavailable; skipping account API curl integration test.\n";
            return true;
        }
        AccountApiConfig config;
        config.baseUrl = server.baseUrl();
        config.timeoutMs = 1000;
        AccountApiClient client(config, *transport);
        const LoginStartResult result = client.loginStart("loopback@example.test");
        ok &= expect(result.status == AccountApiStatus::Success, "loginStart succeeds through curl transport");
        ok &= expect(result.challengeId == "lc_loopback", "loginStart parses curl response");
        ok &= expect(server.request().url == "/v1/account/login/start", "loginStart curl path");
        ok &= expect(server.request().body.find("loopback@example.test") != std::string::npos,
                     "loginStart curl body");
    }

    {
        LoopbackHttpServer server(
            200, "{\"ok\":true,\"account\":{\"id\":\"acct_loop\",\"email\":\"loopback@example.test\"},"
                 "\"entitlement\":{\"tier\":\"founder\"}}");
        if (!server.ok()) {
            std::cout << "loopback sockets unavailable; skipping account API me curl integration test.\n";
            return true;
        }
        AccountApiConfig config;
        config.baseUrl = server.baseUrl();
        config.timeoutMs = 1000;
        AccountApiClient client(config, *transport);
        const AccountMeResult result = client.me("loopback_session_secret");
        ok &= expect(result.status == AccountApiStatus::Success, "me succeeds through curl transport");
        ok &= expect(result.identity.userId == "acct_loop", "me parses curl account id");
        ok &= expect(server.request().url == "/v1/account/me", "me curl path");
        ok &= expect(server.request().headers.at("authorization") == "Bearer loopback_session_secret",
                     "me curl bearer header");
    }

    return ok;
#endif
}
} // namespace

#ifndef _WIN32
bool testLoginFlowFull() {
    if (!curlHttpTransportAvailable()) {
        std::cout << "libcurl transport unavailable in this build; skipping login flow integration test.\n";
        return true;
    }
    bool ok = true;
    const fs::path cachePath = testPath("login_flow_full");
    fs::remove(cachePath);

    std::unique_ptr<IHttpTransport> transport = createDefaultHttpTransport();
    AccountApiConfig config;
    config.timeoutMs = 2000;

    LocalAccountCache cache(cachePath);
    FakeLeaseInstaller installer;
    EntitlementProfile testProfile;
    testProfile.tier = MembershipTier::Core;
    testProfile.status = EntitlementStatus::Missing;
    testProfile.offline = true;
    testProfile.verified = false;
    EntitlementStore entitlements([testProfile]() { return testProfile; });
    AccountApiClient apiClient(config, *transport);
    AccountService service(apiClient, cache, installer);

    {
        LoopbackHttpServer startServer(200,
                                       R"({"ok":true,"challenge_id":"lc_full_flow","expires_at":9999999999,"fixture_code":"111222"})");
        if (!startServer.ok()) {
            std::cout << "loopback sockets unavailable; skipping login flow integration test.\n";
            return true;
        }
        config.baseUrl = startServer.baseUrl();
        AccountApiClient startClient(config, *transport);
        AccountService startService(startClient, cache, installer);

        const AccountLoginStartServiceResult start = startService.loginStart("flow@example.test");
        ok &= expect(start.status == AccountServiceStatus::Success, "loginStart returns success");
        ok &= expect(start.challengeId == "lc_full_flow", "loginStart returns challenge_id");
        ok &= expect(startServer.request().method == "POST", "loginStart method is POST");
        ok &= expect(startServer.request().url == "/v1/account/login/start", "loginStart path");
        ok &= expect(startServer.request().body.find("flow@example.test") != std::string::npos,
                     "loginStart body contains email");
    }

    {
        LoopbackHttpServer verifyServer(200,
                                        R"({"ok":true,"account":{"id":"acct_flow","email":"flow@example.test"},)"
                                        R"("session":{"token":"session_flow_test","expires_at":9999999999}})");
        if (!verifyServer.ok()) {
            std::cout << "verify loopback server unavailable; skipping.\n";
            return false;
        }
        config.baseUrl = verifyServer.baseUrl();
        AccountApiClient updatedClient(config, *transport);
        AccountService updatedService(updatedClient, cache, installer);

        {
            LoopbackHttpServer failServer(401, R"({"error":{"code":"invalid_code","message":"login code is invalid"}})");
            if (failServer.ok()) {
                AccountApiConfig failConfig;
                failConfig.baseUrl = failServer.baseUrl();
                failConfig.timeoutMs = 2000;
                AccountApiClient failClient(failConfig, *transport);
                AccountService failService(failClient, cache, installer);
                const AccountServiceResult wrongCode =
                    failService.loginVerify("flow@example.test", "lc_full_flow", "000000");
                ok &= expect(wrongCode.status == AccountServiceStatus::Unauthorized,
                             "verify with wrong code returns unauthorized");
            }
        }

        const AccountServiceResult verify = updatedService.loginVerify("flow@example.test", "lc_full_flow", "111222");
        ok &= expect(verify.status == AccountServiceStatus::Success, "verify with correct fixture_code succeeds");
        ok &= expect(verifyServer.request().url == "/v1/account/login/verify", "verify path");
        ok &= expect(verifyServer.request().body.find("111222") != std::string::npos,
                     "verify body contains fixture_code from start response");
        const LocalAccountCacheLoadResult cached = cache.load();
        ok &= expect(cached.status == LocalAccountCacheLoadStatus::Loaded, "loginVerify stores session to cache");
        ok &= expect(cached.record.sessionToken == "session_flow_test", "cached session token is stored");
        ok &= expect(cached.record.identity.userId == "acct_flow", "loginVerify stores identity userId");
        ok &= expect(cached.record.identity.email == "flow@example.test", "loginVerify stores identity email");
    }

    {
        const std::string canonical =
            "{\"license_id\":\"lic_flow\",\"user_id\":\"acct_flow\",\"tier\":\"Founder\",\"plugins\":[],"
            "\"features\":[\"Rumble\",\"RumbleHeadless\",\"FounderBadge\",\"PremiumPluginBundle\"],"
            "\"device_hash\":\"device_flow_test\",\"issued_at\":1700000000,"
            "\"expires_at\":1799999999,\"grace_policy\":\"restrict\",\"revocation_epoch\":0}";
        const std::string sigHex = std::string(128, 'a');
        const std::string refreshBody =
            "{\"payload\":{},\"canonical\":\"" + escapeJsonString(canonical) + "\",\"signature_hex\":\"" + sigHex +
            "\",\"lease_blob\":\"" + escapeJsonString(canonical + "\n" + sigHex) +
            "\",\"key_id\":\"test\",\"format\":\"aestra-license-v1\"}";
        LoopbackHttpServer refreshServer(200, refreshBody);
        if (!refreshServer.ok()) {
            std::cout << "refresh loopback server unavailable; skipping.\n";
            return false;
        }
        config.baseUrl = refreshServer.baseUrl();
        AccountApiClient refreshedClient(config, *transport);
        AccountService refreshedService(refreshedClient, cache, installer);
        const AccountServiceResult refresh = refreshedService.refreshEntitlements();
        ok &= expect(refresh.status == AccountServiceStatus::Success, "refreshEntitlements after loginVerify succeeds");
        ok &= expect(!installer.installed.empty(), "lease installer received blob");
        ok &= expect(refreshServer.request().headers.at("authorization") == "Bearer session_flow_test",
                     "refreshEntitlements uses cached session token");
    }

    {
        LocalAccountCache freshCache(cachePath);
        AccountSession freshSession(freshCache, entitlements);
        const AccountSessionSnapshot snap = freshSession.current();
        ok &= expect(snap.signedIn, "fresh AccountSession sees signed-in after login flow");
        ok &= expect(!snap.identity.userId.empty(), "fresh AccountSession has identity after login flow");
        ok &= expect(freshSession.state() == AccountSessionState::SignedInCached ||
                        freshSession.state() == AccountSessionState::SignedInFresh,
                    "fresh AccountSession state is signed-in (not core/signed-out)");
        ok &= expect(!snap.statusMessage.empty(), "fresh AccountSession has status message after login flow");
    }

    fs::remove(cachePath);
    return ok;
}

bool testAccountApiUsesCanonicalDefaultBaseUrl() {
    ScopedEnvironmentVariable baseUrl("AESTRA_ACCOUNT_API_BASE_URL");
    baseUrl.unset();
    const AccountApiConfig config = accountApiConfigFromEnvironment();
    bool ok = true;
    ok &= expect(config.baseUrl == "https://www.aestra.studio",
                 "accountApiConfigFromEnvironment uses the canonical production URL by default");
    return ok;
}

bool testRealWorkerLoginStartSendsMail() {
    if (!curlHttpTransportAvailable()) {
        std::cout << "libcurl transport unavailable; skipping real Worker login test.\n";
        return true;
    }
    const char* explicitBaseUrl = std::getenv("AESTRA_ACCOUNT_API_BASE_URL");
    if (!explicitBaseUrl || !*explicitBaseUrl) {
        std::cout << "no explicit Worker URL; skipping real Worker login test.\n";
        return true;
    }
    const AccountApiConfig config = accountApiConfigFromEnvironment();

    std::unique_ptr<IHttpTransport> curlTransport = createDefaultHttpTransport();
    AccountApiClient client(config, *curlTransport);

    const LoginStartResult result = client.loginStart("aestra-real-worker-test@example.test");
    bool ok = true;
    ok &= expect(result.status == AccountApiStatus::Success, "loginStart against real Worker returns success");
    ok &= expect(!result.challengeId.empty(), "real Worker loginStart returns challenge_id");
    ok &= expect(result.challengeId.rfind("lc_", 0) == 0, "challenge_id has expected prefix");
    return ok;
}
#endif

int main() {
    fs::remove_all(fs::temp_directory_path() / "aestra_account_api_client_tests");
    bool ok = true;
    ok &= testRequestConstruction();
    ok &= testCurlTransportLoopback();
    ok &= testAccountApiClientWithCurlTransport();
    ok &= testServiceStoresSessionAndRefreshesLease();
    ok &= testServiceFailureBoundaries();
#ifndef _WIN32
    ok &= testLoginFlowFull();
    ok &= testAccountApiUsesCanonicalDefaultBaseUrl();
    ok &= testRealWorkerLoginStartSendsMail();
#endif
    fs::remove_all(fs::temp_directory_path() / "aestra_account_api_client_tests");

    if (!ok) {
        return 1;
    }
    std::cout << "Account API client tests passed.\n";
    return 0;
}
