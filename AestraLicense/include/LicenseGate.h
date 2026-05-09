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
    // Key label: AESTRA_DEV_TEST_PUBKEY_V3
    // Hex: bf30bfb9e66ff349bb96922b26e92fb860272adc2413f15b4052bc8b56800f58
    0xbf, 0x30, 0xbf, 0xb9, 0xe6, 0x6f, 0xf3, 0x49, 0xbb, 0x96, 0x92, 0x2b, 0x26, 0xe9, 0x2f, 0xb8,
    0x60, 0x27, 0x2a, 0xdc, 0x24, 0x13, 0xf1, 0x5b, 0x40, 0x52, 0xbc, 0x8b, 0x56, 0x80, 0x0f, 0x58,
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
