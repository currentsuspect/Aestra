// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateFeed.h"

#include "AestraJSON.h"
#include "LicenseGate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Aestra {
namespace License {

namespace {

constexpr size_t kMaxNotesBytes = 8 * 1024;
constexpr size_t kMaxUrlBytes = 2048;
constexpr uint64_t kMaxPackageBytes = 2ull * 1024 * 1024 * 1024;

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool isHex(const std::string& s, size_t length) {
    return s.size() == length && std::all_of(s.begin(), s.end(), [](char c) { return hexNibble(c) >= 0; });
}

bool decodeHex(const std::string& hex, std::vector<unsigned char>& out) {
    out.clear();
    if (hex.size() % 2 != 0) return false;
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexNibble(hex[i]), lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<unsigned char>(hi * 16 + lo));
    }
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct ParsedVersion {
    long numbers[3] = {0, 0, 0};
    std::vector<std::string> pre; ///< dot-separated pre-release identifiers, empty for a release
};

ParsedVersion parseVersion(const std::string& text) {
    ParsedVersion v;
    size_t i = 0;
    if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) ++i;
    for (int part = 0; part < 3; ++part) {
        long n = 0;
        bool any = false;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            n = std::min(n * 10 + (text[i] - '0'), 1000000L);
            ++i;
            any = true;
        }
        v.numbers[part] = any ? n : 0;
        if (i < text.size() && text[i] == '.' && part < 2) ++i;
        else break;
    }
    if (i < text.size() && text[i] == '-') {
        std::string current;
        for (++i; i < text.size() && text[i] != '+'; ++i) {
            if (text[i] == '.') {
                v.pre.push_back(current);
                current.clear();
            } else {
                current += text[i];
            }
        }
        v.pre.push_back(current);
    }
    return v;
}

bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

} // namespace

int compareVersions(const std::string& a, const std::string& b) {
    const ParsedVersion x = parseVersion(a), y = parseVersion(b);
    for (int i = 0; i < 3; ++i)
        if (x.numbers[i] != y.numbers[i]) return x.numbers[i] < y.numbers[i] ? -1 : 1;
    if (x.pre.empty() && y.pre.empty()) return 0;
    if (x.pre.empty()) return 1; // a release is newer than its own pre-release
    if (y.pre.empty()) return -1;
    for (size_t i = 0; i < std::min(x.pre.size(), y.pre.size()); ++i) {
        const std::string &p = x.pre[i], &q = y.pre[i];
        if (p == q) continue;
        const bool pn = allDigits(p), qn = allDigits(q);
        if (pn && qn) {
            const long pv = std::strtol(p.c_str(), nullptr, 10), qv = std::strtol(q.c_str(), nullptr, 10);
            return pv < qv ? -1 : 1;
        }
        if (pn != qn) return pn ? -1 : 1; // numeric identifiers sort before text ones
        return p < q ? -1 : 1;
    }
    if (x.pre.size() == y.pre.size()) return 0;
    return x.pre.size() < y.pre.size() ? -1 : 1;
}

std::string currentUpdatePlatform() {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
    return "macos-arm64";
#else
    return "linux-x64";
#endif
}

const unsigned char* bundledUpdatePublicKey() {
    static const std::vector<unsigned char> key = [] {
        std::vector<unsigned char> bytes;
        decodeHex(AESTRA_UPDATE_PUBLIC_KEY_HEX, bytes);
        bytes.resize(32);
        return bytes;
    }();
    return key.data();
}

UpdateFeedResult parseSignedUpdateFeed(const std::string& document, const unsigned char publicKey[32],
                                       const std::string& expectedChannel, const std::string& expectedPlatform) {
    UpdateFeedResult result;
    auto fail = [&result](UpdateFeedError e) {
        result.error = e;
        result.manifest = {};
        return result;
    };

    JSON envelope;
    try {
        envelope = JSON::parse(document);
    } catch (...) {
        return fail(UpdateFeedError::Malformed);
    }
    if (!envelope.isObject() || !envelope["payload"].isString() || !envelope["signature"].isString())
        return fail(UpdateFeedError::Malformed);

    const std::string& payload = envelope["payload"].asString();
    std::vector<unsigned char> signature;
    if (!decodeHex(envelope["signature"].asString(), signature) || signature.size() != 64)
        return fail(UpdateFeedError::BadSignature);
    if (!verifyEd25519Detached(payload, signature, publicKey)) return fail(UpdateFeedError::BadSignature);

    // Signed. Everything below is about whether we can use it, not whether we can trust it.
    JSON p;
    try {
        p = JSON::parse(payload);
    } catch (...) {
        return fail(UpdateFeedError::BadPayload);
    }
    if (!p.isObject() || !p["schema"].isNumber() || p["schema"].asInt() != 1) return fail(UpdateFeedError::BadPayload);
    for (const char* key : {"channel", "platform", "version", "url", "sha256"})
        if (!p[key].isString()) return fail(UpdateFeedError::BadPayload);
    if (!p["size"].isNumber()) return fail(UpdateFeedError::BadPayload);

    UpdateManifest m;
    m.channel = p["channel"].asString();
    m.platform = p["platform"].asString();
    m.version = p["version"].asString();
    m.url = p["url"].asString();
    m.sha256 = lower(p["sha256"].asString());
    if (p["published"].isString()) m.published = p["published"].asString();
    if (p["notes"].isString()) m.notes = p["notes"].asString();
    const double size = p["size"].asNumber();

    if (m.channel != expectedChannel) return fail(UpdateFeedError::WrongChannel);
    if (m.platform != expectedPlatform) return fail(UpdateFeedError::WrongPlatform);
    if (m.version.empty() || m.version.size() > 64 || !std::isdigit(static_cast<unsigned char>(m.version[0])))
        return fail(UpdateFeedError::BadPayload);
    if (m.url.rfind("https://", 0) != 0 || m.url.size() > kMaxUrlBytes) return fail(UpdateFeedError::BadPayload);
    if (!isHex(m.sha256, 64)) return fail(UpdateFeedError::BadPayload);
    if (!(size > 0) || size > static_cast<double>(kMaxPackageBytes)) return fail(UpdateFeedError::BadPayload);
    m.size = static_cast<uint64_t>(size);
    if (m.notes.size() > kMaxNotesBytes) m.notes.resize(kMaxNotesBytes);

    result.manifest = std::move(m);
    return result;
}

} // namespace License
} // namespace Aestra
