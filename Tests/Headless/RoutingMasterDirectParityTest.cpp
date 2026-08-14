// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RoutingMasterDirectParityTest — regression for the routing/gain parity bug:
// the same source must sound equally loud whether its clip is routed directly
// to Master (mixerChannelId 0, the masterClips path) or through a normal mixer
// channel strip at unity controls.
//
// Root cause fixed here: the channel strip applied PanLaw::equalPower at
// centre (-3.01 dB per leg) to already-stereo content while the direct-to-
// Master path applied no strip pan law at all, so channel-routed music was
// exactly 0.7071x (-3.0103 dB) quieter than Master-routed music. The strip
// now uses the stereo-balance law (unity at centre).
//
// Both arms render through the real RT path (AudioEngine::processBlock) with
// the same deterministic stereo tone, unity fader, centre pan, no automation,
// no sends, and no plugins. The only difference is the clip's routing.

#include "../AestraAudio/GoldenAudio/GoldenAudioHarness.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"
#include "Models/PatternManager.h"
#include "Models/PlaylistModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSeconds = 2;
constexpr double kTau = 6.28318530717958647692;
constexpr double kFreq = 997.0; // odd frequency: no harmonic coincidence with block sizes
constexpr float kAmp = 0.25f;

int g_failures = 0;

#define EXPECT_TRUE(expr)                                                                 \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            reportFailure(#expr, std::string(__FILE__) + ":" + std::to_string(__LINE__)); \
        }                                                                                 \
    } while (0)

void reportFailure(const char* what, const std::string& detail) {
    std::fprintf(stderr, "[FAIL] %s — %s\n", what, detail.c_str());
    ++g_failures;
}

// Deterministic stereo tone: identical L/R content so any per-leg gain error
// shows up on both channels.
std::vector<float> makeTone(uint32_t frames, uint32_t sampleRate) {
    std::vector<float> s(static_cast<size_t>(frames) * 2, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * kFreq * static_cast<double>(i) / sampleRate)) * kAmp;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

// Build a session with one clip whose pattern is routed to `mixerChannelId`
// (0 = Master -> masterClips path; >= 1 = the given channel's strip).
std::shared_ptr<TrackManager> buildSession(uint32_t mixerChannelId, const std::vector<float>& tone,
                                           uint32_t frames, const GoldenAudio::SessionConfig& cfg) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));

    if (mixerChannelId != 0) {
        tm->addChannel("RouteChannel");
    } else {
        // Keep a non-trivial graph (one unused channel) so both arms exercise
        // the same engine paths; only the clip's routing differs.
        tm->addChannel("Unused");
    }

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = cfg.sampleRate;
    buffer->numChannels = cfg.channels;
    buffer->numFrames = frames;
    buffer->interleavedData = tone;

    const std::string path = (std::filesystem::temp_directory_path() /
                              ("master_direct_parity_" + std::to_string(mixerChannelId) + ".wav")).string();
    ClipSourceID sourceId = tm->getSourceManager().createRecordedSource(path, "parity", buffer);

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = static_cast<double>(frames) / cfg.sampleRate;
    payload.slices.push_back({0.0, payload.durationSeconds, 0.0, static_cast<double>(frames)});

    PlaylistLaneID laneId = tm->getPlaylistModel().createLane("Track");
    const double durationBeats = payload.durationSeconds * (static_cast<double>(cfg.bpm) / 60.0);
    PatternID patternId = tm->getPatternManager().createAudioPattern("parity", durationBeats, payload);
    tm->getPatternManager().setPatternMixerChannel(patternId, mixerChannelId);
    const ClipInstanceID clipId = tm->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationBeats);
    if (!tm->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
        throw std::runtime_error("Failed to configure unity-gain parity clip");
    }
    return tm;
}

std::vector<float> render(const std::shared_ptr<TrackManager>& tm, const GoldenAudio::SessionConfig& cfg) {
    AudioEngine engine;
    GoldenAudio::prepareEngine(engine, tm, cfg);
    engine.setTransportPlaying(true);
    const uint64_t totalFrames = static_cast<uint64_t>(cfg.sampleRate) * kSeconds;
    std::vector<float> out = GoldenAudio::renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);
    return out;
}

double rmsRange(const std::vector<float>& interleaved, size_t firstFrame, size_t endFrame, uint32_t channels) {
    if (channels == 0 || firstFrame >= endFrame || endFrame * channels > interleaved.size()) {
        return 0.0;
    }
    double sumSq = 0.0;
    const size_t firstSample = firstFrame * channels;
    const size_t endSample = endFrame * channels;
    for (size_t i = firstSample; i < endSample; ++i) {
        sumSq += static_cast<double>(interleaved[i]) * static_cast<double>(interleaved[i]);
    }
    return std::sqrt(sumSq / static_cast<double>(endSample - firstSample));
}

// Isolated-bounce contract, live half (2026-08-14): solo determines which
// track content is audible in the live mix, and the master stage obeys the
// active solo gate like master-routed units. Master clips have no soloable
// owner (mixerChannelId 0 = Master, never soloed), so ANY active solo
// silences them. The bounce half (master stage excluded from isolated-track
// bounce) is pinned in RealtimeExportParityTest.
void testMasterClipsRespectSolo() {
    std::printf("[RoutingMasterDirectParityTest] master-routed clips obey the live solo gate...\n");
    GoldenAudio::SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;
    const std::vector<float> tone = makeTone(totalFrames, cfg.sampleRate);

    // Baseline: no solo -> the master clip is audible.
    const std::vector<float> noSolo = render(buildSession(0, tone, totalFrames, cfg), cfg);
    const double noSoloRms = rmsRange(noSolo, static_cast<size_t>(cfg.blockSize) * 4,
                                      static_cast<size_t>(totalFrames) - 256, cfg.channels);
    EXPECT_TRUE(noSoloRms > 1e-4);
    if (noSoloRms <= 1e-4) {
        return;
    }

    // Another track soloed -> the master clip must be silent (the soloed
    // track has no clips, so the whole output is silent).
    auto soloSession = buildSession(0, tone, totalFrames, cfg);
    auto* soloChannel = soloSession->getChannel(0);
    EXPECT_TRUE(soloChannel != nullptr);
    if (!soloChannel) {
        return;
    }
    soloChannel->setSolo(true);
    const std::vector<float> soloOut = render(soloSession, cfg);
    const double soloRms = rmsRange(soloOut, static_cast<size_t>(cfg.blockSize) * 4,
                                    static_cast<size_t>(totalFrames) - 256, cfg.channels);
    EXPECT_TRUE(soloRms < 1e-5);
    if (soloRms >= 1e-5) {
        return;
    }

    std::printf("[RoutingMasterDirectParityTest] PASS: no-solo rms=%.6f, soloed rms=%.6f\n", noSoloRms, soloRms);
}

void testMasterDirectMatchesChannelRouted() {
    std::printf("[RoutingMasterDirectParityTest] master-direct clip == channel-routed clip at unity...\n");
    GoldenAudio::SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;
    const std::vector<float> tone = makeTone(totalFrames, cfg.sampleRate);

    const std::vector<float> masterArm = render(buildSession(0, tone, totalFrames, cfg), cfg);
    const std::vector<float> channelArm = render(buildSession(1, tone, totalFrames, cfg), cfg);

    EXPECT_TRUE(masterArm.size() == channelArm.size());
    if (masterArm.size() != channelArm.size()) {
        return;
    }

    // Trim the transport fade-in and the clip-edge fade out of the comparison.
    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4;
    const size_t trimEnd = static_cast<size_t>(256);
    const size_t first = trimStart;
    const size_t last = totalFrames - trimEnd;

    const double masterRms = rmsRange(masterArm, first, last, cfg.channels);
    const double channelRms = rmsRange(channelArm, first, last, cfg.channels);

    if (masterRms <= 1e-5 || channelRms <= 1e-5) {
        reportFailure("both arms audible", "master RMS " + std::to_string(masterRms) + ", channel RMS " +
                                               std::to_string(channelRms));
        return;
    }

    // Unity controls must give unity ratio. 1% ratio tolerance ~= 0.086 dB;
    // the old bug was exactly -3.0103 dB (ratio 0.7071), which fails by 40x.
    const double ratio = channelRms / masterRms;
    if (ratio < 0.999 || ratio > 1.001) {
        reportFailure("channel/master RMS parity", "ratio " + std::to_string(ratio) + " (expected ~1.0; old bug was 0.7071)");
        return;
    }

    // Pin the absolute level too: the channel arm must reach the tone
    // amplitude (unity at centre), not amplitude * 0.7071.
    double channelPeak = 0.0;
    for (size_t i = first * cfg.channels; i < last * cfg.channels; ++i) {
        channelPeak = std::max(channelPeak, std::abs(static_cast<double>(channelArm[i])));
    }
    if (channelPeak < kAmp * 0.999 || channelPeak > kAmp * 1.001) {
        reportFailure("channel arm centre gain is unity",
                      "peak " + std::to_string(channelPeak) + " (expected " + std::to_string(kAmp) + ")");
        return;
    }

    std::printf("[RoutingMasterDirectParityTest] PASS: master RMS %.6f, channel RMS %.6f, ratio %.6f, peak %.6f\n",
                masterRms, channelRms, ratio, channelPeak);
}

} // namespace

int main() {
    std::printf("=== RoutingMasterDirectParityTest (routing/gain BUG-3 triage 2026-08-14) ===\n");
    try {
        testMasterDirectMatchesChannelRouted();
        testMasterClipsRespectSolo();
    } catch (const std::exception& e) {
        reportFailure("fixture setup", e.what());
    }

    if (g_failures == 0) {
        std::printf("=== RoutingMasterDirectParityTest: all checks passed ===\n");
        return EXIT_SUCCESS;
    }
    std::fprintf(stderr, "=== RoutingMasterDirectParityTest: %d failure(s) ===\n", g_failures);
    return EXIT_FAILURE;
}
