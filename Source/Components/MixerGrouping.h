// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aestra {

/**
 * @brief The order the mixer shows its strips in, grouped by where they go (V81-M1, FD-28 point 1).
 *
 * Pure: it reads a list of channels (master excluded) and returns a display order plus the groups, so the
 * rule is testable without a window. Left to right follows the signal: the channels that feed a bus come
 * first, the bus closes its group, and the channels that go straight to the master come last.
 *
 *   Kick  Snare  Hats  [Drum Bus]   Bass  Lead
 *   └──── → Drum Bus ──────────┘    └ → Master ┘
 *
 * The bus itself sits in the group it closes (its own output is shown on its chip). The order inside a
 * group is the project's channel order; groups are ordered by their first member, the master group last.
 * A route to a channel that does not exist, or one that loops back, counts as going to the master.
 */
struct MixerGroupInput {
    uint32_t id{0};
    uint32_t mainOutputId{0}; ///< 0 = master
    std::string name;
    int colorIndex{-1};
};

struct MixerStripGroup {
    uint32_t targetId{0}; ///< the bus this group feeds; 0 = master
    std::string label;    ///< "Drum Bus", "Master"
    int colorIndex{-1};   ///< the bus's track colour, -1 for the master
    size_t first{0};      ///< first display slot
    size_t count{0};
};

struct MixerGroupedOrder {
    std::vector<size_t> order;           ///< display slot -> index into the input list
    std::vector<MixerStripGroup> groups; ///< in display order
    /// True when there is more than one group; a flat project shows no headers and no gaps.
    bool grouped() const { return groups.size() > 1; }
};

inline MixerGroupedOrder groupMixerStripsByOutput(const std::vector<MixerGroupInput>& channels) {
    std::unordered_map<uint32_t, size_t> indexOf;
    for (size_t i = 0; i < channels.size(); ++i)
        indexOf[channels[i].id] = i;

    // The group a channel belongs to: the bus it feeds, or the master (0).
    const auto targetOf = [&](size_t i) -> uint32_t {
        const uint32_t target = channels[i].mainOutputId;
        if (target == 0 || target == channels[i].id || !indexOf.count(target))
            return 0;
        // A loop (A -> B -> A) is refused by the router, but a damaged project may carry one.
        std::unordered_set<uint32_t> seen{channels[i].id};
        uint32_t cursor = target;
        while (cursor != 0) {
            if (!seen.insert(cursor).second)
                return 0;
            const auto it = indexOf.find(cursor);
            if (it == indexOf.end())
                break;
            cursor = channels[it->second].mainOutputId;
        }
        return target;
    };

    std::unordered_map<uint32_t, std::vector<size_t>> members; // group target -> channel indices
    std::vector<uint32_t> targetsByFirstMember;
    for (size_t i = 0; i < channels.size(); ++i) {
        const uint32_t target = targetOf(i);
        auto& list = members[target];
        if (list.empty())
            targetsByFirstMember.push_back(target);
        list.push_back(i);
    }
    // A bus closes the group it feeds: pull it out of its own output's group and append it to its feeders'.
    for (auto& [target, list] : members) {
        (void)list;
        if (target == 0)
            continue;
        const size_t busIndex = indexOf[target];
        const uint32_t busTarget = targetOf(busIndex);
        auto& home = members[busTarget];
        home.erase(std::remove(home.begin(), home.end(), busIndex), home.end());
    }
    for (auto& [target, list] : members) {
        if (target == 0)
            continue;
        list.push_back(indexOf[target]);
    }

    // Groups ordered by their first member's position in the project; the master group last.
    std::vector<uint32_t> targets;
    for (const auto& [target, list] : members) {
        if (!list.empty() && target != 0)
            targets.push_back(target);
    }
    std::sort(targets.begin(), targets.end(), [&](uint32_t a, uint32_t b) {
        return *std::min_element(members[a].begin(), members[a].end()) <
               *std::min_element(members[b].begin(), members[b].end());
    });
    if (members.count(0) && !members[0].empty())
        targets.push_back(0);

    MixerGroupedOrder result;
    for (const uint32_t target : targets) {
        MixerStripGroup group;
        group.targetId = target;
        group.first = result.order.size();
        group.count = members[target].size();
        if (target == 0) {
            group.label = "Master";
        } else {
            const auto& bus = channels[indexOf[target]];
            group.label = bus.name;
            group.colorIndex = bus.colorIndex;
        }
        for (const size_t index : members[target])
            result.order.push_back(index);
        result.groups.push_back(std::move(group));
    }
    return result;
}

} // namespace Aestra
