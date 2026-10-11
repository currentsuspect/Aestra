// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraJSON.h"
#include "Core/AutomationRegions.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace Aestra {

/// Project format for automation clips (V8-A7): a curve's optional "regions" array. Written only
/// when the curve has regions, so a project that never used clips is byte-identical to before and
/// an older build ignores the key (the points still play).
inline void writeAutomationRegions(JSON& curveJson, const Audio::AutomationCurve& curve) {
    if (curve.regions.empty())
        return;
    JSON arr = JSON::array();
    for (const auto& r : curve.regions) {
        JSON rj = JSON::object();
        rj.set("id", JSON(std::to_string(r.id))); // decimal string, as instance ids are
        rj.set("start", JSON(r.startBeat));
        rj.set("end", JSON(r.endBeat));
        arr.push(rj);
    }
    curveJson.set("regions", arr);
}

/// Reads "regions" defensively: the file is untrusted. A region with a missing or malformed id, a
/// non-finite or reversed span, a repeated id, or an overlap with an earlier region is dropped,
/// never repaired; the points themselves are untouched, so a bad region costs only the grouping.
inline void readAutomationRegions(const JSON& curveJson, Audio::AutomationCurve& curve) {
    curve.regions.clear();
    if (!curveJson.has("regions") || !curveJson["regions"].isArray())
        return;
    const auto arr = curveJson["regions"].asArray();
    std::set<uint64_t> seen;
    for (size_t i = 0; i < arr.size(); ++i) {
        const JSON& rj = arr[i];
        if (!rj.isObject() || !rj.has("id") || !rj["id"].isString() || !rj.has("start") || !rj["start"].isNumber() ||
            !rj.has("end") || !rj["end"].isNumber())
            continue;
        const std::string rawId = rj["id"].asString();
        const bool digits = !rawId.empty() && rawId.size() <= 18 &&
                            std::all_of(rawId.begin(), rawId.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!digits)
            continue;
        Audio::AutomationRegion r;
        r.id = std::stoull(rawId);
        r.startBeat = rj["start"].asNumber();
        r.endBeat = rj["end"].asNumber();
        if (r.id == 0 || !seen.insert(r.id).second || !std::isfinite(r.startBeat) || !std::isfinite(r.endBeat) ||
            r.startBeat < 0.0 || r.endBeat < r.startBeat)
            continue;
        if (Audio::AutomationRegions::overlapsAnother(curve, 0, r.startBeat, r.endBeat))
            continue;
        curve.regions.push_back(r);
    }
    Audio::AutomationRegions::sortRegions(curve);
}

} // namespace Aestra
