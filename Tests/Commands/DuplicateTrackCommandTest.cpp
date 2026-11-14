// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// DuplicateTrackCommandTest — duplicate a track: a copy right under it with its own channel (settings copied), its
// lanes and clips; audio clips play through the copy's channel; one undo removes it, redo brings it back by id.

#include "Commands/DuplicateTrackCommand.h"
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
    const auto keys = pl.createLane("Keys");
    auto* voxCh = tm.addChannel("Vox");
    voxCh->setVolume(0.5f);
    voxCh->setPan(-0.25f);
    const uint64_t voxTrack = tm.createTrack(vox, "Vox", voxCh->getChannelId());
    tm.createTrack(keys, "Keys", tm.addChannel("Keys")->getChannelId());

    AudioSlicePayload payload;
    payload.durationSeconds = 2.0;
    const PatternID audio = tm.getPatternManager().createAudioPattern("vox take", 4.0, payload);
    tm.getPatternManager().setPatternMixerChannel(audio, voxCh->getChannelId());
    ClipInstance clip;
    clip.patternId = audio;
    clip.sourceId = audio.value;
    clip.startBeat = 8.0;
    clip.durationBeats = 4.0;
    pl.addClip(vox, clip);
    const size_t channelsBefore = tm.getChannelCount();

    std::cout << "[1] the copy sits under the original with its own channel\n";
    auto cmd = std::make_shared<DuplicateTrackCommand>(tm, vox);
    tm.getCommandHistory().pushAndExecute(cmd);
    const Track* copy = tm.getTrack(cmd->newTrackId());
    check(copy != nullptr && copy->name == "Vox copy", "a track named \"Vox copy\"");
    check(pl.getLaneIndex(cmd->newHeadLane()) == pl.getLaneIndex(vox) + 1, "right under the original");
    check(tm.getChannelCount() == channelsBefore + 1, "a new mixer channel");
    const auto* ch = copy ? tm.getChannelById(copy->channelId) : nullptr;
    check(ch && ch->getChannelId() != voxCh->getChannelId() && ch->getName() == "Vox copy", "its own channel, named");
    check(ch && ch->getVolume() == 0.5f && ch->getPan() == -0.25f, "level and pan copied");

    std::cout << "[2] its audio clip plays through the copy's channel\n";
    const auto* lane = pl.getLane(cmd->newHeadLane());
    check(lane && lane->clips.size() == 1 && lane->clips[0].startBeat == 8.0, "the clip, at the same place");
    const auto* p = lane && !lane->clips.empty() ? tm.getPatternManager().getPattern(lane->clips[0].patternId) : nullptr;
    check(p && lane->clips[0].patternId != audio && ch && p->getMixerChannelId() == ch->getChannelId(),
          "a cloned pattern routed to the copy's channel");
    check(tm.getPatternManager().getPattern(audio)->getMixerChannelId() == voxCh->getChannelId(),
          "the original still plays through its own");

    std::cout << "[3] one undo removes the copy; redo brings it back with the same ids\n";
    const uint64_t copyId = cmd->newTrackId();
    const auto head = cmd->newHeadLane();
    const uint32_t copyCh = copy->channelId;
    tm.getCommandHistory().undo();
    check(!tm.getTrack(copyId) && !pl.getLane(head) && !tm.getChannelById(copyCh) && tm.getChannelCount() == channelsBefore,
          "track, lane and channel gone");
    check(tm.getTrack(voxTrack) != nullptr, "the original stays");
    tm.getCommandHistory().redo();
    check(tm.getTrack(copyId) && pl.getLane(head) && pl.getLane(head)->clips.size() == 1 && tm.getChannelById(copyCh),
          "back with the same track, lane and channel ids");

    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All duplicate-track checks passed\n";
    return 0;
}
