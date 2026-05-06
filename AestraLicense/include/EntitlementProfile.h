#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace Aestra {
namespace License {

enum class MembershipTier {
    Core = 0,
    Supporter,
    Founder,
};

enum class EntitlementStatus {
    Missing = 0,
    Valid,
    Expired,
    Grace,
    InvalidSignature,
    WrongDevice,
    Revoked,
    ParseError,
    Unknown,
};

enum class ProductFeature {
    CoreDAW = 0,
    Rumble,
    RumbleHeadless,
    PremiumPluginBundle,
    FounderBadge,
    SupporterBadge,
    CloudSync,
    EarlyAccess,
};

struct EntitlementProfile {
    MembershipTier tier = MembershipTier::Core;
    EntitlementStatus status = EntitlementStatus::Missing;
    std::string userId;
    std::string licenseId;
    std::vector<std::string> rawPlugins;
    std::vector<std::string> rawFeatures;
    std::chrono::system_clock::time_point issuedAt{};
    std::chrono::system_clock::time_point expiresAt{};
    bool offline = true;
    bool verified = false;
};

} // namespace License
} // namespace Aestra
