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
    ok &= expect(!invalid.canAccess(ProductFeature::SupporterBadge), "invalid signature must not unlock paid features");
    ok &= expect(invalid.canAccess(ProductFeature::Rumble), "Rumble is free (FD-57), even with an invalid lease");

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

    // FD-57: Aestra's own plugins are free for everyone, signed in or not.
    for (const EntitlementStore* store : {&missing, &core, &supporter, &founder}) {
        ok &= expect(store->canAccess(ProductFeature::Rumble), "Rumble is free for every tier");
        ok &= expect(store->canAccess(ProductFeature::RumbleHeadless), "RumbleHeadless is free for every tier");
        ok &= expect(store->canUsePlugin("com.Aestrastudios.rumble"), "the Rumble plugin loads for every tier");
    }

    ok &= expect(!missing.canAccess(ProductFeature::SupporterBadge), "SupporterBadge needs a verified membership");
    ok &= expect(!core.canAccess(ProductFeature::SupporterBadge), "Core must not get the Supporter badge");
    ok &= expect(supporter.canAccess(ProductFeature::SupporterBadge), "Supporter gets the Supporter badge");
    ok &= expect(!supporter.canAccess(ProductFeature::FounderBadge), "Supporter must not get the Founder badge");
    ok &= expect(founder.canAccess(ProductFeature::FounderBadge), "Founder keeps the Founder badge");

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

    EntitlementStore unverified([] { return profile(MembershipTier::Supporter, EntitlementStatus::Missing, false); });
    ok &= expect(!unverified.canUsePlugin("com.Aestrastudios.experimental"),
                 "an unverified profile must not unlock listed plugins");

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
