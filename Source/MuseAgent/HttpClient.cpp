// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "HttpClient.h"

#include <curl/curl.h>

#include <mutex>

namespace Aestra {
namespace MuseAgent {

namespace {

struct BodySink {
    std::string* body;
    size_t cap;
    bool overflowed = false;
};

size_t writeToString(char* data, size_t size, size_t nmemb, void* userData) {
    auto* sink = static_cast<BodySink*>(userData);
    const size_t length = size * nmemb;
    if (!appendWithinCap(*sink->body, data, length, sink->cap)) {
        // Returning short makes libcurl abort the transfer (CURLE_WRITE_ERROR).
        sink->overflowed = true;
        return 0;
    }
    return length;
}

void ensureCurlInit() {
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

HttpResponse httpPostJson(const std::string& url,
                          const std::vector<std::pair<std::string, std::string>>& headers,
                          const std::string& jsonBody, long timeoutSeconds,
                          size_t maxResponseBytes) {
    ensureCurlInit();
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    // A failed append would silently drop a header, and the request would go
    // out without its auth, so treat it as a failure of the whole call.
    curl_slist* headerList = nullptr;
    bool headersOk = true;
    const auto addHeader = [&](const std::string& line) {
        curl_slist* grown = curl_slist_append(headerList, line.c_str());
        if (grown == nullptr) {
            headersOk = false;
            return;
        }
        headerList = grown;
    };
    addHeader("content-type: application/json");
    for (const auto& [key, value] : headers) {
        addHeader(key + ": " + value);
    }
    if (!headersOk) {
        response.error = "out of memory building request headers";
        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    BodySink sink{&response.body, maxResponseBytes};
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    // Second line of defence: refuse up front when the server declares a size over the cap.
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maxResponseBytes));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    const CURLcode result = curl_easy_perform(curl);
    if (sink.overflowed || result == CURLE_FILESIZE_EXCEEDED) {
        response.bodyTooLarge = true;
        response.error = "response exceeded " + std::to_string(maxResponseBytes) + " bytes";
    } else if (result != CURLE_OK) {
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
