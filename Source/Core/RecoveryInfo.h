// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"
#include "VersionLanes.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief What the crash-recovery card says (SPEC-017, board 4): the last version that was saved, the autosaves made
 * after it, and plain words about how far apart they are. Pure: a manifest and some times in, text out.
 */
namespace RecoveryInfo {

struct Info {
    bool hasVersion = false;        ///< the project has a saved version to compare against
    std::string lastSavedName;
    uint64_t lastSavedMs = 0;
    std::string branch;             ///< the open branch as shown ("Main mix")
    int lane = 0;
    std::vector<uint64_t> autosaveMs;  ///< the autosaves after the last saved version, oldest first, at most kMaxDots
    std::string detail;             ///< one line under the headline
};

constexpr size_t kMaxDots = 3;

/// "2 minutes", "1 minute", "3 hours", "moments": a gap between two times in plain words.
inline std::string gapWords(uint64_t fromMs, uint64_t toMs) {
    const uint64_t ms = toMs > fromMs ? toMs - fromMs : 0;
    const uint64_t minutes = (ms + 30000) / 60000;
    if (minutes == 0) return "moments";
    if (minutes < 120) return std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    const uint64_t hours = (minutes + 30) / 60;
    return std::to_string(hours) + (hours == 1 ? " hour" : " hours");
}

inline Info build(const TakeManager::Manifest& manifest, std::vector<uint64_t> autosaveTimesMs) {
    Info info;
    std::sort(autosaveTimesMs.begin(), autosaveTimesMs.end());
    const auto* open = manifest.ok ? manifest.activeTake() : nullptr;
    if (open) {
        info.hasVersion = true;
        info.lastSavedName = open->name.empty() ? open->id : open->name;
        info.lastSavedMs = open->updatedAtMs ? open->updatedAtMs : open->createdAtMs;
        const auto lanes = VersionLanes::names(manifest.takes);
        const std::string branch = open->branch.empty() ? "Main" : open->branch;
        info.lane = static_cast<int>(std::find(lanes.begin(), lanes.end(), branch) - lanes.begin());
        info.branch = VersionLanes::label(branch);
    }
    for (const uint64_t t : autosaveTimesMs)
        if (!info.hasVersion || t > info.lastSavedMs) info.autosaveMs.push_back(t);
    if (info.autosaveMs.size() > kMaxDots) info.autosaveMs.erase(info.autosaveMs.begin(), info.autosaveMs.end() - kMaxDots);

    const uint64_t newest = autosaveTimesMs.empty() ? 0 : autosaveTimesMs.back();
    if (!info.hasVersion) info.detail = "No version of this project has been saved yet.";
    else if (newest > info.lastSavedMs) info.detail = "The newest autosave is " + gapWords(info.lastSavedMs, newest) + " after your last saved version.";
    else info.detail = "Nothing newer than your last saved version was autosaved.";
    return info;
}

} // namespace RecoveryInfo
} // namespace Aestra
