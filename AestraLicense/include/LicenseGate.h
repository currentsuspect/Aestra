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
    // Key label: AESTRA_DEV_TEST_PUBKEY_V1
    // Hex: 03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8
    0x03, 0xa1, 0x07, 0xbf, 0xf3, 0xce, 0x10, 0xbe, 0x1d, 0x70, 0xdd, 0x18, 0xe7, 0x4b, 0xc0, 0x99,
    0x67, 0xe4, 0xd6, 0x30, 0x9b, 0xa5, 0x0d, 0x5f, 0x1d, 0xdc, 0x86, 0x64, 0x12, 0x55, 0x31, 0xb8,
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
