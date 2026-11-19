// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/CrossfadeCommands.h"
#include "Commands/MacroCommand.h"

#include <unordered_set>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Overlap two audio clips and they crossfade (V83-E10, FD-94).
 *
 * Called by the gestures that can create a NEW overlap (a clip dropped or moved onto another) with the clips the
 * gesture touched. For each audio clip among them that now overlaps another audio clip on its lane, it returns a
 * Crossfade command, so the caller can put it in the SAME undo step as the move.
 *
 * It never overwrites what is there: a clip that already crossfades from another keeps that one, and a clip that
 * already feeds a crossfade does not feed a second. It never touches overlaps the gesture did not make, so a project
 * saved before crossfades existed keeps playing exactly as it did.
 */
inline std::vector<std::shared_ptr<ICommand>> autoCrossfadeCommands(PlaylistModel& model, PatternManager& patterns,
                                                                    const std::vector<ClipInstanceID>& touched) {
    std::vector<std::shared_ptr<ICommand>> out;
    std::unordered_set<ClipInstanceID> usedAsLater, usedAsEarlier;
    for (const ClipInstanceID& id : touched) {
        const ClipInstance* moved = model.getClip(id);
        if (!moved || !model.isAudioClip(*moved))
            continue;
        const PlaylistLane* lane = model.getLane(model.findClipLane(id));
        if (!lane)
            continue;
        for (const ClipInstance& other : lane->clips) {
            if (other.id == id || !model.isAudioClip(other))
                continue;
            const bool movedIsLater = moved->startBeat > other.startBeat;
            const ClipInstance& earlier = movedIsLater ? other : *moved;
            const ClipInstance& later = movedIsLater ? *moved : other;
            if (crossfadeOverlapBeats(earlier, later) <= 0.0)
                continue;
            if (later.edits.crossfadeFrom.isValid())
                continue; // already crossfades from a clip: keep that one
            bool earlierFeeds = false;
            for (const ClipInstance& c : lane->clips)
                earlierFeeds = earlierFeeds || c.edits.crossfadeFrom == earlier.id;
            if (earlierFeeds)
                continue;
            if (usedAsLater.count(later.id) || usedAsEarlier.count(earlier.id))
                continue;
            std::string why;
            if (auto cmd = makeCrossfadeCommand(model, patterns, earlier.id, later.id, &why)) {
                usedAsLater.insert(later.id);
                usedAsEarlier.insert(earlier.id);
                out.push_back(cmd);
            }
        }
    }
    return out;
}

} // namespace Audio
} // namespace Aestra
