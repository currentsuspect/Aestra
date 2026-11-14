// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RemoveTrackCommandTest — delete a track (DAW Essentials Checklist): its lanes, the track and its own mixer channel go
// in one undo step; undo puts every lane back where it was with its clips and ids, the track with its id, the channel
// with its id; a channel another track plays through stays.

#include "Commands/RemoveTrackCommand.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& label) {
    std::cout << (ok ? "  PASS: " : "FAIL: ") << label << "\n";
    if (!ok) ++g_failures;
}
std::vector<PlaylistLaneID> order(TrackManager& tm) {
    std::vector<PlaylistLaneID> out;
    for (size_t i = 0; i < tm.getPlaylistModel().getLaneCount(); ++i) out.push_back(tm.getPlaylistModel().getLaneId(i));
    return out;
}
} // namespace

int main() {
    TrackManager tm;
    auto& pl = tm.getPlaylistModel();
    const auto drums = pl.createLane("Drums");
    const auto take = pl.createLane("Drums take 2");
    const auto bass = pl.createLane("Bass");
    auto* drumsCh = tm.addChannel("Drums");
    const uint32_t drumsChId = drumsCh->getChannelId();
    const uint64_t drumsTrack = tm.createTrack(drums, "Drums", drumsChId);
    tm.attachLaneToTrack(drumsTrack, take);
    const uint64_t bassTrack = tm.createTrack(bass, "Bass", tm.addChannel("Bass")->getChannelId());
    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.startBeat = 4.0;
    clip.durationBeats = 4.0;
    pl.addClip(take, clip);
    const auto before = order(tm);
    const size_t channelsBefore = tm.getChannelCount();

    std::cout << "[1] deleting a track removes its lanes, the track and its own channel\n";
    check(RemoveTrackCommand::clipCount(tm, drums) == 1, "the menu sees the track's one clip (on its take lane)");
    auto cmd = std::make_shared<RemoveTrackCommand>(tm, drums);
    tm.getCommandHistory().pushAndExecute(cmd);
    check(!pl.getLane(drums) && !pl.getLane(take), "both lanes are gone");
    check(tm.getTrack(drumsTrack) == nullptr, "the track is gone");
    check(cmd->deletesChannel() && !tm.getChannelById(drumsChId) && tm.getChannelCount() == channelsBefore - 1,
          "its own channel is gone");
    check(tm.getTrack(bassTrack) != nullptr && pl.getLane(bass) != nullptr, "the other track is untouched");

    std::cout << "[2] one undo puts it all back\n";
    tm.getCommandHistory().undo();
    check(order(tm) == before, "every lane is back at its place");
    const Track* restored = tm.getTrack(drumsTrack);
    check(restored && restored->laneIds.size() == 2, "the track is back with its id and both lanes");
    check(pl.getLane(take) && pl.getLane(take)->clips.size() == 1 && pl.getLane(take)->trackId == drumsTrack,
          "the take lane has its clip and its owner");
    check(tm.getChannelById(drumsChId) != nullptr, "the channel is back with its id");
    tm.getCommandHistory().redo();
    check(tm.getTrack(drumsTrack) == nullptr && !tm.getChannelById(drumsChId), "redo deletes it again");
    tm.getCommandHistory().undo();

    std::cout << "[3] a channel another track plays through stays\n";
    tm.getTrack(bassTrack)->channelId = drumsChId; // Bass now plays through the Drums channel too
    auto shared = std::make_shared<RemoveTrackCommand>(tm, drums);
    tm.getCommandHistory().pushAndExecute(shared);
    check(!shared->deletesChannel() && tm.getChannelById(drumsChId) != nullptr, "the shared channel is kept");
    check(tm.getTrack(drumsTrack) == nullptr, "the track itself is deleted");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All delete-track checks passed\n";
    return 0;
}
