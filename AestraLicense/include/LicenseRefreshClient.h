#pragma once

#include <string>

namespace Aestra {
namespace License {

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
    LicenseRefreshResult refresh(const LicenseRefreshRequest& request) const;

    static LicenseRefreshResult parseRefreshResponse(const std::string& jsonText);
    static bool isUsableLeaseBlob(const LicenseRefreshResult& result);
};

} // namespace License
} // namespace Aestra
