// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RenameCommandTest — rename tracks and mixer channels: one undo step; a track and its channel that shared a name
// keep sharing it; a channel named on its own keeps its name.

#include "Commands/RenameCommand.h"
#include "Models/TrackManager.h"

#include <iostream>
#include <memory>
#include <string>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& label) {
    std::cout << (ok ? "  PASS: " : "FAIL: ") << label << "\n";
    if (!ok) ++g_failures;
}
} // namespace

int main() {
    TrackManager tm;
    auto& pl = tm.getPlaylistModel();
    const auto vox = pl.createLane("Vox");
    auto* voxCh = tm.addChannel("Vox");
    const uint64_t voxTrack = tm.createTrack(vox, "Vox", voxCh->getChannelId());
    const auto bass = pl.createLane("Bass");
    auto* bassCh = tm.addChannel("Low end bus"); // named on its own
    tm.createTrack(bass, "Bass", bassCh->getChannelId());

    std::cout << "[1] names are cleaned\n";
    check(RenameCommand::cleaned("  Lead vox \n") == "Lead vox", "trimmed");
    check(RenameCommand::cleaned("   ").empty(), "blank is empty");
    check(RenameCommand::cleaned(std::string(100, 'a')).size() == RenameCommand::kMaxNameBytes, "capped");

    std::cout << "[2] renaming a track renames its channel when they shared the name\n";
    tm.getCommandHistory().pushAndExecute(RenameCommand::forTrack(tm, vox, " Lead Vox "));
    check(pl.getLane(vox)->name == "Lead Vox" && tm.getTrack(voxTrack)->name == "Lead Vox", "lane and track renamed");
    check(voxCh->getName() == "Lead Vox", "the mixer strip follows");
    tm.getCommandHistory().undo();
    check(pl.getLane(vox)->name == "Vox" && voxCh->getName() == "Vox", "one undo puts both back");
    tm.getCommandHistory().redo();
    check(voxCh->getName() == "Lead Vox", "redo renames both again");

    std::cout << "[3] a channel named on its own keeps its name\n";
    tm.getCommandHistory().pushAndExecute(RenameCommand::forTrack(tm, bass, "Sub"));
    check(pl.getLane(bass)->name == "Sub" && bassCh->getName() == "Low end bus", "track renamed, bus name kept");

    std::cout << "[4] renaming a channel renames the track that shared its name\n";
    tm.getCommandHistory().pushAndExecute(RenameCommand::forChannel(tm, voxCh->getChannelId(), "Hook"));
    check(voxCh->getName() == "Hook" && pl.getLane(vox)->name == "Hook", "channel and track renamed");
    tm.getCommandHistory().pushAndExecute(RenameCommand::forChannel(tm, bassCh->getChannelId(), "808 bus"));
    check(bassCh->getName() == "808 bus" && pl.getLane(bass)->name == "Sub", "a track with its own name keeps it");

    std::cout << "[5] no-ops\n";
    check(RenameCommand::forTrack(tm, vox, "Hook")->empty(), "same name: nothing to do");
    check(RenameCommand::forTrack(tm, vox, "  ")->empty(), "blank name: nothing to do");
    check(RenameCommand::forChannel(tm, 0, "Main")->empty(), "the master is not renamed");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All rename checks passed\n";
    return 0;
}
