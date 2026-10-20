// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The signed update feed (update.aestra.studio). One small file per channel and platform:
//
//   { "payload": "<JSON text>", "signature": "<128 hex chars: Ed25519 over the payload bytes>" }
//
// The payload is a string, not a nested object, so the bytes that were signed are exactly the bytes verified: nothing
// is re-serialised, nothing depends on key order or number formatting. Payload (schema 1):
//
//   { "schema":1, "channel":"stable"|"early", "platform":"windows-x64"|"linux-x64", "version":"0.8.2",
//     "published":"2026-10-20T16:00:00Z", "notes":"...", "url":"https://...", "sha256":"<64 hex>", "size":123456 }
//
// Nothing in the feed is trusted before the signature checks out, and a feed for another channel or platform is
// refused even when its signature is good (a stable build must never be told to install an early one, or the reverse).

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace License {

struct UpdateManifest {
    std::string channel;
    std::string platform;
    std::string version;
    std::string published;
    std::string notes;
    std::string url;
    std::string sha256; ///< lower-case hex of the downloaded file
    uint64_t size = 0;  ///< bytes of the downloaded file
};

enum class UpdateFeedError {
    None,
    Malformed,        ///< not JSON, or the envelope is missing a field
    BadSignature,     ///< the signature does not match the payload and key
    BadPayload,       ///< signed, but a field is missing or out of bounds
    WrongChannel,
    WrongPlatform,
};

struct UpdateFeedResult {
    UpdateFeedError error = UpdateFeedError::None;
    UpdateManifest manifest;
    bool ok() const { return error == UpdateFeedError::None; }
};

/// Check and read a feed document. @p publicKey is the 32-byte Ed25519 key the app was built with.
UpdateFeedResult parseSignedUpdateFeed(const std::string& document, const unsigned char publicKey[32],
                                       const std::string& expectedChannel, const std::string& expectedPlatform);

/// Semantic-version order: -1, 0 or 1. Missing parts count as 0; a pre-release ("0.8.2-preview") is older than the
/// release it leads to ("0.8.2"). Text that is not a version compares as 0.0.0.
int compareVersions(const std::string& a, const std::string& b);

/// Whether @p offered is a version worth installing over @p current.
inline bool isNewerVersion(const std::string& offered, const std::string& current) {
    return compareVersions(offered, current) > 0;
}

/// "windows-x64" / "linux-x64" / "macos-arm64" for the running build.
std::string currentUpdatePlatform();

} // namespace License
} // namespace Aestra
