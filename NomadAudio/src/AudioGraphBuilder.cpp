// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioGraphBuilder.h"
#include "PlaylistRuntimeSnapshot.h"
#include <limits>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace Nomad {
namespace Audio {

namespace {
    uint64_t safeSecondsToSamples(long double seconds, double sampleRate) {
        const long double maxSamples = static_cast<long double>(std::numeric_limits<uint64_t>::max());
        const long double samples = seconds * static_cast<long double>(sampleRate);
        if (samples > maxSamples) return std::numeric_limits<uint64_t>::max();
        return static_cast<uint64_t>(std::llround(samples));
    }
}

AudioGraph AudioGraphBuilder::buildFromTrackManager(const TrackManager& trackManager, double outputSampleRate) {
    AudioGraph graph;
    const size_t channelCount = trackManager.getChannelCount();
    graph.tracks.reserve(channelCount);
    
    // Create track render states for all mixer channels
    for (size_t i = 0; i < channelCount; ++i) {
        auto channel = trackManager.getChannel(i);
        if (!channel) continue;

        TrackRenderState trackState;
        trackState.trackId = channel->getChannelId();
        trackState.trackIndex = static_cast<uint32_t>(i);
        trackState.volume = channel->getVolume();
        trackState.pan = channel->getPan();
        trackState.mute = channel->isMuted();
        trackState.solo = channel->isSoloed();
        
        graph.tracks.push_back(std::move(trackState));
    }

    // Populate clips from PlaylistModel
    const auto& playlist = trackManager.getPlaylistModel();
    const auto& patterns = trackManager.getPatternManager();
    const auto& sources = trackManager.getSourceManager();

    auto snapshot = playlist.buildRuntimeSnapshot(patterns, sources);
    if (snapshot) {
        uint64_t maxEndSample = 0;
        
        // Map snapshot lanes to mixer tracks.
        // In the current implementation, lane index usually corresponds to mixer channel index.
        for (size_t laneIdx = 0; laneIdx < snapshot->lanes.size(); ++laneIdx) {
            if (laneIdx >= graph.tracks.size()) break;
            
            auto& trackState = graph.tracks[laneIdx];
            const auto& laneInfo = snapshot->lanes[laneIdx];
            
            for (const auto& clipInfo : laneInfo.clips) {
                if (!clipInfo.isValid()) continue;
                
                ClipRenderState clip;
                // ClipRuntimeInfo.audioData is a raw pointer to AudioBufferData.
                // AudioGraph needs a shared_ptr to AudioBuffer for lifetime management,
                // but currently it just takes the raw pointer from the snapshot's buffer.
                // We'll bridge this by assuming SourceManager keeps the buffers alive.
                
                if (clipInfo.isAudio()) {
                    clip.audioData = clipInfo.audioData->interleavedData.data();
                    clip.totalFrames = clipInfo.audioData->numFrames;
                    clip.sourceSampleRate = static_cast<double>(clipInfo.sourceSampleRate);
                }
                
                clip.startSample = clipInfo.startTime;
                clip.endSample = clipInfo.getEndTime();
                clip.sampleOffset = clipInfo.sourceStart;
                clip.gain = clipInfo.gainLinear;
                clip.pan = clipInfo.pan;
                
                if (clip.endSample > maxEndSample) {
                    maxEndSample = clip.endSample;
                }
                
                trackState.clips.push_back(std::move(clip));
            }
            trackState.automationCurves = laneInfo.automationCurves;
        }
        
        graph.timelineEndSample = maxEndSample;
        graph.bpm = snapshot->bpm;
    }

    return graph;
}

} // namespace Audio
} // namespace Nomad
