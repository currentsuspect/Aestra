// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "HttpLimits.h"

#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace MuseAgent {

/**
 * @brief Minimal blocking HTTPS POST via libcurl — all a provider adapter needs.
 */
struct HttpResponse {
    long status = 0;
    int retryAfterSeconds = -1; // Retry-After in delta-seconds form; -1 if absent or an HTTP-date
    std::string body;
    std::string error; // transport-level failure (status stays 0)
    bool bodyTooLarge = false; // the response exceeded the size cap; body holds a prefix, error is set
};

HttpResponse httpPostJson(const std::string& url,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& jsonBody, long timeoutSeconds = 300,
                          size_t maxResponseBytes = kDefaultMaxResponseBytes);

} // namespace MuseAgent
} // namespace Aestra
