// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// "Place in timeline" (the Arsenal's header button): put the active pattern into the arrangement as one clip,
// where Models/PatternPlacement.h says. Kept out of AestraContent.cpp, which is at its size pin.
#include "AestraContent.h"

#include "Commands/AddClipCommand.h"
#include "Commands/CommandTransaction.h"
#include "Commands/CreateTrackWithLaneCommand.h"
#include "Models/PatternPlacement.h"
#include "TrackManager.h"
#include "TrackManagerUI.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

void AestraContent::placePatternInTimeline(PatternID pid) {
    if (!m_trackManager || !pid.isValid()) return;
    auto& tm = *m_trackManager;
    auto& playlist = tm.getPlaylistModel();
    const auto* pattern = tm.getPatternManager().getPattern(pid);
    if (!pattern) return;

    // Where the arrangement stands: every clip, with its lane's place in the list.
    std::vector<PlacedClip> clips;
    std::vector<PlaylistLaneID> laneIds;
    playlist.withLanes([&](const auto& lanes) {
        for (size_t i = 0; i < lanes.size(); ++i) {
            laneIds.push_back(lanes[i].id);
            for (const auto& c : lanes[i].clips) clips.push_back({static_cast<int>(i), c.startBeat, c.startBeat + c.durationBeats, c.patternId.value});
        }
        return 0;
    });

    // In the Arsenal the transport is the pattern loop, so the timeline's cursor is where it was left on the way in.
    const double cursorSeconds = m_viewFocus == ViewFocus::Arsenal ? m_savedTimelinePosition : tm.getPosition();
    const double beatsPerBar = static_cast<double>(tm.getTimelineClock().getBeatsPerBar());
    const PlacementTarget target = planPlacement(clips, static_cast<int>(laneIds.size()), pid.value, pattern->lengthBeats,
                                                 playlist.secondsToBeats(std::max(0.0, cursorSeconds)), beatsPerBar);

    // A new lane and its clip are one undo step, like a drop (the lane is part of the edit).
    std::shared_ptr<CreateTrackWithLaneCommand> laneCommand;
    PlaylistLaneID laneId;
    if (target.lane >= 0) {
        laneId = laneIds[static_cast<size_t>(target.lane)];
    } else {
        laneCommand = std::make_shared<CreateTrackWithLaneCommand>(tm, pattern->name.empty() ? "Pattern" : pattern->name);
        laneCommand->execute();
        laneId = laneCommand->getLaneId();
        if (!laneId.isValid()) { showToast("Could not make a track for the pattern"); return; }
    }

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = target.startBeat;
    clip.durationBeats = pattern->lengthBeats;
    clip.patternId = pid;
    clip.sourceId = pid.value;
    auto add = std::make_shared<AddClipCommand>(playlist, laneId, clip);
    add->execute();
    if (!playlist.getClip(clip.id)) {
        if (laneCommand) laneCommand->undo();
        showToast("Could not place the pattern");
        return;
    }
    auto transaction = std::make_shared<CommandTransaction>("Place Pattern in Timeline");
    if (laneCommand) transaction->add(laneCommand);
    transaction->add(add);
    transaction->markExecuted();
    tm.getCommandHistory().pushExecuted(transaction);

    tm.refreshTimelinePatternInstances(); // a clip added while playing must be scheduled
    if (m_trackManagerUI) {
        m_trackManagerUI->refreshTracks();
        m_trackManagerUI->invalidateCache();
    }
    setDirty(true);
    const int bar = static_cast<int>(std::floor(target.startBeat / std::max(beatsPerBar, 1.0))) + 1;
    const int track = (target.lane >= 0 ? target.lane : static_cast<int>(laneIds.size())) + 1;
    showToast("Placed " + (pattern->name.empty() ? std::string("pattern") : pattern->name) + " at bar " + std::to_string(bar) + ", track " + std::to_string(track));
}
