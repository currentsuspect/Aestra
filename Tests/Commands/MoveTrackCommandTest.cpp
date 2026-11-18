// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MoveTrackCommandTest — reorder tracks (DAW Essentials Checklist P0): a row moves one place, the track's lanes travel
// with it, and undo restores the exact lane order.

#include "Commands/MoveTrackCommand.h"
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

std::vector<PlaylistLaneID> laneOrder(TrackManager& tm) {
    std::vector<PlaylistLaneID> out;
    for (size_t i = 0; i < tm.getPlaylistModel().getLaneCount(); ++i) out.push_back(tm.getPlaylistModel().getLaneId(i));
    return out;
}

/** The head lane of each row, top to bottom. */
std::vector<PlaylistLaneID> heads(TrackManager& tm) {
    std::vector<PlaylistLaneID> out;
    for (const auto& row : MoveTrackCommand::rows(tm)) out.push_back(row.front());
    return out;
}
} // namespace

int main() {
    TrackManager tm;
    auto& pl = tm.getPlaylistModel();
    // Rows: Drums (with a take lane), Bass, a lane no track owns, Keys.
    const auto drums = pl.createLane("Drums");
    const auto drumsTake = pl.createLane("Drums take");
    const auto bass = pl.createLane("Bass");
    const auto loose = pl.createLane("Loose");
    const auto keys = pl.createLane("Keys");
    const uint64_t drumsTrack = tm.createTrack(drums, "Drums", tm.addChannel("Drums")->getChannelId());
    tm.attachLaneToTrack(drumsTrack, drumsTake);
    tm.createTrack(bass, "Bass", tm.addChannel("Bass")->getChannelId());
    tm.createTrack(keys, "Keys", tm.addChannel("Keys")->getChannelId());

    check((heads(tm) == std::vector<PlaylistLaneID>{drums, bass, loose, keys}), "four rows: Drums, Bass, Loose, Keys");
    check(!MoveTrackCommand::canMove(tm, drums, -1), "the top row cannot move up");
    check(MoveTrackCommand::canMove(tm, drumsTake, +1), "a take lane moves its whole track");
    const auto before = laneOrder(tm);

    tm.getCommandHistory().pushAndExecute(std::make_shared<MoveTrackCommand>(tm, drums, +1));
    check((heads(tm) == std::vector<PlaylistLaneID>{bass, drums, loose, keys}), "Drums moved below Bass");
    const auto after = laneOrder(tm);
    check(after.size() == before.size() && after[1] == drums && after[2] == drumsTake, "Drums' take lane travelled with it");

    tm.getCommandHistory().pushAndExecute(std::make_shared<MoveTrackCommand>(tm, keys, -1));
    check((heads(tm) == std::vector<PlaylistLaneID>{bass, drums, keys, loose}), "Keys moved above the loose lane");

    tm.getCommandHistory().undo();
    tm.getCommandHistory().undo();
    check(laneOrder(tm) == before, "two undos restore the exact lane order");
    tm.getCommandHistory().redo();
    check((heads(tm) == std::vector<PlaylistLaneID>{bass, drums, loose, keys}), "redo moves Drums again");

    auto noop = std::make_shared<MoveTrackCommand>(tm, keys, +1);
    noop->execute();
    check(!noop->moved() && !noop->changesProjectState(), "moving the bottom row down does nothing");

    // Hidden rows (folded empty tracks) are stepped over: with Loose hidden, Keys moves above it to Drums' side.
    tm.getCommandHistory().pushAndExecute(
        std::make_shared<MoveTrackCommand>(tm, keys, -1, std::vector<PlaylistLaneID>{bass, drums, drumsTake, keys}));
    check((heads(tm) == std::vector<PlaylistLaneID>{bass, keys, drums, loose}), "Keys stepped over the hidden row to sit above Drums");
    check(!MoveTrackCommand::canMove(tm, drums, +1, {bass, drums, drumsTake, keys}),
          "the last shown row cannot move down past hidden rows only");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All move-track checks passed\n";
    return 0;
}
