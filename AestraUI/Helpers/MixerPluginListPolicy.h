// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// MixerPluginListPolicy — widget-independent grouping policy for the mixer's
// plugin dropdown. Header-only and dependency-free (std only) so the "which
// plugins belong on a mixer channel" contract is testable headless, in the
// TimelineInteractionPolicy.h / TimelineMarquee.h mould.
//
// The rule the policy owns: mixer inserts are audio EFFECTS. Instruments,
// MIDI effects and analyzers attach elsewhere (Arsenal units, the piano roll,
// metering surfaces) and are excluded here — from metadata, never from a
// hand-maintained allowlist, so newly registered internal plugins and scanned
// VST3/CLAP effects appear without any UI change.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace Aestra {
namespace Components {

/// Minimal plugin description the policy needs. The widget/app layers map
/// their own types (PluginInfo, PluginListItem) onto this.
struct MixerPluginEntry {
    std::string id;
    std::string name;
    std::string category; // plugin's own category string ("Dynamics", "Reverb", ...)
    std::string typeName; // "Effect", "Instrument", "MidiEffect", "Analyzer"
};

struct MixerPluginGroup {
    std::string label; // section header shown in the dropdown
    std::string icon;  // icon name from the dropdown's icon set
    std::vector<MixerPluginEntry> entries;
};

/// True when the plugin belongs on a mixer insert chain.
inline bool isMixerInsertPlugin(const MixerPluginEntry& entry) {
    return entry.typeName == "Effect" && !entry.id.empty();
}

/// Category label -> (group label, icon). Unknown categories land in "OTHER".
/// Kept in one table so the dropdown's sections stay consistent.
inline void mixerGroupForCategory(const std::string& category, std::string& groupLabel, std::string& icon) {
    if (category == "Dynamics" || category == "Compressor" || category == "Distortion") {
        groupLabel = "DYNAMICS";
        icon = "chart-bar";
    } else if (category == "Equalizer" || category == "Filter") {
        groupLabel = "SPECTRAL";
        icon = "wave-sine";
    } else if (category == "Reverb") {
        groupLabel = "SPECTRAL";
        icon = "circles";
    } else if (category == "Delay") {
        groupLabel = "TIME";
        icon = "clock";
    } else {
        groupLabel = "OTHER";
        icon = "circles";
    }
}

/// Group mixer-compatible plugins into dropdown sections.
///
/// Deterministic: entries are sorted alphabetically by display name inside
/// each group (scan order is not), groups keep a stable fixed order, and
/// duplicate plugin IDs are dropped (first wins).
inline std::vector<MixerPluginGroup> groupForMixerDropdown(std::vector<MixerPluginEntry> entries) {
    std::vector<MixerPluginGroup> groups = {
        {"DYNAMICS", "chart-bar", {}},
        {"TIME", "clock", {}},
        {"SPECTRAL", "wave-sine", {}},
        {"OTHER", "circles", {}},
    };

    std::unordered_set<std::string> seen;
    for (auto& entry : entries) {
        if (!isMixerInsertPlugin(entry)) {
            continue;
        }
        if (!seen.insert(entry.id).second) {
            continue;
        }
        std::string label;
        std::string icon;
        mixerGroupForCategory(entry.category, label, icon);
        for (auto& group : groups) {
            if (group.label == label) {
                group.entries.push_back(entry);
                break;
            }
        }
    }

    for (auto& group : groups) {
        std::sort(group.entries.begin(), group.entries.end(), [](const MixerPluginEntry& a, const MixerPluginEntry& b) {
            if (a.name != b.name) {
                return a.name < b.name;
            }
            return a.id < b.id;
        });
    }

    std::vector<MixerPluginGroup> nonEmpty;
    nonEmpty.reserve(groups.size());
    for (auto& group : groups) {
        if (!group.entries.empty()) {
            nonEmpty.push_back(std::move(group));
        }
    }
    return nonEmpty;
}

} // namespace Components
} // namespace Aestra
