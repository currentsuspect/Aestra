// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioGraphBuilder.h"

#include "PlaylistRuntimeSnapshot.h"

#include <algorithm>
#include <cmath>
#include "AestraLog.h"
#include "AestraDebug.h"
#include <cstddef>
#include <limits>

namespace Aestra {
namespace Audio {

void finalizeAudioGraphRouting(AudioGraph& graph) {
    const size_t trackCount = graph.tracks.size();
    graph.anySolo = false;
    graph.audibleDownstream.assign(trackCount, {});
    graph.audibleIncoming.assign(trackCount, {});
    graph.sidechainIncoming.assign(trackCount, {});
    graph.topologicalEdges.assign(trackCount, {});
    graph.topologicalOrder.clear();
    graph.topologicalOrder.reserve(trackCount);
    graph.hasRoutingCycle = false;

    uint32_t maxTrackId = 0;
    for (const auto& track : graph.tracks) {
        maxTrackId = std::max(maxTrackId, track.trackId);
        graph.anySolo = graph.anySolo || track.solo;
    }

    graph.trackIndexById.assign(static_cast<size_t>(maxTrackId) + 1, AudioGraph::kInvalidTrackIndex);
    for (size_t i = 0; i < trackCount; ++i) {
        const uint32_t trackId = graph.tracks[i].trackId;
        if (trackId < graph.trackIndexById.size()) {
            graph.trackIndexById[trackId] = i;
        }
    }

    auto findTrackIndex = [&graph](uint32_t trackId) -> size_t {
        if (trackId < graph.trackIndexById.size()) {
            return graph.trackIndexById[trackId];
        }
        return AudioGraph::kInvalidTrackIndex;
    };

    auto addEdge = [&](size_t sourceIndex, uint32_t targetTrackId, bool sidechainOnly) {
        constexpr uint32_t kMasterOutputId = 0xFFFFFFFFu;
        if (targetTrackId == kMasterOutputId) {
            return;
        }

        const size_t targetIndex = findTrackIndex(targetTrackId);
        if (targetIndex == AudioGraph::kInvalidTrackIndex || targetIndex == sourceIndex) {
            return;
        }

        graph.topologicalEdges[sourceIndex].push_back(targetIndex);
        if (sidechainOnly) {
            graph.sidechainIncoming[targetIndex].push_back(sourceIndex);
            return;
        }

        graph.audibleDownstream[sourceIndex].push_back(targetIndex);
        graph.audibleIncoming[targetIndex].push_back(sourceIndex);
    };

    for (size_t i = 0; i < trackCount; ++i) {
        const auto& track = graph.tracks[i];
        addEdge(i, track.mainOutputId, false);
        for (const auto& send : track.sends) {
            if (send.mute) {
                continue;
            }
            addEdge(i, send.targetChannelId, send.sidechainOnly);
        }
    }

    std::vector<uint32_t> indegree(trackCount, 0);
    for (const auto& edges : graph.topologicalEdges) {
        for (const size_t targetIndex : edges) {
            if (targetIndex < trackCount) {
                ++indegree[targetIndex];
            }
        }
    }

    std::vector<size_t> queue;
    queue.reserve(trackCount);
    for (size_t i = 0; i < trackCount; ++i) {
        if (indegree[i] == 0) {
            queue.push_back(i);
        }
    }

    size_t read = 0;
    while (read < queue.size()) {
        const size_t index = queue[read++];
        graph.topologicalOrder.push_back(index);
        for (const size_t targetIndex : graph.topologicalEdges[index]) {
            if (targetIndex < trackCount && --indegree[targetIndex] == 0) {
                queue.push_back(targetIndex);
            }
        }
    }

    graph.hasRoutingCycle = graph.topologicalOrder.size() != trackCount;
    if (graph.hasRoutingCycle) {
        std::vector<uint8_t> visited(trackCount, 0);
        for (const size_t index : graph.topologicalOrder) {
            if (index < trackCount) {
                visited[index] = 1;
            }
        }
        for (size_t i = 0; i < trackCount; ++i) {
            if (visited[i] == 0) {
                graph.topologicalOrder.push_back(i);
            }
        }
    }
}

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
                AESTRA_DEBUG_ONLY(std::cerr << "[AudioGraphBuilder] Warning: snapshot has more lanes (" << snapshot->lanes.size()
                          << ") than mixer tracks (" << graph.tracks.size() << "). Extra lanes dropped.\n");
                break;
            }

            auto& trackState = graph.tracks[laneIdx];
            const auto& laneInfo = snapshot->lanes[laneIdx];

            for (const auto& clipInfo : laneInfo.clips) {
                if (!clipInfo.isValid())
                    continue;

                ClipRenderState clip;
                // ClipRenderState.bufferOwner holds a shared_ptr<AudioBufferData>
                // that extends the buffer's lifetime independently of SourceManager.
                // audioData is extracted from the same buffer as a raw pointer for hot-path use.

                if (clipInfo.isAudio()) {
                    clip.audioData = clipInfo.audioData->interleavedData.data();
                    clip.bufferOwner = clipInfo.sharedAudioData;
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

    finalizeAudioGraphRouting(graph);
    return graph;
}

} // namespace Audio
} // namespace Aestra
