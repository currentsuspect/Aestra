#pragma once

#include "EntitlementProfile.h"
#include "LicenseRefreshClient.h"
#include "LicenseTier.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace License {

inline constexpr unsigned char AESTRA_LICENSE_PUBKEY[32] = {
    // Development/test verification public key for public repository builds.
    // This is NOT a production private signing key and can be committed safely.
    // Replace in private/release pipelines with the intended production verification key.
    // Key label: AESTRA_DEV_TEST_PUBKEY_V2
    // Hex: b81de93ce731a1509249b6d07abed5061ae12e07e884b9e90b422f2c40a7fe18
    0xb8, 0x1d, 0xe9, 0x3c, 0xe7, 0x31, 0xa1, 0x50, 0x92, 0x49, 0xb6, 0xd0, 0x7a, 0xbe, 0xd5, 0x06,
    0x1a, 0xe1, 0x2e, 0x07, 0xe8, 0x84, 0xb9, 0xe9, 0x0b, 0x42, 0x2f, 0x2c, 0x40, 0xa7, 0xfe, 0x18,
};

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
