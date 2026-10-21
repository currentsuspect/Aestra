// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Asking update.aestra.studio whether a newer build exists for this channel and platform (see UpdateFeed.h for the
// format and tools/update/README.md for the host). One request, one answer, no side effects: downloading and installing
// are separate steps the user starts.
//
// Two rules protect the founder key:
//   * it is sent only over https, and only to the update host the app was configured with;
//   * a feed whose download URL is on any other host is refused, so a feed can never steer the key (or the user) elsewhere.

#include "HttpTransport.h"
#include "UpdateFeed.h"

#include <string>

namespace Aestra {
namespace License {

struct UpdateConfig {
    std::string baseUrl = "https://update.aestra.studio";
    std::string channel = "stable";       ///< "stable" or "early"
    std::string founderKey;               ///< only the early channel uses it
    std::string currentVersion;           ///< what is running now
    std::string platform;                 ///< currentUpdatePlatform()
    unsigned char publicKey[32] = {0};    ///< the key feeds must be signed with
    int timeoutMs = 10000;
};

enum class UpdateCheckStatus {
    UpToDate,      ///< nothing newer (or nothing published for this channel yet)
    Available,     ///< a newer build exists: see manifest
    Unauthorized,  ///< early channel without a founder key, or the host refused it
    NetworkError,  ///< could not reach the host, or it answered with an error
    BadFeed,       ///< reached it, but the answer cannot be trusted or used
    BadConfig,     ///< not a channel we know, or a base URL that is not https
};

struct UpdateCheckResult {
    UpdateCheckStatus status = UpdateCheckStatus::NetworkError;
    UpdateManifest manifest; ///< filled when status == Available
    std::string detail;      ///< short, for the log (never contains the founder key)
};

/// "https://host/path" -> "host" (lower case, no port, no credentials); empty if it is not an https URL.
std::string httpsHostOf(const std::string& url);

UpdateCheckResult checkForUpdate(IHttpTransport& transport, const UpdateConfig& config);

} // namespace License
} // namespace Aestra
