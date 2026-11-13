// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Your 808s (SPEC-025, slice R7): the sounds the producer kept, as their own Rumble presets (FD-72), so every project
// and the shared preset bar see them. A kept sound is exact: the fifteen settings and the click's grain.
#pragma once

#include "RumbleVoice.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Plugins {

class RumbleKept {
public:
    struct Kept {
        std::string name;
        RumbleSound sound{};
        std::vector<uint8_t> state; // what RumbleInstance::loadState takes
        std::string path;           // the preset file
    };

    // @p root is the producer's preset folder (Presets::PresetLibrary::defaultUserRoot() in the app).
    explicit RumbleKept(std::filesystem::path root);

    // Rumble's kept sounds, by name.
    std::vector<Kept> list() const;
    // Keeps @p sound as @p name, or "name 2", "name 3" when that name is taken: a keep never overwrites.
    std::optional<Kept> keep(const RumbleSound& sound, const std::string& name) const;
    // Undoes a keep: removes the file.
    bool unkeep(const Kept& kept) const;

private:
    std::filesystem::path m_root;
};

} // namespace Plugins
} // namespace Aestra
