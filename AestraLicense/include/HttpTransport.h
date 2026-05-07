#pragma once

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
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

class UnavailableHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
};

class CurlHttpTransport final : public IHttpTransport {
public:
    HttpResponse send(const HttpRequest& request) override;
};

bool curlHttpTransportAvailable();
std::unique_ptr<IHttpTransport> createDefaultHttpTransport();

} // namespace License
} // namespace Aestra
