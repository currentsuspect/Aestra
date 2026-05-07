#include "HttpTransport.h"

#ifdef AESTRA_LICENSE_HAS_CURL
#include <curl/curl.h>
#endif

#include <algorithm>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>

namespace Aestra {
namespace License {

namespace {
#ifdef AESTRA_LICENSE_HAS_CURL
std::once_flag g_curlInitOnce;

size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    const size_t byteCount = size * nmemb;
    body->append(ptr, byteCount);
    return byteCount;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isAllowedHeaderName(const std::string& name) {
    const std::string lower = lowerCopy(name);
    return lower == "authorization" || lower == "content-type" || lower == "accept";
}
#endif
} // namespace

HttpResponse UnavailableHttpTransport::send(const HttpRequest& request) {
    (void)request;

    HttpResponse response;
    response.status = 0;
    response.error = "HTTP transport is not configured.";
    return response;
}

HttpResponse CurlHttpTransport::send(const HttpRequest& request) {
#ifndef AESTRA_LICENSE_HAS_CURL
    (void)request;
    HttpResponse response;
    response.status = 0;
    response.error = "libcurl HTTP transport is not available in this build.";
    return response;
#else
    std::call_once(g_curlInitOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.status = 0;
        response.error = "HTTP transport initialization failed.";
        return response;
    }

    std::string responseBody;
    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        if (!isAllowedHeaderName(name)) {
            continue;
        }
        headers = curl_slist_append(headers, (name + ": " + value).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.timeoutMs));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeoutMs));
    if (headers != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if (request.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        if (!request.body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }
    }

    const CURLcode rc = curl_easy_perform(curl);
    long statusCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);
    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    response.status = static_cast<int>(statusCode);
    response.body = std::move(responseBody);
    if (rc != CURLE_OK) {
        response.status = 0;
        response.error = "HTTP request failed.";
    }
    return response;
#endif
}

bool curlHttpTransportAvailable() {
#ifdef AESTRA_LICENSE_HAS_CURL
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IHttpTransport> createDefaultHttpTransport() {
#ifdef AESTRA_LICENSE_HAS_CURL
    return std::make_unique<CurlHttpTransport>();
#else
    return std::make_unique<UnavailableHttpTransport>();
#endif
}

} // namespace License
} // namespace Aestra
