// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace Aestra::Audio;

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kTotalFrames = kSampleRate;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

std::vector<float> render(const std::shared_ptr<TrackManager>& tracks) {
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tracks);
    engine.setBPM(120.0f);
    engine.setSafetyLimiterEnabled(false);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tracks));
    engine.initialize();
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    std::vector<float> output(static_cast<size_t>(kTotalFrames) * kChannels, 0.0f);
    std::vector<float> block(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    uint32_t rendered = 0;
    while (rendered < kTotalFrames) {
        const uint32_t frames = std::min(kBlockSize, kTotalFrames - rendered);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, frames, 0.0);
        std::copy_n(block.begin(), static_cast<size_t>(frames) * kChannels,
                    output.begin() + static_cast<ptrdiff_t>(rendered) * kChannels);
        rendered += frames;
    }
    engine.setTransportPlaying(false);
    return output;
}
} // namespace

int main() {
    using namespace Aestra::Audio;

    auto tracks = std::make_shared<TrackManager>();
    tracks->setOutputSampleRate(kSampleRate);

    auto sourceBuffer = std::make_shared<AudioBufferData>();
    sourceBuffer->sampleRate = kSampleRate;
    sourceBuffer->numChannels = 1;
    sourceBuffer->numFrames = kTotalFrames;
    sourceBuffer->interleavedData.assign(kTotalFrames, 1.0f);

    const ClipSourceID sourceId =
        tracks->getSourceManager().createRecordedSource("instance-render.wav", "Instance Render", sourceBuffer);
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = 1.0;
    payload.slices.push_back({0.0, 1.0, 0.0, static_cast<double>(kTotalFrames)});
    const PatternID patternId = tracks->getPatternManager().createAudioPattern("Instance Render", 2.0, payload);
    const PlaylistLaneID laneId = tracks->getPlaylistModel().createLane("Arrangement");
    const ClipInstanceID clipId = tracks->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, 2.0);
    auto* clip = tracks->getPlaylistModel().getClip(clipId);
    require(clip != nullptr, "Audio clip setup failed");
    require(std::abs(clip->edits.gainLinear - DEFAULT_AUDIO_CLIP_GAIN_LINEAR) < 1.0e-7f,
            "New audio clip did not start with the headroom-preserving gain");

    ClipEdits edits = clip->edits;
    edits.gain = 0.5f;
    edits.gainLinear = 0.5f;
    edits.pan = -1.0f;
    edits.fadeInBeats = 0.5f;
    edits.fadeOutBeats = 0.5f;
    require(tracks->getPlaylistModel().setClipEdits(clipId, edits), "Clip edits were not accepted");

    const auto output = render(tracks);
    const auto leftAt = [&output](uint32_t frame) { return output[static_cast<size_t>(frame) * 2]; };
    const auto rightAt = [&output](uint32_t frame) { return output[static_cast<size_t>(frame) * 2 + 1]; };

    require(std::abs(leftAt(0)) < 1.0e-6f, "Fade-in did not begin at silence");
    require(leftAt(6000) > 0.20f && leftAt(6000) < 0.30f, "Fade-in did not shape the rendered clip");
    require(leftAt(18000) > 0.49f && leftAt(18000) < 0.51f, "Clip gain did not reach its steady-state level");
    require(std::abs(rightAt(18000)) < 1.0e-6f, "Hard-left clip pan leaked into the right channel");
    require(leftAt(42000) > 0.20f && leftAt(42000) < 0.30f, "Fade-out did not shape the rendered clip");
    require(std::abs(rightAt(42000)) < 1.0e-6f, "Clip pan changed during fade-out");

    // Playback speed and source-start are instance edits. They must reach the
    // same graph used by live playback and offline export.
    for (uint32_t frame = 0; frame < kTotalFrames; ++frame) {
        sourceBuffer->interleavedData[frame] = static_cast<float>(frame) / static_cast<float>(kTotalFrames);
    }
    edits.gain = 1.0f;
    edits.gainLinear = 1.0f;
    edits.pan = 0.0f;
    edits.fadeInBeats = 0.0f;
    edits.fadeOutBeats = 0.0f;
    edits.playbackRate = 2.0f;
    edits.sourceStart = kSampleRate / 4.0;
    require(tracks->getPlaylistModel().setClipEdits(clipId, edits), "Playback edits were not accepted");

    const auto shiftedOutput = render(tracks);
    const auto shiftedLeftAt = [&shiftedOutput](uint32_t frame) {
        return shiftedOutput[static_cast<size_t>(frame) * 2];
    };
    require(shiftedLeftAt(6000) > 0.49f && shiftedLeftAt(6000) < 0.51f,
            "Playback speed or source-start did not reach the renderer");
    require(shiftedLeftAt(12000) > 0.74f && shiftedLeftAt(12000) < 0.76f,
            "Playback rate did not advance through the source at the requested speed");
    require(std::abs(shiftedLeftAt(18000)) < 1.0e-6f,
            "Playback continued after the rate-adjusted source region was exhausted");

    std::cout << "[PASS] AudioClipInstanceRenderTest\n";
    return 0;
}
