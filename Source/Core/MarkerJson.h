// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The project's "markers" section: named timeline positions (song sections and cues). Kept out of
// ProjectSerializer.cpp, which is size-pinned.

#include "AestraJSON.h"
#include "Models/MarkerModel.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace Aestra {

inline constexpr double kMaxMarkerBeat = 1.0e9; // far past any real song; a larger value is a damaged file

inline JSON markersToJson(const Audio::MarkerModel& markers) {
    JSON list = JSON::array();
    for (const auto& marker : markers.markers()) {
        JSON item = JSON::object();
        item.set("id", JSON(static_cast<double>(marker.id)));
        item.set("beat", JSON(marker.beat));
        item.set("name", JSON(marker.name));
        list.push(item);
    }
    return list;
}

/// Reads the "markers" section. A marker that is malformed is skipped, not fatal: the rest of the timeline, and the
/// other markers, still load. Duplicate ids are skipped by the model rather than overwriting a live marker.
inline void markersFromJson(const JSON& list, Audio::MarkerModel& markers) {
    markers.clear();
    if (!list.isArray()) return;
    for (const auto& item : list.asArray()) {
        if (markers.count() >= Audio::MarkerModel::kMaxMarkers) break;
        if (!item.isObject()) continue;
        if (!item.has("beat") || !item["beat"].isNumber()) continue;
        const double beat = item["beat"].asNumber();
        if (!std::isfinite(beat) || beat < 0.0 || beat > kMaxMarkerBeat) continue;

        uint64_t id = 0;
        if (item.has("id") && item["id"].isNumber()) {
            const double rawId = item["id"].asNumber();
            if (std::isfinite(rawId) && rawId >= 1.0 && rawId <= 9007199254740992.0) {
                id = static_cast<uint64_t>(rawId);
            }
        }

        std::string name;
        if (item.has("name") && item["name"].isString()) {
            name = item["name"].asString();
        }
        markers.add(beat, name, id);
    }
}

/// Project save: the "markers" section is written only when the project has some, so a project that never used
/// markers produces the same file it always did.
inline void saveMarkers(JSON& root, const Audio::MarkerModel& markers) {
    if (!markers.empty()) root.set("markers", markersToJson(markers));
}

/// Project load: a file with no "markers" section loads with none. The previous project's markers must never
/// survive a load. Markers are UI-thread only, like the track table: the audio graph never reads them.
inline void loadMarkers(const JSON& root, Audio::MarkerModel& markers) {
    markers.clear();
    if (root.has("markers")) markersFromJson(root["markers"], markers);
}

} // namespace Aestra
