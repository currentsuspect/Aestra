// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The signed update feed: what the app will and will not believe, and how versions order. The fixtures are signed with a
// throwaway key that exists only for this test; the real update key never appears in the repository.

#include "UpdateFeed.h"

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

std::vector<unsigned char> key() {
    std::vector<unsigned char> out;
    const std::string hex = kPublicKeyHex;
    for (size_t i = 0; i < hex.size(); i += 2) out.push_back(static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

UpdateFeedError parse(const Signed& s, const std::string& channel = "early", const std::string& platform = "windows-x64") {
    return parseSignedUpdateFeed(document(s), key().data(), channel, platform).error;
}

} // namespace

int main() {
    // A good feed is read.
    {
        const auto r = parseSignedUpdateFeed(document(kGood), key().data(), "early", "windows-x64");
        check(r.ok(), "a correctly signed feed is accepted");
        check(r.manifest.version == "0.8.2" && r.manifest.size == 6017376 && r.manifest.notes == "Plugin windows on Linux.",
              "fields come through");
        check(r.manifest.url.rfind("https://", 0) == 0 && r.manifest.sha256.size() == 64, "url and hash come through");
    }

    // Trust: wrong key, tampered payload, tampered signature.
    check(parse(kOtherKey) == UpdateFeedError::BadSignature, "a feed signed by another key is refused");
    {
        std::string doc = document(kGood);
        const size_t at = doc.find("0.8.2");
        doc[at + 4] = '3'; // 0.8.3: a changed byte in the signed text
        check(parseSignedUpdateFeed(doc, key().data(), "early", "windows-x64").error == UpdateFeedError::BadSignature,
              "a payload changed after signing is refused");
        std::string sig = document(kGood);
        const size_t s = sig.find("\"signature\":\"") + 13;
        sig[s] = sig[s] == '0' ? '1' : '0';
        check(parseSignedUpdateFeed(sig, key().data(), "early", "windows-x64").error == UpdateFeedError::BadSignature,
              "a damaged signature is refused");
    }

    // Right signature, wrong audience: never install another channel's or platform's build.
    check(parse(kStableChannel) == UpdateFeedError::WrongChannel, "a stable feed is refused by the early channel");
    check(parse(kGood, "stable") == UpdateFeedError::WrongChannel, "an early feed is refused by the stable channel");
    check(parse(kOtherPlatform) == UpdateFeedError::WrongPlatform, "another platform's feed is refused");

    // Signed but unusable.
    check(parse(kHttpUrl) == UpdateFeedError::BadPayload, "a plain http download is refused");
    check(parse(kShortHash) == UpdateFeedError::BadPayload, "a hash that is not 64 hex characters is refused");
    check(parse(kZeroSize) == UpdateFeedError::BadPayload, "a zero-size package is refused");
    check(parse(kSchema2) == UpdateFeedError::BadPayload, "an unknown schema is refused rather than guessed at");
    check(parse(kNotJson) == UpdateFeedError::BadPayload, "a signed payload that is not JSON is refused");

    // Not even an envelope.
    for (const char* junk : {"", "{}", "not json", "{\"payload\":1,\"signature\":2}", "[]"})
        check(parseSignedUpdateFeed(junk, key().data(), "early", "windows-x64").error == UpdateFeedError::Malformed,
              std::string("junk is malformed: ") + junk);
    check(parseSignedUpdateFeed("{\"payload\":\"x\",\"signature\":\"zz\"}", key().data(), "early", "windows-x64").error ==
              UpdateFeedError::BadSignature,
          "a signature that is not hex is refused");

    // Version order.
    check(compareVersions("0.8.2", "0.8.1") > 0 && compareVersions("0.8.1", "0.8.2") < 0, "patch order");
    check(compareVersions("0.9.0", "0.8.12") > 0, "minor beats patch");
    check(compareVersions("1.0.0", "0.99.99") > 0, "major beats all");
    check(compareVersions("0.8.2", "0.8.2") == 0 && compareVersions("v0.8.2", "0.8.2") == 0, "equal, with or without a v");
    check(compareVersions("0.8", "0.8.0") == 0, "missing parts are zero");
    check(compareVersions("0.8.2-preview", "0.8.2") < 0, "a pre-release is older than its release");
    check(compareVersions("0.8.2", "0.8.1-preview") > 0, "a release is newer than the earlier pre-release");
    check(compareVersions("0.8.2-alpha.2", "0.8.2-alpha.10") < 0, "numeric pre-release parts compare as numbers");
    check(compareVersions("0.8.2-alpha", "0.8.2-beta") < 0, "text pre-release parts compare as text");
    check(compareVersions("0.8.2-alpha", "0.8.2-alpha.1") < 0, "a shorter pre-release is older");
    check(compareVersions("garbage", "0.0.0") == 0, "text that is not a version is 0.0.0");
    check(compareVersions("0.8.2+build5", "0.8.2") == 0, "build metadata does not matter");
    check(isNewerVersion("0.8.2", "0.8.1-alpha") && !isNewerVersion("0.8.1", "0.8.1"), "isNewerVersion");
    check(compareVersions("99999999999999999999.0.0", "1.0.0") > 0, "an absurd number does not overflow");

    check(!currentUpdatePlatform().empty(), "this build knows its platform");

    if (g_failures == 0) std::cout << "update feed: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
