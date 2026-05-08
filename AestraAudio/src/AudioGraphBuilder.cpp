// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioGraphBuilder.h"

#include "PlaylistRuntimeSnapshot.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace Aestra {
namespace Audio {

AudioGraph AudioGraphBuilder::buildFromTrackManager(TrackManager& trackManager) {
    AudioGraph graph;
    const size_t channelCount = trackManager.getChannelCount();
    graph.tracks.reserve(channelCount);

    // Create track render states for all mixer channels
    bool anySoloFound = false;
    for (size_t i = 0; i < channelCount; ++i) {
        auto channel = trackManager.getChannel(i);
        if (!channel)
            continue;

        TrackRenderState trackState;
        trackState.trackId = channel->getChannelId();
        trackState.trackIndex = static_cast<uint32_t>(i);
        trackState.volume = channel->getVolume();
        trackState.pan = channel->getPan();
        trackState.mute = channel->isMuted();
        trackState.solo = channel->isSoloed();
        if (trackState.solo)
            anySoloFound = true;
        trackState.isSoloSafe = channel->isSoloSafe();

        // Copy Routing
        trackState.mainOutputId = channel->getMainOutputId();
        trackState.sends = channel->getSends();
        trackState.effectChainSnapshot = channel->getEffectChainSnapshot();

        graph.tracks.push_back(std::move(trackState));
    }
    graph.anySolo = anySoloFound;

    // Populate clips from PlaylistModel
    const auto& playlist = trackManager.getPlaylistModel();
    const auto& patterns = trackManager.getPatternManager();
    const auto& sources = trackManager.getSourceManager();

    auto snapshot = playlist.buildRuntimeSnapshot(patterns, sources);
    if (snapshot) {
        uint64_t maxEndSample = 0;
        double projectSampleRate = playlist.getProjectSampleRate();

        // Map snapshot lanes to mixer tracks.
        // In the current implementation, lane index usually corresponds to mixer channel index.
        for (size_t laneIdx = 0; laneIdx < snapshot->lanes.size(); ++laneIdx) {
            if (laneIdx >= graph.tracks.size()) {
                std::cerr << "[AudioGraphBuilder] Warning: snapshot has more lanes (" << snapshot->lanes.size()
                          << ") than mixer tracks (" << graph.tracks.size() << "). Extra lanes dropped.\n";
                break;
            }

            auto& trackState = graph.tracks[laneIdx];
            const auto& laneInfo = snapshot->lanes[laneIdx];

            for (const auto& clipInfo : laneInfo.clips) {
                if (!clipInfo.isValid())
                    continue;

                ClipRenderState clip;
                // ClipRuntimeInfo.audioData is a raw pointer to AudioBufferData.
                // AudioGraph needs a shared_ptr to AudioBuffer for lifetime management,
                // but currently it just takes the raw pointer from the snapshot's buffer.
                // We'll bridge this by assuming SourceManager keeps the buffers alive.

                if (clipInfo.isAudio()) {
                    clip.audioData = clipInfo.audioData->interleavedData.data();
                    clip.totalFrames = clipInfo.audioData->numFrames;
                    clip.sourceSampleRate = static_cast<double>(clipInfo.sourceSampleRate);
                    clip.channels = clipInfo.sourceChannels;
                }

                clip.startSample = clipInfo.startTime;
                clip.endSample = clipInfo.getEndTime();

                // Convert sourceStart (Project Rate) to sampleOffset (Source Rate)
                // Use double precision to prevent audio popping due to sub-sample drift
                if (projectSampleRate > 0.0 && clip.sourceSampleRate > 0.0) {
                    clip.sampleOffset =
                        static_cast<double>(clipInfo.sourceStart) * (clip.sourceSampleRate / projectSampleRate);
                } else {
                    clip.sampleOffset = static_cast<double>(clipInfo.sourceStart);
                }

                clip.gain = clipInfo.gainLinear;
                clip.pan = clipInfo.pan;

                if (clip.endSample > maxEndSample) {
                    maxEndSample = clip.endSample;
                }

                trackState.clips.push_back(std::move(clip));
            }
            trackState.automationCurves = std::move(laneInfo.automationCurves);
        }

        graph.timelineEndSample = maxEndSample;
        graph.bpm = snapshot->bpm;
    }

    return graph;
}

} // namespace Audio
} // namespace Aestra
