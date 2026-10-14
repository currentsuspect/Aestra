// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {

/// How branches are ordered, named and coloured wherever versions are shown (the timeline, the title-bar chip).
namespace VersionLanes {

/// Branch names in lane order: the order they first appear in the manifest, the first being the main mix.
inline std::vector<std::string> names(const std::vector<TakeManager::TakeEntry>& versions) {
    std::vector<std::string> lanes;
    for (const auto& v : versions) {
        const std::string lane = v.branch.empty() ? "Main" : v.branch;
        if (std::find(lanes.begin(), lanes.end(), lane) == lanes.end()) lanes.push_back(lane);
    }
    return lanes;
}

/// How a branch reads on screen: the first lane is stored as "Main" and shows as the main mix.
inline std::string label(const std::string& branch) { return branch.empty() || branch == "Main" ? "Main mix" : branch; }

/// A lane's colour as 0xAARRGGBB: violet for the main mix, then cyan, amber, pink, lime, blue, repeating.
inline uint32_t hex(int lane) {
    static const uint32_t palette[] = {0xff7c3aed, 0xff67e8f9, 0xfff5a83a, 0xfff472b6, 0xff8bc34a, 0xff60a5fa};
    return palette[static_cast<size_t>(std::max(0, lane)) % (sizeof(palette) / sizeof(palette[0]))];
}

} // namespace VersionLanes
} // namespace Aestra
