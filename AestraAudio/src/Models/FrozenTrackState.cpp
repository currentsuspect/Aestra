// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Models/FrozenTrackState.h"

#include <cstdlib>
#include <sstream>

namespace Aestra {
namespace Audio {

namespace {

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in(s);
    while (std::getline(in, cur, sep))
        if (!cur.empty())
            out.push_back(cur);
    return out;
}

} // namespace

// Format, version 1:  v1|<frozenClip>|<clip>,<clip>|<slot>,<slot>|<lane>:<instance>:<param>,...
std::string FrozenTrackState::toText() const {
    if (!active())
        return {};
    std::string out = "v1|" + frozenClip.toString() + "|";
    for (size_t i = 0; i < mutedClips.size(); ++i)
        out += (i ? "," : "") + mutedClips[i].toString();
    out += "|";
    for (size_t i = 0; i < bypassedSlots.size(); ++i)
        out += (i ? "," : "") + std::to_string(bypassedSlots[i]);
    out += "|";
    for (size_t i = 0; i < mutedCurves.size(); ++i)
        out += (i ? "," : "") + mutedCurves[i].lane.toString() + ":" + std::to_string(mutedCurves[i].deviceInstanceId) +
               ":" + std::to_string(mutedCurves[i].paramId);
    return out;
}

FrozenTrackState FrozenTrackState::fromText(const std::string& text) {
    FrozenTrackState state;
    // Keep empty fields: "a||b" must give three parts, so split by hand here.
    std::vector<std::string> parts;
    size_t from = 0;
    while (true) {
        const size_t bar = text.find('|', from);
        parts.push_back(text.substr(from, bar == std::string::npos ? std::string::npos : bar - from));
        if (bar == std::string::npos)
            break;
        from = bar + 1;
    }
    if (parts.size() != 5 || parts[0] != "v1")
        return state; // unknown or damaged: load as not frozen, the project still opens

    const ClipInstanceID frozen = ClipInstanceID::fromString(parts[1]);
    if (!frozen.isValid())
        return state;
    state.frozenClip = frozen;
    for (const auto& id : split(parts[2], ',')) {
        const ClipInstanceID clip = ClipInstanceID::fromString(id);
        if (clip.isValid())
            state.mutedClips.push_back(clip);
    }
    for (const auto& slot : split(parts[3], ','))
        state.bypassedSlots.push_back(static_cast<uint32_t>(std::strtoul(slot.c_str(), nullptr, 10)));
    for (const auto& entry : split(parts[4], ',')) {
        const auto f = split(entry, ':');
        if (f.size() != 3)
            continue;
        AestraUUID laneId;
        if (!AestraUUID::tryParse(f[0], laneId))
            continue;
        FrozenTrackState::MutedCurve curve;
        curve.lane = PlaylistLaneID(laneId);
        curve.deviceInstanceId = std::strtoull(f[1].c_str(), nullptr, 10);
        curve.paramId = static_cast<uint32_t>(std::strtoul(f[2].c_str(), nullptr, 10));
        state.mutedCurves.push_back(curve);
    }
    return state;
}

} // namespace Audio
} // namespace Aestra
