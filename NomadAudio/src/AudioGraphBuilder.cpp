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

        const uint32_t channels = track->getNumChannels();

        // Resolve an owned buffer for this snapshot. If the track already has a
        // shared decoded buffer, reuse it; otherwise, copy from the track's
        // internal vector so edits/clears can't invalidate the active graph.
        std::shared_ptr<const AudioBuffer> clipBuffer = track->getSampleBuffer();
        const std::vector<float>* audioDataPtr = nullptr;
        if (clipBuffer && clipBuffer->ready.load(std::memory_order_relaxed)) {
            audioDataPtr = &clipBuffer->data;
        } else {
            const auto& audioData = track->getAudioData();
            if (!audioData.empty() && channels > 0) {
                auto owned = std::make_shared<AudioBuffer>();
                owned->data = audioData;
                owned->channels = channels;
                owned->sampleRate = track->getSampleRate();
                owned->numFrames = owned->channels > 0 ? owned->data.size() / owned->channels : 0;
                owned->ready.store(true, std::memory_order_relaxed);
                owned->sourcePath = track->getSourcePath();
                clipBuffer = owned;
                audioDataPtr = &owned->data;
            }
        }

        if (audioDataPtr && !audioDataPtr->empty() && channels > 0) {
            // Single-clip fallback (until playlist provides multiple)
            ClipRenderState clip;
            clip.buffer = clipBuffer;
            clip.audioData = audioDataPtr->data();
            const uint64_t frames = static_cast<uint64_t>(audioDataPtr->size() / channels);
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
