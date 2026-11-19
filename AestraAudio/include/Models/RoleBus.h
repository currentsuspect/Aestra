// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file RoleBus.h
 * @brief When sounds that share a role should share a bus, and which of them would go into it.
 *
 * Connected Understanding (owner brief, 16 Nov 2026): the user loads a kick, a snare and a hi-hat, all Drums; Aestra
 * can see they belong together and offers a Drum Bus. This is the deterministic rule behind that offer: no listening,
 * no guessing, only the roles (MusicalRole.h) and the routing already in the project.
 *
 * What goes into the bus keeps its own channel and its own processing: a sound that has a channel of its own has that
 * channel routed into the bus; a sound still going straight to Master is routed to the bus itself. Nothing is merged.
 */

#include "Models/MusicalRole.h"
#include "Models/TrackManager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/// The bus's name for a category: "Drum Bus", "Vocal Bus", ...
inline std::string roleBusName(RoleCategory category) {
    switch (category) {
    case RoleCategory::Drums: return "Drum Bus";
    case RoleCategory::Percussion: return "Percussion Bus";
    case RoleCategory::Bass: return "Bass Bus";
    case RoleCategory::Vocals: return "Vocal Bus";
    case RoleCategory::Keys: return "Keys Bus";
    case RoleCategory::Guitar: return "Guitar Bus";
    case RoleCategory::Synth: return "Synth Bus";
    case RoleCategory::Strings: return "Strings Bus";
    case RoleCategory::Fx: return "FX Bus";
    case RoleCategory::Unknown: break;
    }
    return {};
}

struct RoleBusMember {
    enum class Kind { UnitOnMaster, Channel }; ///< a unit with no channel of its own, or a channel (a unit's or a track's)
    Kind kind{Kind::Channel};
    UnitID unit{0};          ///< UnitOnMaster: the unit to route into the bus
    uint32_t channelId{0};   ///< Channel: the channel whose output goes into the bus
    std::string name;        ///< for the toast and the menu
};

struct RoleBusPlan {
    RoleCategory category{RoleCategory::Unknown};
    std::string name;
    std::vector<RoleBusMember> members;
    bool worthOffering() const { return category != RoleCategory::Unknown && members.size() >= 2; }
};

namespace role_bus_detail {

/// True when this channel's output already goes somewhere other than Master: it is already in a group.
inline bool alreadyGrouped(const TrackManager& tm, uint32_t channelId) {
    const MixerChannel* channel = tm.getChannelById(channelId);
    if (!channel) return false;
    const uint32_t out = channel->getMainOutputId();
    return out != 0xFFFFFFFFu && out != MASTER_MIXER_CHANNEL_ID;
}

/// How many of @p category's sounds each channel already collects, Master included.
inline std::map<uint32_t, size_t> soundsPerChannel(const UnitManager& units, RoleCategory category) {
    std::map<uint32_t, size_t> counts;
    for (UnitID id : units.getAllUnitIDs()) {
        const UnitInfo* unit = units.getUnit(id);
        if (!unit || unit->role.category != category) continue;
        ++counts[unit->targetMixerChannelId];
    }
    return counts;
}

/// True when this channel already collects more than one of the category's sounds: that is a group already, whether it
/// was made by hand or by a role bus, so a bus made of it would nest one group inside another.
inline bool alreadyACollector(const std::map<uint32_t, size_t>& collected, uint32_t channelId) {
    const auto it = collected.find(channelId);
    return it != collected.end() && it->second > 1;
}

} // namespace role_bus_detail

/**
 * @brief The bus the project could use for @p category: the sounds and tracks with that role that are not already in
 * a group. Empty or under two members means there is nothing to offer.
 *
 * Units on the same channel count once (their channel goes into the bus, as one). A channel that already collects two
 * of the category's sounds is a group already and is left alone, which is also what keeps a Drum Bus out of a second
 * Drum Bus. A track still going straight to Master has no channel to route and is left out; a track that has one is
 * included.
 */
inline RoleBusPlan planRoleBus(TrackManager& tm, RoleCategory category) {
    RoleBusPlan plan;
    if (category == RoleCategory::Unknown) return plan;
    plan.category = category;
    plan.name = roleBusName(category);
    std::set<uint32_t> channelsTaken;

    const UnitManager& units = static_cast<const TrackManager&>(tm).getUnitManager();
    const std::map<uint32_t, size_t> collected = role_bus_detail::soundsPerChannel(units, category);
    for (UnitID id : units.getAllUnitIDs()) {
        const UnitInfo* unit = units.getUnit(id);
        if (!unit || unit->role.category != category) continue;
        const uint32_t channelId = unit->targetMixerChannelId;
        if (channelId == MASTER_MIXER_CHANNEL_ID || !tm.getChannelById(channelId)) {
            RoleBusMember m;
            m.kind = RoleBusMember::Kind::UnitOnMaster;
            m.unit = id;
            m.name = unit->name;
            plan.members.push_back(std::move(m));
            continue;
        }
        if (role_bus_detail::alreadyGrouped(tm, channelId) || role_bus_detail::alreadyACollector(collected, channelId) ||
            !channelsTaken.insert(channelId).second)
            continue;
        RoleBusMember m;
        m.channelId = channelId;
        m.name = unit->name;
        plan.members.push_back(std::move(m));
    }
    for (const Track* track : tm.getTracks()) {
        if (!track || track->role.category != category) continue;
        const uint32_t channelId = track->channelId;
        if (channelId == MASTER_MIXER_CHANNEL_ID || !tm.getChannelById(channelId)) continue;
        if (role_bus_detail::alreadyGrouped(tm, channelId) || role_bus_detail::alreadyACollector(collected, channelId) ||
            !channelsTaken.insert(channelId).second)
            continue;
        RoleBusMember m;
        m.channelId = channelId;
        m.name = track->name;
        plan.members.push_back(std::move(m));
    }
    return plan;
}

/// Whether @p unit would go into @p plan's bus (so the offer is made on that unit and not on one already grouped).
inline bool planIncludesUnit(const RoleBusPlan& plan, const TrackManager& tm, UnitID unit) {
    const uint32_t channel = tm.getUnitManager().getUnitMixerChannel(unit);
    for (const auto& m : plan.members) {
        if (m.kind == RoleBusMember::Kind::UnitOnMaster ? m.unit == unit : (channel != 0 && m.channelId == channel)) return true;
    }
    return false;
}

} // namespace Audio
} // namespace Aestra
