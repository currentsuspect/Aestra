#include "EntitlementStore.h"

#include <chrono>
#include <iostream>
#include <string>

using namespace Aestra::License;

namespace {
bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "failed: " << message << "\n";
        return false;
    }
    return true;
}

EntitlementProfile profile(MembershipTier tier, EntitlementStatus status, bool verified) {
    EntitlementProfile p;
    p.tier = tier;
    p.status = status;
    p.verified = verified;
    p.offline = true;
    p.userId = "user-test";
    p.licenseId = "license-test";
    p.issuedAt = std::chrono::system_clock::now() - std::chrono::hours(1);
    p.expiresAt = std::chrono::system_clock::now() + std::chrono::hours(1);
    return p;
}

bool testTierAndStatusFallbacks() {
    bool ok = true;

    EntitlementStore missing([] { return profile(MembershipTier::Founder, EntitlementStatus::Missing, false); });
    ok &= expect(missing.currentTier() == MembershipTier::Core, "missing license must fall back to Core");
    ok &= expect(missing.status() == EntitlementStatus::Missing, "missing status should be preserved");
    ok &= expect(missing.canAccess(ProductFeature::CoreDAW), "CoreDAW must remain accessible when missing");

    EntitlementStore invalid(
        [] { return profile(MembershipTier::Founder, EntitlementStatus::InvalidSignature, false); });
    ok &= expect(invalid.currentTier() == MembershipTier::Core, "invalid signature must fall back to Core");
    ok &= expect(!invalid.canAccess(ProductFeature::Rumble), "invalid signature must not unlock Rumble");

    EntitlementStore malformed([] { return profile(MembershipTier::Supporter, EntitlementStatus::ParseError, false); });
    ok &= expect(malformed.currentTier() == MembershipTier::Core, "parse errors must fall back to Core");

    EntitlementStore core([] { return profile(MembershipTier::Core, EntitlementStatus::Valid, true); });
    ok &= expect(core.currentTier() == MembershipTier::Core, "valid Core should remain Core");

    EntitlementStore supporter([] { return profile(MembershipTier::Supporter, EntitlementStatus::Valid, true); });
    ok &= expect(supporter.currentTier() == MembershipTier::Supporter, "valid Supporter should remain Supporter");

    EntitlementStore founder([] { return profile(MembershipTier::Founder, EntitlementStatus::Valid, true); });
    ok &= expect(founder.currentTier() == MembershipTier::Founder, "valid Founder should remain Founder");

    return ok;
}

bool testFeatureMatrix() {
    bool ok = true;

    EntitlementStore missing([] { return profile(MembershipTier::Core, EntitlementStatus::Missing, false); });
    EntitlementStore core([] { return profile(MembershipTier::Core, EntitlementStatus::Valid, true); });
    EntitlementStore supporter([] { return profile(MembershipTier::Supporter, EntitlementStatus::Valid, true); });
    EntitlementStore founder([] { return profile(MembershipTier::Founder, EntitlementStatus::Valid, true); });

    ok &= expect(missing.canAccess(ProductFeature::CoreDAW), "CoreDAW allowed for missing state");
    ok &= expect(core.canAccess(ProductFeature::CoreDAW), "CoreDAW allowed for Core");
    ok &= expect(supporter.canAccess(ProductFeature::CoreDAW), "CoreDAW allowed for Supporter");
    ok &= expect(founder.canAccess(ProductFeature::CoreDAW), "CoreDAW allowed for Founder");

    ok &= expect(!core.canAccess(ProductFeature::Rumble), "Core must not access Rumble");
    ok &= expect(supporter.canAccess(ProductFeature::Rumble), "Supporter should access Rumble");
    ok &= expect(founder.canAccess(ProductFeature::Rumble), "Founder should access Rumble");

    ok &= expect(!core.canAccess(ProductFeature::RumbleHeadless), "Core must not access RumbleHeadless");
    ok &= expect(!supporter.canAccess(ProductFeature::RumbleHeadless), "Supporter must not access RumbleHeadless");
    ok &= expect(founder.canAccess(ProductFeature::RumbleHeadless), "Founder should access RumbleHeadless");

    ok &= expect(!supporter.canAccess(ProductFeature::CloudSync), "planned CloudSync should be denied by tier alone");
    ok &= expect(!founder.canAccess(ProductFeature::EarlyAccess), "planned EarlyAccess should be denied by tier alone");

    return ok;
}

bool testExplicitFeatureAndPluginEntitlements() {
    bool ok = true;

    EntitlementStore explicitFeature([] {
        EntitlementProfile p = profile(MembershipTier::Core, EntitlementStatus::Valid, true);
        p.rawFeatures = {"cloud_sync", "early_access", "rumble"};
        p.rawPlugins = {"com.Aestrastudios.experimental"};
        return p;
    });

    ok &= expect(explicitFeature.canAccess(ProductFeature::CloudSync),
                 "explicit CloudSync entitlement should allow CloudSync");
    ok &= expect(explicitFeature.canAccess(ProductFeature::EarlyAccess),
                 "explicit EarlyAccess entitlement should allow EarlyAccess");
    ok &= expect(explicitFeature.canAccess(ProductFeature::Rumble), "explicit Rumble feature should allow Rumble");
    ok &= expect(explicitFeature.canUsePlugin("com.Aestrastudios.experimental"),
                 "explicit plugin entitlement should allow matching plugin ID");
    ok &= expect(!explicitFeature.canUsePlugin("com.Aestrastudios.unknown"), "unknown plugin must be denied");

    EntitlementStore tierFallback([] { return profile(MembershipTier::Supporter, EntitlementStatus::Valid, true); });
    ok &= expect(tierFallback.canUsePlugin("com.Aestrastudios.rumble"),
                 "tier fallback should preserve current Rumble plugin behavior");

    return ok;
}
} // namespace

int main() {
    bool ok = true;
    ok &= testTierAndStatusFallbacks();
    ok &= testFeatureMatrix();
    ok &= testExplicitFeatureAndPluginEntitlements();
    if (!ok) {
        return 1;
    }
    std::cout << "EntitlementStore tests passed.\n";
    return 0;
}
