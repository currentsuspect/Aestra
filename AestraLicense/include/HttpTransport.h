#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace Aestra {
namespace License {

struct HttpRequest {
    std::string method;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutMs = 5000;
    /// Follow 3xx answers (bounded). The update client turns this off: a feed that moves the request somewhere else
    /// is not one to follow with a founder key attached.
    bool followRedirects = true;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
};

/// A download straight to a file, resumable: @p resumeFrom bytes are already there and the server is asked for the rest.
struct DownloadRequest {
    std::string url;
    std::map<std::string, std::string> headers;
    std::string destPath;
    uint64_t resumeFrom = 0;
    int connectTimeoutMs = 15000;
    int stallTimeoutMs = 30000; ///< no bytes for this long: give up (the part file stays, so it can resume)
};

struct DownloadResult {
    int status = 0;              ///< the server's answer (200 whole, 206 resumed); 0 = never got one
    uint64_t bytesOnDisk = 0;    ///< the size of destPath afterwards
    bool cancelled = false;
    std::string error;
};

/// Called as bytes arrive with (bytes on disk, total expected or 0 if unknown). Return false to cancel.
using DownloadProgress = std::function<bool(uint64_t done, uint64_t total)>;

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
    /// Not every transport can download; the default says so, which keeps small test fakes small.
    virtual DownloadResult download(const DownloadRequest& request, const DownloadProgress& progress) {
        (void)request;
        (void)progress;
        DownloadResult result;
        result.error = "this transport cannot download files";
        return result;
    }
};

class UnavailableHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
};

class CurlHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
    DownloadResult download(const DownloadRequest& request, const DownloadProgress& progress) override;
};

/// Windows' own HTTP stack (WinHTTP): no library to ship, and it follows the system proxy settings. The default on
/// Windows, where the early-access build has no libcurl (so update checks and account sync had no transport at all).
class WinHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
    DownloadResult download(const DownloadRequest& request, const DownloadProgress& progress) override;
};

bool curlHttpTransportAvailable();
std::unique_ptr<IHttpTransport> createDefaultHttpTransport();

} // namespace License
} // namespace Aestra
