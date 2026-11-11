// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// V82-T2: which timeline tracks fold into the "N empty tracks · show" row. Only a track nobody has touched: one lane,
// no clips, no automation points, and still its default "Track N" name. Renaming or using a track keeps it in view,
// so folding can never hide work.

#include "Models/PlaylistModel.h"

#include <cctype>
#include <string>

namespace Aestra {
namespace Audio {

inline bool isDefaultTrackName(const std::string& name) {
    static const std::string prefix = "Track ";
    if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
        return false;
    for (size_t i = prefix.size(); i < name.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(name[i])))
            return false;
    return true;
}

inline bool isUntouchedTrackLane(const PlaylistLane& lane, size_t lanesInItsTrack) {
    if (lanesInItsTrack > 1 || !lane.clips.empty() || !isDefaultTrackName(lane.name))
        return false;
    for (const auto& curve : lane.automationCurves)
        if (!curve.getPoints().empty())
            return false;
    return true;
}

} // namespace Audio
} // namespace Aestra
