#pragma once

#include "EntitlementProfile.h"
#include "LicenseRefreshClient.h"
#include "LicenseTier.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace License {

// Shared Ed25519 detached-signature verifier used by the license gate and tests.
bool verifyEd25519Detached(const std::string& payload, const std::vector<unsigned char>& signature,
                           const unsigned char publicKey[32]);
std::string currentDeviceHashForRefresh();

class LicenseGate {
public:
    static void initialize();
    static LicenseTier currentTier();
    static EntitlementProfile currentProfile();
    static bool canAccess(Feature feature);
    static void refreshAsync();
    static bool installLeaseBlobForRefresh(const std::string& leaseBlob, std::string& message);
    static int64_t secondsUntilExpiry();
};

class LicenseGateLeaseInstaller final : public ILeaseInstaller {
public:
    bool installLeaseBlob(const std::string& leaseBlob, std::string& message) override;
};

} // namespace License
} // namespace Aestra
