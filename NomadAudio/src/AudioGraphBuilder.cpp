// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioGraphBuilder.h"

namespace Nomad {
namespace Audio {

AudioGraph AudioGraphBuilder::buildFromTrackManager(const TrackManager& trackManager, double outputSampleRate) {
    AudioGraph graph;
    const size_t trackCount = trackManager.getTrackCount();
    graph.tracks.reserve(trackCount);
    uint64_t maxEndSample = 0;

    for (size_t t = 0; t < trackCount; ++t) {
        auto track = trackManager.getTrack(t);
        if (!track) {
            continue;
        }

        TrackRenderState trackState;
        trackState.trackId = track->getTrackId();
        trackState.trackIndex = track->getTrackIndex();
        trackState.volume = track->getVolume();
        trackState.pan = track->getPan();
        trackState.mute = track->isMuted();
        trackState.solo = track->isSoloed();

        const auto& audioData = track->getAudioData();
        const uint32_t channels = track->getNumChannels();
        if (!audioData.empty() && channels > 0) {
            // Single-clip fallback (until playlist provides multiple)
            ClipRenderState clip;
            clip.audioData = audioData.data();
            const uint64_t frames = static_cast<uint64_t>(audioData.size() / channels);
            const double startSeconds = track->getStartPositionInTimeline();
            const double trimStart = track->getTrimStart();
            const double trimEnd = track->getTrimEnd();
            const double sourceDuration = track->getDuration();
            const double effectiveEnd = (trimEnd > 0.0) ? trimEnd : sourceDuration;
            const double trimmedDuration = std::max(0.0, effectiveEnd - trimStart);

            clip.startSample = static_cast<uint64_t>(startSeconds * outputSampleRate);
            clip.endSample = clip.startSample + static_cast<uint64_t>(trimmedDuration * outputSampleRate);
            clip.sampleOffset = static_cast<uint64_t>(trimStart * track->getSampleRate());
            clip.totalFrames = frames;
            clip.sourceSampleRate = static_cast<double>(track->getSampleRate());
            clip.gain = 1.0f;
            clip.pan = 0.0f;

            // Clamp offset to available frames
            if (clip.sampleOffset > frames) {
                clip.sampleOffset = frames;
            }
            // Ensure endSample not before startSample
            if (clip.endSample < clip.startSample) {
                clip.endSample = clip.startSample;
            }

            trackState.clips.push_back(clip);
            if (clip.endSample > maxEndSample) {
                maxEndSample = clip.endSample;
            }
        }

        graph.tracks.push_back(std::move(trackState));
    }

    graph.timelineEndSample = maxEndSample;
    return graph;
}

} // namespace Audio
} // namespace Nomad
