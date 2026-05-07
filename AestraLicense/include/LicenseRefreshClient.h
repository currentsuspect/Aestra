#pragma once

#include "HttpTransport.h"

#include <string>

namespace Aestra {
namespace License {

class IHttpTransport;

enum class LicenseRefreshStatus {
    Success = 0,
    SyncUnavailable,
    Unauthorized,
    InvalidResponse,
    RejectedSignature,
};

struct LicenseRefreshRequest {
    std::string userId;
    std::string deviceHash;
};

struct LicenseRefreshResult {
    LicenseRefreshStatus status = LicenseRefreshStatus::SyncUnavailable;
    std::string payload;
    std::string canonical;
    std::string signatureHex;
    std::string leaseBlob;
    std::string keyId;
    std::string format;
    std::string message;
};

class ILeaseInstaller {
public:
    virtual ~ILeaseInstaller() = default;
    virtual bool installLeaseBlob(const std::string& leaseBlob, std::string& message) = 0;
};

class LicenseRefreshClient {
public:
    static LicenseRefreshResult refresh(const LicenseRefreshRequest& request, IHttpTransport& transport,
                                         const std::string& baseUrl, const std::string& sessionToken);

    static LicenseRefreshResult parseRefreshResponse(const std::string& jsonText);
    static bool isUsableLeaseBlob(const LicenseRefreshResult& result);
};

} // namespace License
} // namespace Aestra
