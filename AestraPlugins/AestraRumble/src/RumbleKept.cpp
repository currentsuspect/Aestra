// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleKept.h"

#include "Presets/PresetLibrary.h"
#include "RumbleInstance.h"
#include "RumblePresetBank.h"

#include <set>
#include <utility>

namespace Aestra {
namespace Plugins {

namespace {
namespace Presets = Aestra::Audio::Presets;
constexpr const char* kRumbleId = "com.Aestrastudios.rumble";

RumbleKept::Kept keptOf(const Presets::Preset& preset) {
    RumbleKept::Kept kept;
    kept.name = preset.name;
    kept.path = preset.path;
    kept.state = preset.state;
    if (auto sound = RumbleInstance::soundOfState(preset.state)) {
        kept.sound = *sound;
    } else {
        // A preset without Rumble's state (hand-made, or from an older bar): read its settings by name.
        kept.sound = rumbleDefaultSound();
        for (const auto& param : preset.params) {
            for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
                if (param.name == RumbleInstance::parameterName(id)) kept.sound[id] = param.value;
            }
        }
        kept.state = RumbleInstance::stateOfSound(kept.sound);
    }
    return kept;
}
} // namespace

RumbleKept::RumbleKept(std::filesystem::path root) : m_root(std::move(root)) {}

std::vector<RumbleKept::Kept> RumbleKept::list() const {
    std::vector<Kept> out;
    for (const auto& preset : Presets::PresetLibrary(m_root).userPresets(kRumbleId)) out.push_back(keptOf(preset));
    return out;
}

std::optional<RumbleKept::Kept> RumbleKept::keep(const RumbleSound& sound, const std::string& name) const {
    Presets::PresetLibrary library(m_root);
    std::set<std::string> taken;
    for (const auto& preset : library.userPresets(kRumbleId)) taken.insert(preset.name);
    std::string unique = name;
    for (int n = 2; taken.count(unique) != 0; ++n) unique = name + " " + std::to_string(n);

    Presets::Preset preset;
    preset.name = unique;
    preset.pluginId = kRumbleId;
    preset.pluginName = "Aestra Rumble";
    preset.category = "Your 808s";
    preset.tags = {"808", "bass", "sub"};
    preset.source = Presets::PresetSource::User;
    for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
        preset.params.push_back({id, RumbleInstance::parameterName(id), static_cast<float>(sound[id])});
    }
    preset.state = RumbleInstance::stateOfSound(sound); // the grain is not a parameter
    std::string error;
    const auto saved = library.saveUser(std::move(preset), false, &error);
    if (!saved) return std::nullopt;
    return keptOf(*saved);
}

bool RumbleKept::unkeep(const Kept& kept) const {
    Presets::Preset preset;
    preset.name = kept.name;
    preset.pluginId = kRumbleId;
    preset.source = Presets::PresetSource::User;
    preset.path = kept.path;
    return Presets::PresetLibrary(m_root).removeUser(preset);
}

} // namespace Plugins
} // namespace Aestra
