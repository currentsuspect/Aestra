#include "EntitlementStore.h"

#include "LicenseGate.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Aestra {
namespace License {

namespace {
constexpr std::string_view kRumblePluginId = "com.Aestrastudios.rumble";

bool isVerifiedStatus(EntitlementStatus status) {
    return status == EntitlementStatus::Valid || status == EntitlementStatus::Grace;
}

bool containsToken(const std::vector<std::string>& values, std::string_view token) {
    return std::any_of(values.begin(), values.end(), [token](const std::string& value) { return value == token; });
}

bool hasExplicitFeature(const EntitlementProfile& profile, ProductFeature feature) {
    switch (feature) {
    case ProductFeature::Rumble:
        return containsToken(profile.rawFeatures, "rumble") || containsToken(profile.rawFeatures, "RUMBLE");
    case ProductFeature::RumbleHeadless:
        return containsToken(profile.rawFeatures, "rumble_headless") ||
               containsToken(profile.rawFeatures, "RUMBLE_HEADLESS");
    case ProductFeature::PremiumPluginBundle:
        return containsToken(profile.rawFeatures, "premium_plugin_bundle");
    case ProductFeature::FounderBadge:
        return containsToken(profile.rawFeatures, "founder_badge");
    case ProductFeature::SupporterBadge:
        return containsToken(profile.rawFeatures, "supporter_badge");
    case ProductFeature::CloudSync:
        return containsToken(profile.rawFeatures, "cloud_sync");
    case ProductFeature::EarlyAccess:
        return containsToken(profile.rawFeatures, "early_access");
    case ProductFeature::CoreDAW:
    default:
        return false;
    }
}

bool tierAllows(MembershipTier tier, ProductFeature feature) {
    switch (feature) {
    case ProductFeature::CoreDAW:
        return true;
    case ProductFeature::Rumble:
    case ProductFeature::PremiumPluginBundle:
    case ProductFeature::SupporterBadge:
        return tier == MembershipTier::Supporter || tier == MembershipTier::Founder;
    case ProductFeature::RumbleHeadless:
    case ProductFeature::FounderBadge:
        return tier == MembershipTier::Founder;
    case ProductFeature::CloudSync:
    case ProductFeature::EarlyAccess:
    default:
        return false;
    }
}
} // namespace

EntitlementStore::EntitlementStore() : m_profileProvider([] { return LicenseGate::currentProfile(); }) {}

EntitlementStore::EntitlementStore(ProfileProvider provider) : m_profileProvider(std::move(provider)) {}

EntitlementProfile EntitlementStore::currentProfile() const {
    if (!m_profileProvider) {
        return {};
    }
    EntitlementProfile profile = m_profileProvider();
    if (!isVerifiedStatus(profile.status)) {
        profile.tier = MembershipTier::Core;
        profile.verified = false;
    }
    return profile;
}

bool EntitlementStore::canAccess(ProductFeature feature) const {
    if (feature == ProductFeature::CoreDAW) {
        return true;
    }

    const EntitlementProfile profile = currentProfile();
    if (!profile.verified || !isVerifiedStatus(profile.status)) {
        return false;
    }

    if (hasExplicitFeature(profile, feature)) {
        return true;
    }
    return tierAllows(profile.tier, feature);
}

bool EntitlementStore::canUsePlugin(std::string_view pluginId) const {
    const EntitlementProfile profile = currentProfile();
    if (!profile.verified || !isVerifiedStatus(profile.status)) {
        return false;
    }

    if (containsToken(profile.rawPlugins, pluginId) || containsToken(profile.rawPlugins, "*")) {
        return true;
    }
    if (pluginId == kRumblePluginId) {
        return canAccess(ProductFeature::Rumble);
    }
    return false;
}

MembershipTier EntitlementStore::currentTier() const {
    return currentProfile().tier;
}

EntitlementStatus EntitlementStore::status() const {
    return currentProfile().status;
}

} // namespace License
} // namespace Aestra
