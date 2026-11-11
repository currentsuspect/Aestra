// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Presets/Preset.h"
#include "Presets/PresetLibrary.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Aestra {
namespace Audio {
namespace Presets {

/// The plugin id of Aestra Verb, whose editor kept its own user presets before FD-72.
inline constexpr const char* kVerbPluginId = "com.Aestrastudios.verb";

/// Where the Verb editor kept its user presets before FD-72 (one `.aeverb` JSON file each).
std::filesystem::path legacyVerbPresetDir();

/// Reads one `.aeverb` file's text as a Verb preset (params by name). `fallbackName` is used when the file has none.
std::optional<Preset> presetFromLegacyVerb(const std::string& text, const std::string& fallbackName);

/// Copies every `.aeverb` in `legacyDir` into the library as "Yours", once: a marker in the library root records
/// that it ran, so a preset the producer later deletes does not come back. The old files are left where they are.
/// Returns how many were imported.
int importLegacyVerbPresets(const PresetLibrary& library, const std::filesystem::path& legacyDir);

} // namespace Presets
} // namespace Audio
} // namespace Aestra
