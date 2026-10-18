// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "VersionLanes.h"
#include "TakeManager.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief What the title-bar version chip and its picker show (SPEC-017, board 5): the open branch and version, and
 * each branch with the version you would land on. Pure: it reads a manifest and returns text, so it is tested
 * without a window.
 */
namespace VersionPicker {

struct Chip {
    std::string branch;   ///< as shown: "Main mix", "Alt hook"; empty when there is no open version
    std::string version;
    int lane = 0;
};

struct Entry {
    std::string branch;   ///< as shown
    int lane = 0;
    std::string versionId;    ///< the newest saved version on the branch, the one it opens
    std::string versionName;
    bool current = false;     ///< the open version is on this branch
};

inline std::string nameOf(const TakeManager::TakeEntry& v) { return v.name.empty() ? v.id : v.name; }

inline Chip chip(const TakeManager::Manifest& manifest) {
    Chip c;
    const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
    if (!open) return c;
    const auto lanes = VersionLanes::names(manifest.takes);
    const std::string branch = open->branch.empty() ? "Main" : open->branch;
    c.lane = static_cast<int>(std::find(lanes.begin(), lanes.end(), branch) - lanes.begin());
    c.branch = VersionLanes::label(branch);
    c.version = nameOf(*open);
    return c;
}

/// One entry per branch, in lane order. A branch opens on its newest saved version (autosaves are never a landing place).
inline std::vector<Entry> entries(const TakeManager::Manifest& manifest) {
    std::vector<Entry> out;
    if (!manifest.ok) return out;
    const auto lanes = VersionLanes::names(manifest.takes);
    const auto* open = manifest.activeTake();
    const std::string openBranch = open ? (open->branch.empty() ? "Main" : open->branch) : "";
    for (size_t i = 0; i < lanes.size(); ++i) {
        const TakeManager::TakeEntry* newest = nullptr;
        for (const auto& v : manifest.takes) {
            if ((v.branch.empty() ? "Main" : v.branch) != lanes[i] || v.kind == TakeManager::TakeEntry::Kind::Autosave) continue;
            if (!newest || v.createdAtMs >= newest->createdAtMs) newest = &v;
        }
        if (!newest) continue;
        out.push_back({VersionLanes::label(lanes[i]), static_cast<int>(i), newest->id, nameOf(*newest), lanes[i] == openBranch});
    }
    return out;
}

} // namespace VersionPicker
} // namespace Aestra
