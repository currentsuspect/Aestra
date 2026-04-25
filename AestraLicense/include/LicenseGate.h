#pragma once

#include "LicenseTier.h"

#include <cstdint>

namespace Aestra {
namespace License {

inline constexpr unsigned char AESTRA_LICENSE_PUBKEY[32] = {
    // Development/test verification public key for public repository builds.
    // This is NOT a production private signing key and can be committed safely.
    // Replace in private/release pipelines with the intended production verification key.
    // Key label: AESTRA_DEV_TEST_PUBKEY_V1
    0xbc, 0x31, 0xf4, 0xae, 0x03, 0x60, 0xf0, 0xcd,
    0x62, 0xaf, 0x1e, 0x23, 0x32, 0xa8, 0xa2, 0x41,
    0xa9, 0x40, 0x6b, 0x20, 0x29, 0x5a, 0xb3, 0xe6,
    0x00, 0x30, 0x9b, 0xa3, 0x8f, 0x28, 0x86, 0x4f,
};

class LicenseGate {
public:
    static void initialize();
    static LicenseTier currentTier();
    static bool canAccess(Feature feature);
    static void refreshAsync();
    static int64_t secondsUntilExpiry();
};

} // namespace License
} // namespace Aestra
