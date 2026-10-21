// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Asking the update host: what the app sends, what it believes, what it does about each way the answer can go wrong,
// the background checker's schedule, and the saved settings. Fixtures: update_feed_fixtures.inc (a throwaway test key).

#include "UpdateChecker.h"
#include "UpdateClient.h"
#include "UpdateSettings.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::License;

namespace {

struct Signed {
    const char* payload;
    const char* signatureHex;
};
#include "update_feed_fixtures.inc"

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "failed: " << what << "\n";
        ++g_failures;
    }
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}
std::string document(const Signed& s) {
    return std::string("{\"payload\":\"") + escape(s.payload) + "\",\"signature\":\"" + s.signatureHex + "\"}";
}

class FakeTransport : public IHttpTransport {
public:
    HttpResponse reply;
    std::vector<HttpRequest> seen;
    std::atomic<int> calls{0};
    HttpResponse send(const HttpRequest& request) override {
        seen.push_back(request);
        ++calls;
        return reply;
    }
};

HttpResponse ok(const Signed& s) {
    HttpResponse r;
    r.status = 200;
    r.body = document(s);
    return r;
}
HttpResponse status(int code) {
    HttpResponse r;
    r.status = code;
    return r;
}

UpdateConfig config(const std::string& channel, const std::string& current = "0.8.1", const std::string& key = "aef_testkey0123456789") {
    UpdateConfig c;
    c.channel = channel;
    c.founderKey = key;
    c.currentVersion = current;
    c.platform = "windows-x64";
    const std::string hex = kPublicKeyHex;
    for (size_t i = 0; i < 32; ++i) c.publicKey[i] = static_cast<unsigned char>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    return c;
}

UpdateCheckStatus run(FakeTransport& t, const UpdateConfig& c) { return checkForUpdate(t, c).status; }

} // namespace

int main() {
    // httpsHostOf
    check(httpsHostOf("https://update.aestra.studio/early/x.zip") == "update.aestra.studio", "host of a plain URL");
    check(httpsHostOf("https://UPDATE.Aestra.Studio:443/x") == "update.aestra.studio", "host is lower case without the port");
    check(httpsHostOf("http://update.aestra.studio/x").empty(), "http is not accepted");
    check(httpsHostOf("https://user:pw@update.aestra.studio/x").empty(), "credentials in a URL are refused");
    check(httpsHostOf("https://").empty() && httpsHostOf("ftp://a").empty() && httpsHostOf("").empty(), "junk has no host");
    check(httpsHostOf("https://update.aestra.studio.evil.example/x") == "update.aestra.studio.evil.example", "a look-alike host is a different host");

    // The request: path, and the founder key only where it belongs.
    {
        FakeTransport t;
        t.reply = ok(kGood);
        const auto r = checkForUpdate(t, config("early"));
        check(r.status == UpdateCheckStatus::Available && r.manifest.version == "0.8.2", "a newer build is offered");
        check(t.seen.size() == 1 && t.seen[0].url == "https://update.aestra.studio/early/windows-x64/latest.json", "asks the channel and platform path");
        check(t.seen[0].method == "GET", "reads, never writes");
        check(t.seen[0].headers["Authorization"] == "Bearer aef_testkey0123456789", "early sends the founder key");
        check(r.detail.find("aef_") == std::string::npos, "the key never appears in the detail line");
    }
    {
        FakeTransport t;
        t.reply = ok(kStableGood);
        const auto r = checkForUpdate(t, config("stable"));
        check(r.status == UpdateCheckStatus::Available, "stable works the same");
        check(t.seen[0].headers.count("Authorization") == 0, "stable never sends the founder key, even when one is saved");
    }
    {
        FakeTransport t;
        t.reply = ok(kGood);
        UpdateConfig c = config("early");
        c.baseUrl = "https://update.aestra.studio/";
        checkForUpdate(t, c);
        check(t.seen[0].url == "https://update.aestra.studio/early/windows-x64/latest.json", "a trailing slash on the base is harmless");
    }

    // Versions.
    {
        FakeTransport t;
        t.reply = ok(kGood);
        check(run(t, config("early", "0.8.2")) == UpdateCheckStatus::UpToDate, "same version: nothing to do");
        check(run(t, config("early", "0.8.3")) == UpdateCheckStatus::UpToDate, "running something newer: nothing to do");
        check(run(t, config("early", "0.8.2-preview.3")) == UpdateCheckStatus::Available, "a preview is offered its own release");
    }

    // Config problems never reach the network.
    {
        FakeTransport t;
        t.reply = ok(kGood);
        check(run(t, config("early", "0.8.1", "")) == UpdateCheckStatus::Unauthorized && t.calls == 0, "early with no key asks nothing");
        UpdateConfig c = config("beta");
        check(run(t, c) == UpdateCheckStatus::BadConfig && t.calls == 0, "an unknown channel asks nothing");
        c = config("early");
        c.baseUrl = "http://update.aestra.studio";
        check(run(t, c) == UpdateCheckStatus::BadConfig && t.calls == 0, "a plain-http host asks nothing, so the key is never sent in clear");
        c = config("early");
        c.currentVersion.clear();
        check(run(t, c) == UpdateCheckStatus::BadConfig && t.calls == 0, "no version asks nothing");
    }

    // Answers.
    {
        FakeTransport t;
        t.reply = status(401);
        check(run(t, config("early")) == UpdateCheckStatus::Unauthorized, "401 means the key is not accepted");
        t.reply = status(403);
        check(run(t, config("early")) == UpdateCheckStatus::Unauthorized, "403 too");
        t.reply = status(404);
        check(run(t, config("early")) == UpdateCheckStatus::UpToDate, "nothing published yet is not an error");
        t.reply = status(500);
        check(run(t, config("early")) == UpdateCheckStatus::NetworkError, "a server error is a network error");
        t.reply = HttpResponse();
        t.reply.error = "could not connect";
        check(run(t, config("early")) == UpdateCheckStatus::NetworkError, "no answer at all is a network error");
        t.reply = ok(kOtherKey);
        check(run(t, config("early")) == UpdateCheckStatus::BadFeed, "a feed signed by someone else is refused");
        t.reply = ok(kStableChannel);
        check(run(t, config("early")) == UpdateCheckStatus::BadFeed, "a stable feed answering an early request is refused");
        t.reply = ok(kOtherPlatform);
        check(run(t, config("early")) == UpdateCheckStatus::BadFeed, "another platform's feed is refused");
        t.reply = ok(kOtherHost);
        check(run(t, config("early")) == UpdateCheckStatus::BadFeed, "a correctly signed feed that sends the download elsewhere is refused");
        t.reply = HttpResponse();
        t.reply.status = 200;
        t.reply.body = "<html>captive portal</html>";
        check(run(t, config("early")) == UpdateCheckStatus::BadFeed, "a captive portal's page is a bad feed, not a crash");
    }

    // Settings and the announcement rule.
    {
        UpdateSettings s;
        check(s.channel == "stable" && s.autoCheck && s.founderKey.empty(), "defaults: stable, automatic, no key");
        s.channel = "early";
        s.founderKey = "aef_abc";
        s.lastNotified = "0.8.2";
        const UpdateSettings back = parseUpdateSettings(serializeUpdateSettings(s));
        check(back.channel == "early" && back.founderKey == "aef_abc" && back.lastNotified == "0.8.2" && back.autoCheck, "settings round-trip");
        check(parseUpdateSettings("not json").channel == "stable", "a damaged file means defaults");
        check(parseUpdateSettings("{\"channel\":\"nightly\"}").channel == "stable", "an unknown channel in the file means stable");
        check(!parseUpdateSettings("{\"autoCheck\":false}").autoCheck, "automatic checks can be turned off");
        check(parseUpdateSettings(std::string("{\"founderKey\":\"") + std::string(500, 'a') + "\"}").founderKey.empty(), "an absurd key is ignored");

        UpdateSettings a;
        check(shouldAnnounceUpdate(UpdateCheckStatus::Available, "0.8.2", a), "a first announcement");
        a.lastNotified = "0.8.2";
        check(!shouldAnnounceUpdate(UpdateCheckStatus::Available, "0.8.2", a), "the same version is announced once");
        check(shouldAnnounceUpdate(UpdateCheckStatus::Available, "0.8.3", a), "a newer one is announced again");
        a.autoCheck = false;
        check(!shouldAnnounceUpdate(UpdateCheckStatus::Available, "0.9.0", a), "automatic checks off: no unprompted message");
        a.autoCheck = true;
        check(!shouldAnnounceUpdate(UpdateCheckStatus::UpToDate, "0.9.0", a) && !shouldAnnounceUpdate(UpdateCheckStatus::NetworkError, "", a),
              "only an available update is announced");
    }

    // What the Settings page says.
    {
        using Tone = UpdateStatusText::Tone;
        UpdateSettings st;
        UpdateCheckResult none;
        auto text = describeUpdateStatus(true, false, none, st, "0.8.1");
        check(text.headline.find("Checking") == 0, "while checking it says so");
        text = describeUpdateStatus(false, false, none, st, "0.8.1");
        check(text.headline == "Not checked yet" && text.detail.find("once a day") != std::string::npos, "before the first check: when it will look");
        st.autoCheck = false;
        text = describeUpdateStatus(false, false, none, st, "0.8.1");
        check(text.detail.find("Check now") != std::string::npos, "with automatic checks off it points at the button");
        st.autoCheck = true;

        UpdateCheckResult r;
        r.status = UpdateCheckStatus::UpToDate;
        text = describeUpdateStatus(false, true, r, st, "0.8.1");
        check(text.headline == "You're up to date" && text.tone == Tone::Good && text.detail.find("0.8.1") != std::string::npos && text.detail.find("stable") != std::string::npos,
              "up to date names the version and the channel");
        st.channel = "early";
        check(describeUpdateStatus(false, true, r, st, "0.8.1").detail.find("early access") != std::string::npos, "and says early access for founders");

        r.status = UpdateCheckStatus::Available;
        r.manifest.version = "0.8.2";
        r.manifest.size = 6018061;
        text = describeUpdateStatus(false, true, r, st, "0.8.1");
        check(text.headline == "Aestra 0.8.2 is available" && text.detail.find("5.7 MB") != std::string::npos, "available names the version and the download size");

        r = UpdateCheckResult();
        r.status = UpdateCheckStatus::Unauthorized;
        text = describeUpdateStatus(false, true, r, st, "0.8.1");
        check(text.headline == "Enter your founder key" && text.tone == Tone::Warn, "early without a key asks for one");
        st.founderKey = "aef_x";
        check(describeUpdateStatus(false, true, r, st, "0.8.1").headline == "The founder key was not accepted", "a refused key is said plainly");
        r.status = UpdateCheckStatus::NetworkError;
        r.detail = "update host answered HTTP 500";
        text = describeUpdateStatus(false, true, r, st, "0.8.1");
        check(text.headline.find("Couldn't reach") == 0 && text.detail.find("HTTP 500") != std::string::npos && text.detail.find("hour") != std::string::npos,
              "a network failure says what happened and when it retries");
        r.status = UpdateCheckStatus::BadFeed;
        check(describeUpdateStatus(false, true, r, st, "0.8.1").detail.find("Nothing was downloaded") != std::string::npos, "an untrusted feed says nothing was installed");
        check(describeUpdateStatus(true, true, r, st, "0.8.1").headline.find("Checking") == 0, "checking wins over an old result");
    }

    // The background checker.
    {
        auto transport = std::make_unique<FakeTransport>();
        FakeTransport* t = transport.get();
        t->reply = ok(kGood);
        std::atomic<int> configAsked{0};
        std::atomic<bool> automatic{true};
        std::promise<UpdateCheckResult> first;
        std::atomic<bool> firstSet{false};
        std::atomic<int> delivered{0};
        UpdateChecker::Timing timing;
        timing.initialDelay = std::chrono::milliseconds(20);
        timing.interval = std::chrono::hours(24);
        timing.retryAfterFailure = std::chrono::hours(1);
        UpdateChecker checker(std::move(transport), [&] { ++configAsked; return config("early"); },
                              [&](const UpdateCheckResult& r) {
                                  ++delivered;
                                  if (!firstSet.exchange(true)) first.set_value(r);
                              },
                              timing);
        checker.setAutomaticAllowed([&] { return automatic.load(); });
        check(!checker.hasResult(), "no result before the first check");
        checker.start();
        auto future = first.get_future();
        check(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "the first check runs by itself after the delay");
        check(future.get().status == UpdateCheckStatus::Available && checker.hasResult(), "and its answer is kept");
        check(configAsked == 1, "the configuration is asked for before each check");

        checker.checkNow();
        for (int i = 0; i < 200 && delivered < 2; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(delivered == 2 && configAsked == 2, "checkNow runs one right away");

        automatic = false;
        checker.checkNow();
        for (int i = 0; i < 200 && delivered < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(delivered == 3, "checkNow works even with automatic checks off");
        checker.stop();
        checker.stop(); // twice is fine
    }
    {
        // Automatic checks off: the scheduled check touches nothing.
        auto transport = std::make_unique<FakeTransport>();
        FakeTransport* t = transport.get();
        t->reply = ok(kGood);
        UpdateChecker::Timing timing;
        timing.initialDelay = std::chrono::milliseconds(10);
        timing.interval = std::chrono::hours(24);
        UpdateChecker checker(std::move(transport), [] { return config("early"); }, nullptr, timing);
        checker.setAutomaticAllowed([] { return false; });
        checker.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        check(t->calls == 0 && !checker.hasResult(), "automatic checks off: the schedule makes no request");
        checker.stop();
    }
    {
        // A failure is retried sooner than the daily interval.
        auto transport = std::make_unique<FakeTransport>();
        FakeTransport* t = transport.get();
        t->reply = status(500);
        UpdateChecker::Timing timing;
        timing.initialDelay = std::chrono::milliseconds(5);
        timing.interval = std::chrono::hours(24);
        timing.retryAfterFailure = std::chrono::milliseconds(30);
        UpdateChecker checker(std::move(transport), [] { return config("early"); }, nullptr, timing);
        checker.start();
        for (int i = 0; i < 300 && t->calls < 3; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(t->calls >= 3, "a failed check is retried at the shorter interval");
        checker.stop();
    }

    if (g_failures == 0) std::cout << "update client: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
