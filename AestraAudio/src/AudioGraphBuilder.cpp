// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioGraphBuilder.h"
#include "Core/ClipRenderKernel.h"

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
        graph.anySolo = graph.anySolo || (track.solo && !track.mute);
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
    // Routing Contract D1: a cycle is corruption, not a rendering fallback.
    // The partial order is left as-is (never appended with leftover nodes);
    // the publish gate (AudioEngine::setGraph) refuses the snapshot.
}

AudioGraph AudioGraphBuilder::buildFromTrackManager(TrackManager& trackManager) {
    // Phase 4 (F1): apply finished anti-alias prefilter results and queue missing
    // work for downsampled clips BEFORE the snapshot resolves clip buffers, so this
    // build picks up every copy that is ready (never blocks; fallback = original).
    trackManager.ensureClipPrefilters();

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
        if (trackState.solo && !trackState.mute)
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

        // Playlist lanes are arrangement-only. Audio patterns carry their own
        // stable mixer destination, so moving a clip between lanes cannot reroute it.
        for (size_t laneIdx = 0; laneIdx < snapshot->lanes.size(); ++laneIdx) {
            // Non-const: automationCurves below is genuinely moved out of the
            // snapshot (std::move through a const ref silently copies).
            auto& laneInfo = snapshot->lanes[laneIdx];

            for (const auto& clipInfo : laneInfo.clips) {
                if (!clipInfo.isValid())
                    continue;

                ClipRenderState clip = ClipRenderKernel::makeClipRenderState(clipInfo, projectSampleRate);

                if (clip.endSample > maxEndSample) {
                    maxEndSample = clip.endSample;
                }

                if (clipInfo.mixerChannelId == 0) {
                    graph.masterClips.push_back(std::move(clip));
                } else {
                    const auto destination = std::find_if(graph.tracks.begin(), graph.tracks.end(),
                                                          [&clipInfo](const TrackRenderState& track) {
                                                              return track.trackId == clipInfo.mixerChannelId;
                                                          });
                    if (destination != graph.tracks.end()) {
                        destination->clips.push_back(std::move(clip));
                    } else {
                        // Missing/corrupt destinations fail safe to Master rather
                        // than dropping an audible source from the project.
                        graph.masterClips.push_back(std::move(clip));
                    }
                }
            }

            // Automation is displayed on Playlist lanes but targets an
            // explicit stable mixer insert. A lane may host curves for
            // multiple inserts without owning any of them.
            for (auto& curve : laneInfo.automationCurves) {
                const auto destination =
                    std::find_if(graph.tracks.begin(), graph.tracks.end(), [&curve](const TrackRenderState& track) {
                        return curve.mixerChannelId != 0 && track.trackId == curve.mixerChannelId;
                    });
                if (destination != graph.tracks.end()) {
                    destination->automationCurves.push_back(std::move(curve));
                }
            }
        }

        graph.timelineEndSample = maxEndSample;
        graph.bpm = snapshot->bpm;
    }

    finalizeAudioGraphRouting(graph);
    return graph;
}

} // namespace Audio
} // namespace Aestra
