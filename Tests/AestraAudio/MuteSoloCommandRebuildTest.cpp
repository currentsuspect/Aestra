// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioGraphBuilder.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Models/TrackManager.h"

#include <iostream>

using namespace Aestra::Audio;

namespace {

const TrackRenderState* findTrack(const AudioGraph& graph, uint32_t channelId) {
    for (const auto& track : graph.tracks) {
        if (track.trackId == channelId) {
            return &track;
        }
    }
    return nullptr;
}

bool drainAndCheck(TrackManager& trackManager, uint32_t channelId, bool expectMuted, bool expectSoloed,
                   const char* step) {
    // PlaybackGraphController::drainIfDirty() consumes the pending flag and
    // rebuilds the published graph from current channel state.
    if (!trackManager.consumePendingGraphRebuild()) {
        std::cerr << "FAIL[" << step << "]: no pending graph rebuild after the command\n";
        return false;
    }
    auto graph = AudioGraphBuilder::buildFromTrackManager(trackManager);
    const TrackRenderState* track = findTrack(graph, channelId);
    if (!track || track->mute != expectMuted || track->solo != expectSoloed) {
        std::cerr << "FAIL[" << step << "]: published snapshot mismatch (mute=" << (track ? track->mute : false)
                  << " solo=" << (track ? track->solo : false) << ")\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    TrackManager trackManager;
    MixerChannel* channel = trackManager.addChannel("Mute Rebuild Test");
    if (!channel) {
        std::cerr << "failed to add channel\n";
        return 1;
    }
    const uint32_t channelId = channel->getChannelId();
    if (channelId == 0) {
        std::cerr << "expected a non-master channel id\n";
        return 1;
    }

    auto& history = trackManager.getCommandHistory();

    // Regression for #782: the mute command must request a graph rebuild, or
    // the published snapshot keeps the old mute state until an unrelated
    // operation forces a rebuild — unmute stays inaudible.
    history.pushAndExecute(std::make_shared<SetMuteCommand>(trackManager, *channel, true));
    if (!drainAndCheck(trackManager, channelId, true, false, "mute")) {
        return 1;
    }

    history.pushAndExecute(std::make_shared<SetMuteCommand>(trackManager, *channel, false));
    if (!drainAndCheck(trackManager, channelId, false, false, "unmute")) {
        return 1;
    }

    // Undo of the unmute must request another rebuild and restore the muted
    // published snapshot.
    history.undo();
    if (!drainAndCheck(trackManager, channelId, true, false, "undo-unmute")) {
        return 1;
    }
    history.redo();
    if (!drainAndCheck(trackManager, channelId, false, false, "redo-unmute")) {
        return 1;
    }

    // Solo: same contract — the published snapshot must follow immediately.
    history.pushAndExecute(std::make_shared<SetSoloCommand>(trackManager, *channel, true));
    if (!drainAndCheck(trackManager, channelId, false, true, "solo")) {
        return 1;
    }
    history.pushAndExecute(std::make_shared<SetSoloCommand>(trackManager, *channel, false));
    if (!drainAndCheck(trackManager, channelId, false, false, "unsolo")) {
        return 1;
    }

    // Volume: the strip gain reads the SNAPSHOT (track.volume), so a volume
    // command without a rebuild leaves the fader cosmetic until an unrelated
    // edit rebuilds the graph. Regression for the cosmetic-fader report.
    history.pushAndExecute(std::make_shared<SetVolumeCommand>(trackManager, *channel, 0.25f));
    if (!drainAndCheck(trackManager, channelId, false, false, "volume")) {
        return 1;
    }
    {
        auto graph = AudioGraphBuilder::buildFromTrackManager(trackManager);
        const TrackRenderState* track = findTrack(graph, channelId);
        if (!track || std::abs(track->volume - 0.25f) > 0.0001f) {
            std::cerr << "FAIL[volume]: snapshot volume does not reflect the command ("
                      << (track ? track->volume : -1.0f) << ")\n";
            return 1;
        }
    }
    history.undo();
    if (!drainAndCheck(trackManager, channelId, false, false, "undo-volume")) {
        return 1;
    }
    {
        auto graph = AudioGraphBuilder::buildFromTrackManager(trackManager);
        const TrackRenderState* track = findTrack(graph, channelId);
        if (!track || std::abs(track->volume - 1.0f) > 0.0001f) {
            std::cerr << "FAIL[undo-volume]: snapshot volume not restored\n";
            return 1;
        }
    }

    // Pan: same contract.
    history.pushAndExecute(std::make_shared<SetPanCommand>(trackManager, *channel, 0.5f));
    if (!drainAndCheck(trackManager, channelId, false, false, "pan")) {
        return 1;
    }
    {
        auto graph = AudioGraphBuilder::buildFromTrackManager(trackManager);
        const TrackRenderState* track = findTrack(graph, channelId);
        if (!track || std::abs(track->pan - 0.5f) > 0.0001f) {
            std::cerr << "FAIL[pan]: snapshot pan does not reflect the command\n";
            return 1;
        }
    }

    std::cout << "mute/solo/volume/pan command rebuild passed\n";
    return 0;
}
