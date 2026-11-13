// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleKept.h"

#include "Presets/PresetLibrary.h"
#include "RumbleInstance.h"
#include "RumblePresetBank.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

namespace Aestra {
namespace Plugins {

namespace {
namespace Presets = Aestra::Audio::Presets;
constexpr const char* kRumbleId = "com.Aestrastudios.rumble";

// Which of Rumble's settings a preset entry sets: by id and name, then by name, then by id, as
// Presets::apply matches them, so the shelf and the preset bar load the same sound.
int settingOf(const Presets::PresetParam& param) {
    const auto named = [&](uint32_t id) { return param.name == RumbleInstance::parameterName(id); };
    if (param.id < RumbleSound::Count && named(param.id)) return static_cast<int>(param.id);
    if (!param.name.empty()) {
        for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
            if (named(id)) return static_cast<int>(id);
        }
    }
    if (param.id != Presets::PresetParam::kNoId && param.id < RumbleSound::Count) return static_cast<int>(param.id);
    return -1;
}

RumbleKept::Kept keptOf(const Presets::Preset& preset) {
    RumbleKept::Kept kept;
    kept.name = preset.name;
    kept.path = preset.path;
    // As the preset bar loads it: Rumble's state first (it carries the grain), then each setting over it.
    const auto fromState = RumbleInstance::soundOfState(preset.state);
    kept.sound = fromState ? *fromState : rumbleDefaultSound();
    bool overridden = false;
    for (const auto& param : preset.params) {
        const int id = settingOf(param);
        if (id < 0 || !std::isfinite(param.value)) continue;
        double& setting = kept.sound[static_cast<size_t>(id)];
        // Keep writes each setting as a float beside the exact state: the same value is not an override.
        if (fromState && static_cast<float>(setting) == param.value) continue;
        setting = std::clamp(static_cast<double>(param.value), 0.0, 1.0);
        overridden = true;
    }
    kept.state = fromState && !overridden ? preset.state : RumbleInstance::stateOfSound(kept.sound);
    return kept;
}

// The library treats names that differ only in case as the same preset.
std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
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
    for (const auto& preset : library.userPresets(kRumbleId)) taken.insert(lowered(preset.name));
    std::string unique = name;
    for (int n = 2; taken.count(lowered(unique)) != 0; ++n) unique = name + " " + std::to_string(n);

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
