// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "HttpClient.h"

#include <curl/curl.h>

#include <mutex>

namespace Aestra {
namespace MuseAgent {

namespace {

size_t writeToString(char* data, size_t size, size_t nmemb, void* userData) {
    auto* out = static_cast<std::string*>(userData);
    out->append(data, size * nmemb);
    return size * nmemb;
}

void ensureCurlInit() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

HttpResponse httpPostJson(const std::string& url,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& jsonBody, long timeoutSeconds) {
    ensureCurlInit();
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    curl_slist* headerList = nullptr;
    headerList = curl_slist_append(headerList, "content-type: application/json");
    for (const auto& [key, value] : headers) {
        headerList = curl_slist_append(headerList, (key + ": " + value).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        response.error = curl_easy_strerror(result);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace MuseAgent
} // namespace Aestra
