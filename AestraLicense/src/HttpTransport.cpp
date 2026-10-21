#include "HttpTransport.h"

#ifdef AESTRA_LICENSE_HAS_CURL
#include <curl/curl.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    // [SEC-FIX] Reject responses exceeding a reasonable size to prevent memory exhaustion.
    constexpr size_t MAX_RESPONSE_BYTES = 8 * 1024 * 1024; // 8 MiB
    if (body->size() + byteCount > MAX_RESPONSE_BYTES) {
        return 0; // Tell libcurl to abort the transfer
    }
    body->append(ptr, byteCount);
    return byteCount;
}

#endif

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

uint64_t fileSizeOrZero(const std::string& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}
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
    // Follow redirects (bounded). The account host may 3xx apex -> www (e.g.
    // Vercel), and without this the request dies on the redirect instead of
    // reaching the API. CURL_REDIR_POST_ALL keeps POST bodies across 301/302/307/308
    // so login/verify calls aren't silently downgraded to GET.
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_POSTREDIR, static_cast<long>(CURL_REDIR_POST_ALL));
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


#ifdef AESTRA_LICENSE_HAS_CURL
namespace {
struct CurlDownloadState {
    CURL* curl = nullptr;
    std::string path;
    uint64_t resumeFrom = 0;
    uint64_t total = 0;      // the whole file's size when the server said
    uint64_t onDisk = 0;
    std::FILE* file = nullptr;
    long status = 0;
    bool cancelled = false;
    std::string error;
    const DownloadProgress* progress = nullptr;
};

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* st = static_cast<CurlDownloadState*>(userdata);
    const size_t count = size * nmemb;
    if (!st->file) {
        curl_easy_getinfo(st->curl, CURLINFO_RESPONSE_CODE, &st->status);
        if (st->status != 200 && st->status != 206) return count; // an error page is not the file: discard it
        // 206 continues the part file; 200 means the server sent it all again (it ignored the range), so start over.
        const bool append = st->status == 206 && st->resumeFrom > 0;
        st->file = std::fopen(st->path.c_str(), append ? "ab" : "wb");
        if (!st->file) {
            st->error = "cannot write the download file";
            return 0;
        }
        st->onDisk = append ? st->resumeFrom : 0;
    }
    if (std::fwrite(ptr, 1, count, st->file) != count) {
        st->error = "the disk refused the download (full?)";
        return 0;
    }
    st->onDisk += count;
    return count;
}

int downloadProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* st = static_cast<CurlDownloadState*>(userdata);
    if (!st->progress || !*st->progress) return 0;
    const uint64_t base = st->status == 206 ? st->resumeFrom : 0;
    const uint64_t total = dltotal > 0 ? base + static_cast<uint64_t>(dltotal) : 0;
    if (!(*st->progress)(base + static_cast<uint64_t>(dlnow), total)) {
        st->cancelled = true;
        return 1; // abort
    }
    return 0;
}
} // namespace
#endif

DownloadResult CurlHttpTransport::download(const DownloadRequest& request, const DownloadProgress& progress) {
    DownloadResult result;
#ifndef AESTRA_LICENSE_HAS_CURL
    (void)request;
    (void)progress;
    result.error = "libcurl HTTP transport is not available in this build.";
    return result;
#else
    std::call_once(g_curlInitOnce, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "HTTP transport initialization failed.";
        return result;
    }
    CurlDownloadState st;
    st.curl = curl;
    st.path = request.destPath;
    st.resumeFrom = request.resumeFrom;
    st.progress = &progress;

    curl_slist* headers = nullptr;
    for (const auto& [name, value] : request.headers) {
        if (isAllowedHeaderName(name)) headers = curl_slist_append(headers, (name + ": " + value).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"); // a download is https or nothing
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);     // the file is on the host the feed named, not wherever it points
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connectTimeoutMs));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L); // under 1 byte/s for the stall time = stalled
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(std::max(1, request.stallTimeoutMs / 1000)));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &st);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, downloadProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &st);
    if (request.resumeFrom > 0) curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(request.resumeFrom));
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    const CURLcode rc = curl_easy_perform(curl);
    if (st.status == 0) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &st.status);
    if (st.file) std::fclose(st.file);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    result.status = static_cast<int>(st.status);
    result.cancelled = st.cancelled;
    result.bytesOnDisk = fileSizeOrZero(request.destPath);
    if (rc != CURLE_OK && !st.cancelled) {
        result.error = !st.error.empty() ? st.error : "the download was interrupted";
        if (st.status != 200 && st.status != 206) result.status = 0;
    }
    return result;
#endif
}

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

struct WinHandle {
    HINTERNET h = nullptr;
    WinHandle() = default;
    explicit WinHandle(HINTERNET handle) : h(handle) {}
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    ~WinHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

// One request, opened and sent: everything WinHTTP needs between "here is a URL" and "read the body".
struct WinRequest {
    WinHandle session, connect, request;
    int status = 0;
    std::string error;

    bool open(const std::string& method, const std::string& url, const std::map<std::string, std::string>& headers,
              const std::string& body, int connectMs, int receiveMs, bool followRedirects, uint64_t resumeFrom) {
        const std::wstring wurl = widen(url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = 255;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 2047;
        parts.lpszExtraInfo = extra; // the ?query part, which WinHTTP keeps apart from the path
        parts.dwExtraInfoLength = 2047;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
            error = "the update address is not a valid URL";
            return false;
        }
        const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
        session.h = WinHttpOpen(L"Aestra", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            // Automatic proxy needs Windows 8.1+; older systems use the machine default.
            session.h = WinHttpOpen(L"Aestra", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (!session) {
            error = "HTTP transport initialization failed.";
            return false;
        }
        WinHttpSetTimeouts(session.h, connectMs, connectMs, connectMs, receiveMs);
        connect.h = WinHttpConnect(session.h, parts.lpszHostName, parts.nPort, 0);
        if (!connect) {
            error = "could not connect";
            return false;
        }
        const std::wstring wmethod = widen(method);
        const std::wstring object = std::wstring(parts.lpszUrlPath) + parts.lpszExtraInfo;
        request.h = WinHttpOpenRequest(connect.h, wmethod.c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
        if (!request) {
            error = "could not open the request";
            return false;
        }
        DWORD redirectPolicy = followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        std::wstring headerText;
        for (const auto& [name, value] : headers) {
            if (!isAllowedHeaderName(name)) continue;
            headerText += widen(name) + L": " + widen(value) + L"\r\n";
        }
        if (resumeFrom > 0) headerText += L"Range: bytes=" + std::to_wstring(resumeFrom) + L"-\r\n";
        if (!headerText.empty()) WinHttpAddRequestHeaders(request.h, headerText.c_str(), static_cast<DWORD>(headerText.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

        LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
        const DWORD bodyLen = static_cast<DWORD>(body.size());
        if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPtr, bodyLen, bodyLen, 0) ||
            !WinHttpReceiveResponse(request.h, nullptr)) {
            error = "could not reach the server";
            return false;
        }
        DWORD code = 0, size = sizeof(code);
        if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) {
            error = "no status in the answer";
            return false;
        }
        status = static_cast<int>(code);
        return true;
    }

    uint64_t contentLength() {
        wchar_t buf[32] = {};
        DWORD size = sizeof(buf);
        if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, buf, &size, WINHTTP_NO_HEADER_INDEX)) return 0;
        return static_cast<uint64_t>(_wtoi64(buf));
    }
};

} // namespace

HttpResponse WinHttpTransport::send(const HttpRequest& req) {
    HttpResponse response;
    WinRequest w;
    const int ms = std::max(1000, req.timeoutMs);
    if (!w.open(req.method.empty() ? "GET" : req.method, req.url, req.headers, req.body, ms, ms, req.followRedirects, 0)) {
        response.error = w.error;
        return response;
    }
    constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024; // the same cap as the curl transport
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(w.request.h, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(w.request.h, chunk.data(), available, &read) || read == 0) break;
        if (body.size() + read > kMaxResponseBytes) {
            response.error = "HTTP request failed.";
            return response;
        }
        body.append(chunk.data(), read);
    }
    response.status = w.status;
    response.body = std::move(body);
    return response;
}

DownloadResult WinHttpTransport::download(const DownloadRequest& req, const DownloadProgress& progress) {
    DownloadResult result;
    WinRequest w;
    if (!w.open("GET", req.url, req.headers, {}, req.connectTimeoutMs, req.stallTimeoutMs, false, req.resumeFrom)) {
        result.error = w.error;
        return result;
    }
    result.status = w.status;
    if (w.status != 200 && w.status != 206) return result; // the caller reads the status; nothing to save

    const bool append = w.status == 206 && req.resumeFrom > 0; // 200 after a range request: the server sent it all again
    const uint64_t base = append ? req.resumeFrom : 0;
    const uint64_t length = w.contentLength();
    const uint64_t total = length > 0 ? base + length : 0;
    std::ofstream out(req.destPath, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) {
        result.error = "cannot write the download file";
        result.status = 0;
        return result;
    }
    uint64_t done = base;
    std::string chunk(64 * 1024, '\0');
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(w.request.h, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
            result.error = "the download was interrupted";
            break;
        }
        if (read == 0) break; // done
        out.write(chunk.data(), read);
        if (!out) {
            result.error = "the disk refused the download (full?)";
            break;
        }
        done += read;
        if (progress && !progress(done, total)) {
            result.cancelled = true;
            break;
        }
    }
    out.close();
    result.bytesOnDisk = fileSizeOrZero(req.destPath);
    return result;
}
#else
HttpResponse WinHttpTransport::send(const HttpRequest&) {
    HttpResponse response;
    response.error = "WinHTTP exists only on Windows.";
    return response;
}
DownloadResult WinHttpTransport::download(const DownloadRequest&, const DownloadProgress&) {
    DownloadResult result;
    result.error = "WinHTTP exists only on Windows.";
    return result;
}
#endif

bool curlHttpTransportAvailable() {
#ifdef AESTRA_LICENSE_HAS_CURL
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IHttpTransport> createDefaultHttpTransport() {
#ifdef _WIN32
    return std::make_unique<WinHttpTransport>(); // Windows' own stack: always there, follows the system proxy
#elif defined(AESTRA_LICENSE_HAS_CURL)
    return std::make_unique<CurlHttpTransport>();
#else
    return std::make_unique<UnavailableHttpTransport>();
#endif
}

} // namespace License
} // namespace Aestra
