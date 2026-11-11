// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// V83-HS1 (FD-63): gather MIDI notes for the harmony service. Read-only; never called from the audio thread.
#include "Harmony/HarmonyGather.h"

#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace Aestra {
namespace Audio {
namespace Harmony {

std::vector<HarmonyNote> gatherPatternNotes(const PatternSource& pattern) {
    std::vector<HarmonyNote> out;
    if (!pattern.isMidi())
        return out;
    const auto& notes = std::get<MidiPayload>(pattern.payload).notes;
    out.reserve(notes.size());
    for (const MidiNote& n : notes) {
        if (n.durationBeats <= 0.0)
            continue;
        out.push_back({n.startBeat, n.durationBeats, n.pitch, n.velocity});
    }
    return out;
}

std::vector<HarmonyNote> gatherArrangementNotes(TrackManager& tracks, double fromBeat, double toBeat) {
    std::vector<HarmonyNote> out;
    const bool ranged = toBeat > fromBeat;
    PlaylistModel& playlist = tracks.getPlaylistModel(); // read-only use; getLane has no const overload
    const PatternManager& patterns = tracks.getPatternManager();
    for (size_t i = 0; i < playlist.getLaneCount(); ++i) {
        const PlaylistLane* lane = playlist.getLane(playlist.getLaneId(i));
        if (!lane || lane->muted)
            continue;
        for (const ClipInstance& clip : lane->clips) {
            if (!clip.patternId.isValid() || clip.edits.muted || clip.durationBeats <= 0.0)
                continue;
            const PatternSource* pattern = patterns.getPattern(clip.patternId);
            if (!pattern || !pattern->isMidi())
                continue;
            const auto& notes = std::get<MidiPayload>(pattern->payload).notes;
            const double length = pattern->lengthBeats;
            // Repeat the pattern across a longer clip; a shorter clip cuts it.
            const double period = length > 0.0 ? length : clip.durationBeats;
            const int repeats = std::max(1, static_cast<int>(std::ceil((clip.durationBeats + clip.sourceOffset) / period - 1e-9)));
            for (int r = 0; r < repeats; ++r) {
                for (const MidiNote& n : notes) {
                    if (n.durationBeats <= 0.0)
                        continue;
                    double s = clip.startBeat + r * period + n.startBeat - clip.sourceOffset;
                    double e = s + n.durationBeats;
                    s = std::max(s, clip.startBeat);                // cut at the clip start
                    e = std::min(e, clip.startBeat + clip.durationBeats); // and at its end
                    if (ranged) {
                        s = std::max(s, fromBeat);
                        e = std::min(e, toBeat);
                    }
                    if (e - s <= 1e-9)
                        continue;
                    out.push_back({s, e - s, n.pitch, n.velocity});
                }
            }
        }
    }
    return out;
}

} // namespace Harmony
} // namespace Audio
} // namespace Aestra
