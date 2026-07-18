// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

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
    std::string body;
    std::string error; // transport-level failure (status stays 0)
};

HttpResponse httpPostJson(const std::string& url,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& jsonBody, long timeoutSeconds = 300);

} // namespace MuseAgent
} // namespace Aestra
