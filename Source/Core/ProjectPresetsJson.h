// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Two project-file sections kept out of ProjectSerializer.cpp, which is size-pinned:
//   - "presets": the presets kept in this project only (FD-72), each in the same shape as an .aepreset file;
//   - the UI panel list, moved here to make room.

#include "AestraJSON.h"
#include "Presets/Preset.h"

#include <cstddef>
#include <vector>

namespace Aestra {

/// A project holds at most this many presets of its own; more in a file is a damaged or hostile file.
inline constexpr size_t kMaxProjectPresets = 512;

inline JSON projectPresetsToJson(const std::vector<Audio::Presets::Preset>& presets) {
    JSON list = JSON::array();
    for (const auto& p : presets) list.push(JSON::parse(Audio::Presets::toJson(p)));
    return list;
}

/// Reads the "presets" section. A preset that does not read as one is skipped, not fatal: the rest of the project
/// and the other presets still load.
inline std::vector<Audio::Presets::Preset> projectPresetsFromJson(const JSON& list) {
    std::vector<Audio::Presets::Preset> out;
    if (!list.isArray()) return out;
    for (const auto& item : list.asArray()) {
        if (out.size() >= kMaxProjectPresets) break;
        if (!item.isObject()) continue;
        auto preset = Audio::Presets::fromJson(item.toString());
        if (!preset) continue;
        preset->source = Audio::Presets::PresetSource::Project;
        out.push_back(std::move(*preset));
    }
    return out;
}

/// The UI panel list as saved in the project's "ui" section.
template <typename PanelStateList> JSON uiPanelsToJson(const PanelStateList& panels) {
    JSON list = JSON::array();
    for (const auto& p : panels) {
        JSON pj = JSON::object();
        pj.set("title", JSON(p.title));
        pj.set("x", JSON(p.x));
        pj.set("y", JSON(p.y));
        pj.set("width", JSON(p.width));
        pj.set("height", JSON(p.height));
        pj.set("expandedHeight", JSON(p.expandedHeight));
        pj.set("minimized", JSON(p.minimized));
        pj.set("maximized", JSON(p.maximized));
        pj.set("userPositioned", JSON(p.userPositioned));
        list.push(pj);
    }
    return list;
}

} // namespace Aestra
