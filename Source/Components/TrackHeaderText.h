// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The second line of a timeline track header (FD-50, board 8): what plays the track, and where it goes when that
// is not Master: "Kick - Hard → Drum Bus", "Audio", "4 sounds". The board's "Arsenal ·" prefix is left out: next to
// the M S R keys the line has about 100 px, and the prefix used most of it. Header-only and free of the
// models, so TrackHeaderTextTest runs in the headless build; TrackUIComponentHeader.cpp gathers the facts.
#pragma once

#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace TrackHeader {

/// What a lane's clips play: the Arsenal units their notes use (distinct, in first-seen order) and whether any
/// clip is audio.
struct LaneSource {
    std::vector<std::string> unitNames;
    bool hasAudio = false;
};

/// The line under the track name. `outputName` is empty when the track goes to Master. Empty when there is
/// nothing to say (an empty track routed to Master).
inline std::string sourceLine(const LaneSource& source, const std::string& outputName) {
    std::string line;
    if (source.unitNames.size() == 1) {
        line = source.unitNames.front();
    } else if (source.unitNames.size() > 1) {
        line = std::to_string(source.unitNames.size()) + " sounds";
    }
    if (source.hasAudio) line = line.empty() ? "Audio" : line + " + audio";
    if (!outputName.empty()) line = line.empty() ? "→ " + outputName : line + " → " + outputName;
    return line;
}

/// A row shows the second line only when it is tall enough for two lines (board 8: "at 28 px the second line
/// drops away").
inline bool showsSourceLine(float rowHeight) {
    return rowHeight >= 34.0f;
}

} // namespace TrackHeader
} // namespace Audio
} // namespace Aestra
