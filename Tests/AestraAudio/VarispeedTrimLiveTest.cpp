// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// VarispeedTrimLiveTest — #865. A recorded source played at 2x and trimmed to 1 beat must consume span x speed of the
// source on the live path: 0.5 s x 2.0 = 1.0 s. So the output is the ramp rising at 2/48000 per frame up to frame
// 24000, then silence. The issue saw the source consumed at about 1.125x per frame with a wrap at 24000.

#include "Commands/CommandHistory.h"
#include "Commands/TrimClipCommand.h"
#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace Aestra::Audio;
constexpr uint32_t kRate = 48000, kBlock = 256, kChannels = 2, kFrames = kRate;
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << (cond ? "PASS: " : "FAIL: ") << what << "\n";
    if (!cond) ++g_failures;
}
} // namespace

int main() {
    auto tracks = std::make_shared<TrackManager>();
    tracks->setOutputSampleRate(kRate);
    tracks->getPlaylistModel().setBPM(120.0);

    // 1 s mono ramp: the value tells which source frame played (value = frame / 48000).
    auto source = std::make_shared<AudioBufferData>();
    source->sampleRate = kRate;
    source->numChannels = 1;
    source->numFrames = kFrames;
    source->interleavedData.resize(kFrames);
    for (uint32_t f = 0; f < kFrames; ++f) source->interleavedData[f] = static_cast<float>(f) / kFrames;
    const ClipSourceID sourceId = tracks->getSourceManager().createRecordedSource("ramp.wav", "Ramp", source);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = 1.0;
    payload.slices.push_back({0.0, 1.0, 0.0, static_cast<double>(kFrames)});
    const PatternID patternId = tracks->getPatternManager().createAudioPattern("Ramp", 2.0, payload);
    const PlaylistLaneID lane = tracks->getPlaylistModel().createLane("Lane");
    const ClipInstanceID clipId = tracks->getPlaylistModel().addClipFromPattern(lane, patternId, 0.0, 2.0);

    ClipEdits edits = tracks->getPlaylistModel().getClip(clipId)->edits;
    edits.playbackRate = 2.0f;
    edits.gainLinear = 1.0f;
    edits.pan = -1.0f; // hard left: unity gain on the left channel, so values compare exactly
    edits.fadeInBeats = 0.0f;
    edits.fadeOutBeats = 0.0f;
    tracks->getPlaylistModel().setClipEdits(clipId, edits);
    tracks->getCommandHistory().pushAndExecute(
        std::make_shared<TrimClipCommand>(tracks->getPlaylistModel(), clipId, 0.0, 1.0)); // the UI's trim, to 1 beat

    AudioEngine engine;
    engine.setSampleRate(kRate);
    engine.setBufferConfig(kBlock, kChannels);
    engine.setTrackManager(tracks);
    engine.setBPM(120.0f);
    engine.setSafetyLimiterEnabled(false);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tracks));
    engine.initialize();
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);
    std::vector<float> out(static_cast<size_t>(kFrames) * kChannels, 0.0f), block(kBlock * kChannels);
    for (uint32_t done = 0; done < kFrames; done += kBlock) {
        const uint32_t n = std::min(kBlock, kFrames - done);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, n, 0.0);
        std::copy_n(block.begin(), n * kChannels, out.begin() + static_cast<ptrdiff_t>(done) * kChannels);
    }
    const auto left = [&](uint32_t f) { return out[static_cast<size_t>(f) * kChannels]; };

    uint32_t lastNonZero = 0;
    for (uint32_t f = 0; f < kFrames; ++f)
        if (std::abs(left(f)) > 1.0e-6f) lastNonZero = f;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "left(6000)=%.4f left(12000)=%.4f left(23000)=%.4f last non-zero frame %u",
                  left(6000), left(12000), left(23000), lastNonZero);
    std::cout << buf << "\n";
    check(std::abs(left(6000) - 0.25f) < 0.01f && std::abs(left(12000) - 0.5f) < 0.01f,
          "the source is consumed at 2x: 0.25 at frame 6000, 0.5 at frame 12000");
    check(left(23000) > 0.94f, "it reaches the end of the source near frame 24000, without wrapping");
    check(lastNonZero >= 23900 && lastNonZero <= 24100, "and stops at the trimmed end, frame 24000");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
