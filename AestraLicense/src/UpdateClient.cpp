// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "UpdateClient.h"

#include <algorithm>
#include <cctype>

namespace Aestra {
namespace License {

std::string httpsHostOf(const std::string& url) {
    const std::string scheme = "https://";
    if (url.size() <= scheme.size() || url.compare(0, scheme.size(), scheme) != 0) return {};
    size_t end = url.find_first_of("/?#", scheme.size());
    std::string authority = url.substr(scheme.size(), end == std::string::npos ? std::string::npos : end - scheme.size());
    if (authority.find('@') != std::string::npos) return {}; // credentials in a URL: never
    const size_t colon = authority.find(':');
    if (colon != std::string::npos) authority.resize(colon);
    if (authority.empty()) return {};
    for (char& c : authority) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return authority;
}

UpdateCheckResult checkForUpdate(IHttpTransport& transport, const UpdateConfig& config) {
    UpdateCheckResult result;
    auto done = [&result](UpdateCheckStatus status, std::string detail) {
        result.status = status;
        result.detail = std::move(detail);
        if (status != UpdateCheckStatus::Available) result.manifest = {};
        return result;
    };

    if (config.channel != "stable" && config.channel != "early") return done(UpdateCheckStatus::BadConfig, "unknown channel");
    const std::string host = httpsHostOf(config.baseUrl);
    if (host.empty()) return done(UpdateCheckStatus::BadConfig, "the update host must be an https URL");
    if (config.platform.empty() || config.currentVersion.empty()) return done(UpdateCheckStatus::BadConfig, "no platform or version");
    if (config.channel == "early" && config.founderKey.empty()) return done(UpdateCheckStatus::Unauthorized, "the early channel needs a founder key");

    HttpRequest request;
    request.method = "GET";
    std::string base = config.baseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    request.url = base + "/" + config.channel + "/" + config.platform + "/latest.json";
    request.timeoutMs = config.timeoutMs;
    request.headers["Accept"] = "application/json";
    if (config.channel == "early") request.headers["Authorization"] = "Bearer " + config.founderKey;

    const HttpResponse response = transport.send(request);
    if (response.status == 401 || response.status == 403) return done(UpdateCheckStatus::Unauthorized, "the update host refused the key");
    if (response.status == 404) return done(UpdateCheckStatus::UpToDate, "nothing published for this channel yet");
    if (response.status != 200) {
        return done(UpdateCheckStatus::NetworkError, response.status == 0 ? "could not reach the update host: " + response.error
                                                                         : "update host answered HTTP " + std::to_string(response.status));
    }

    const UpdateFeedResult feed = parseSignedUpdateFeed(response.body, config.publicKey, config.channel, config.platform);
    if (!feed.ok()) {
        switch (feed.error) {
        case UpdateFeedError::BadSignature: return done(UpdateCheckStatus::BadFeed, "the feed's signature does not check out");
        case UpdateFeedError::WrongChannel: return done(UpdateCheckStatus::BadFeed, "the feed is for another channel");
        case UpdateFeedError::WrongPlatform: return done(UpdateCheckStatus::BadFeed, "the feed is for another platform");
        default: return done(UpdateCheckStatus::BadFeed, "the feed cannot be read");
        }
    }
    if (httpsHostOf(feed.manifest.url) != host) return done(UpdateCheckStatus::BadFeed, "the download is not on the update host");

    if (!isNewerVersion(feed.manifest.version, config.currentVersion)) return done(UpdateCheckStatus::UpToDate, "up to date");
    result.manifest = feed.manifest;
    return done(UpdateCheckStatus::Available, "version " + feed.manifest.version + " is available");
}

} // namespace License
} // namespace Aestra
