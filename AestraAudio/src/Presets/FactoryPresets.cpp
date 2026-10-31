// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Factory presets for Aestra's own plugins (FD-72: 8 to 10 per plugin). Each preset names its parameters the way the
// plugin's ParamSpec table does, so a table here reads like the plugin's knobs and survives an id being renumbered.
// A parameter left out keeps the plugin's default.

#include "Presets/Preset.h"

#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Presets {

namespace {

[[maybe_unused]] Preset make(const char* name, const char* category, const char* description,
                             std::initializer_list<std::pair<const char*, float>> values,
                             std::initializer_list<const char*> tags = {}) {
    Preset p;
    p.name = name;
    p.category = category;
    p.description = description;
    p.source = PresetSource::Factory;
    for (const auto& [param, value] : values) p.params.push_back({PresetParam::kNoId, param, value});
    for (const char* t : tags) p.tags.push_back(t);
    return p;
}

} // namespace

void addBuiltInFactoryPresets(std::map<std::string, std::vector<Preset>>& table) {
    // Sets are added plugin by plugin as they are voiced (V83-S2 slice 4). The table is the only place they live.
    (void)table;
}

} // namespace Presets
} // namespace Audio
} // namespace Aestra
