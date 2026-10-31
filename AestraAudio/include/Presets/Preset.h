// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Plugin/PluginHost.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace Presets {

/// Where a preset lives (FD-72). Factory presets ship with Aestra and are never overwritten; "Yours" presets are
/// files in the user's preset folder; project presets travel inside one project.
enum class PresetSource { Factory, User, Project };

const char* presetSourceName(PresetSource source);

/// One named value of a plugin's parameter, stored by name so a preset survives a parameter id being renumbered,
/// and by id so a plugin whose names repeat still finds the right one.
struct PresetParam {
    static constexpr uint32_t kNoId = 0xFFFFFFFFu; ///< matched by name only (factory tables)
    uint32_t id = kNoId;
    std::string name;
    float value = 0.0f;
};

/**
 * @brief One preset of one plugin: a name, where it lives, and the sound.
 *
 * The sound is kept two ways. `params` holds every writable, non-bypass parameter by name and value: that is what an
 * Aestra plugin is restored from, what "2 changes" is counted against, and what a person can read in the file.
 * `state` is the plugin's own saved state, kept for plugins whose sound is more than their parameters (a VST3 or
 * CLAP plugin, the sampler's sample). A preset applies its state first and falls back to its params.
 */
struct Preset {
    std::string name;
    std::string pluginId;   ///< PluginInfo::id of the plugin it belongs to
    std::string pluginName; ///< display name, so a file found on its own says what it is for
    std::string category;   ///< a folder in the browser ("Plates", "Essentials"); may be empty
    std::string description;
    std::vector<std::string> tags;
    PresetSource source = PresetSource::User;
    std::vector<PresetParam> params;
    std::vector<uint8_t> state;
    std::string path; ///< the file a User preset was read from or written to; empty otherwise
};

/// The file format version written by toJson. A reader accepts this version and older.
inline constexpr int kPresetFormatVersion = 1;

/// The file extension of a preset on disk.
inline constexpr const char* kPresetExtension = ".aepreset";

/// Serialises a preset to the `.aepreset` JSON text. `source` and `path` are not written: they describe where the
/// file is, which the file itself cannot know.
std::string toJson(const Preset& preset);

/// Reads `.aepreset` JSON text. Returns nothing for text that is not a preset, has no name or plugin, or is from a
/// newer format than this build understands.
std::optional<Preset> fromJson(const std::string& text);

/// Captures the plugin's current sound as a preset named `name`.
Preset capture(const IPluginInstance& plugin, const std::string& name);

/// Puts the preset's sound on the plugin. Returns false when the preset belongs to another plugin, or when neither
/// its state nor any of its params could be applied. Bypass is never touched: a preset changes the sound, not
/// whether the plugin is on.
bool apply(IPluginInstance& plugin, const Preset& preset);

/// How many of the preset's params the plugin no longer matches (the "2 changes" on the bar). A plugin whose preset
/// has no params (state only) is compared by state: 0 when equal, 1 when not.
int countChanges(const IPluginInstance& plugin, const Preset& preset);

/// The name Save offers (FD-72): built from the track's name and the preset the sound started from, so the producer
/// can press Enter. "Lead Vox" + "Vocal Plate" gives "Lead Vox Plate"; a name already in `taken` gets " 2", " 3"...
std::string suggestName(const std::string& trackName, const std::string& startedFrom,
                        const std::vector<std::string>& taken);

/// Turns a preset or plugin name into a file-system-safe stem (no separators, no reserved characters).
std::string safeFileStem(const std::string& name);

} // namespace Presets
} // namespace Audio
} // namespace Aestra
