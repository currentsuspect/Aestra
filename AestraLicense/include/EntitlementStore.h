#pragma once

#include "EntitlementProfile.h"

#include <functional>
#include <string_view>

namespace Aestra {
namespace License {

class EntitlementStore {
public:
    using ProfileProvider = std::function<EntitlementProfile()>;

    EntitlementStore();
    explicit EntitlementStore(ProfileProvider provider);

    EntitlementProfile currentProfile() const;
    bool canAccess(ProductFeature feature) const;
    bool canUsePlugin(std::string_view pluginId) const;
    MembershipTier currentTier() const;
    EntitlementStatus status() const;

private:
    ProfileProvider m_profileProvider;
};

} // namespace License
} // namespace Aestra
