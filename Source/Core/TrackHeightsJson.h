// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The timeline's track heights in the project's UI state: the shared height and the rows resized away from it
// (keyed by lane id). Optional on load: a file without them keeps the default heights.

#include "AestraJSON.h"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Aestra {

inline void trackHeightsToJson(JSON& ui, float trackHeight, const std::vector<std::pair<std::string, float>>& rows) {
    if (trackHeight <= 0.0f) return;
    JSON obj = JSON::object();
    obj.set("default", JSON(static_cast<double>(trackHeight)));
    JSON list = JSON::array();
    for (const auto& [lane, h] : rows) {
        JSON item = JSON::object();
        item.set("lane", JSON(lane));
        item.set("height", JSON(static_cast<double>(h)));
        list.push(item);
    }
    obj.set("rows", list);
    ui.set("trackHeights", obj);
}

inline void trackHeightsFromJson(const JSON& ui, float& trackHeight, std::vector<std::pair<std::string, float>>& rows) {
    if (!ui.has("trackHeights") || !ui["trackHeights"].isObject()) return;
    const JSON& obj = ui["trackHeights"];
    if (obj.has("default") && obj["default"].isNumber()) {
        const double d = obj["default"].asNumber();
        if (std::isfinite(d) && d > 0.0 && d < 2000.0) trackHeight = static_cast<float>(d);
    }
    if (!obj.has("rows") || !obj["rows"].isArray()) return;
    const JSON& list = obj["rows"];
    for (size_t i = 0; i < list.size() && rows.size() < 4096; ++i) { // a hostile file cannot grow this without bound
        const JSON& item = list[i];
        if (!item.isObject() || !item.has("lane") || !item["lane"].isString() || !item.has("height") ||
            !item["height"].isNumber())
            continue;
        const double h = item["height"].asNumber();
        if (std::isfinite(h) && h > 0.0 && h < 2000.0) rows.emplace_back(item["lane"].asString(), static_cast<float>(h));
    }
}

} // namespace Aestra
