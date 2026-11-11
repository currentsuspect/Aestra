// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Presets/Preset.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Presets {

/// Factory presets for a plugin id, in the order the browser shows them. Empty for a plugin that has none.
std::vector<Preset> factoryPresets(const std::string& pluginId);

/// Adds factory presets for a plugin that lives outside AestraAudio (Rumble). Replaces any earlier set for that id.
void registerFactoryPresets(const std::string& pluginId, std::vector<Preset> presets);

/**
 * @brief The producer's own presets ("Yours, every project"), one `.aepreset` file each, under one folder.
 *
 * Files are found by what they say, not where they are: any `.aepreset` under the root whose plugin id matches is
 * listed, so a preset a friend sent can be dropped anywhere in the folder. New presets are written to
 * `<root>/<plugin name>/<preset name>.aepreset`.
 */
class PresetLibrary {
public:
    explicit PresetLibrary(std::filesystem::path userRoot);

    /// `<Documents>/Aestra/Presets`: beside the projects folder, where a producer looks for their own files.
    static std::filesystem::path defaultUserRoot();

    const std::filesystem::path& userRoot() const { return m_root; }

    /// The producer's presets for a plugin, by name.
    std::vector<Preset> userPresets(const std::string& pluginId) const;

    /// Factory first, then the producer's, each by name.
    std::vector<Preset> list(const std::string& pluginId) const;

    /// Writes a preset to the producer's folder. A preset with the same name for the same plugin is replaced only
    /// when `replace` is set (Save's "Update"); otherwise nothing is written and `error` says why.
    /// Returns the preset as written, with `source` User and its `path` filled in.
    std::optional<Preset> saveUser(Preset preset, bool replace, std::string* error = nullptr) const;

    /// Deletes one of the producer's presets. Factory and project presets are not files here and are refused.
    bool removeUser(const Preset& preset) const;

private:
    std::filesystem::path pathFor(const Preset& preset) const;

    std::filesystem::path m_root;
};

/// Orders presets so the ones that fit the track come first (FD-72: "nothing is hidden, only ordered"): a preset
/// whose name, category or tags share a word with the track's name ranks above one that does not. The order is
/// otherwise kept, so factory-then-yours survives.
std::vector<Preset> orderForTrack(std::vector<Preset> presets, const std::string& trackName);

} // namespace Presets
} // namespace Audio
} // namespace Aestra
