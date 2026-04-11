// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// OfflineRenderRegressionTest — Validates exporter parity against the engine bounce path.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"
#include "Models/TrackManager.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/PluginFactory.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr double kBpm = 120.0;
constexpr double kDurationSeconds = 2.0;
constexpr double kDurationBeats = 4.0;
constexpr float kAmplitude = 0.3f;
constexpr double kFrequencyHz = 440.0;
constexpr double kTau = 6.28318530717958647692;

struct DecodedAudio {
    std::vector<float> samples;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
};

enum class FixtureMode {
    Baseline,
    AudibleSend,
    SidechainComp
};

std::shared_ptr<AudioBufferData> makeToneBuffer(double frequencyHz = kFrequencyHz,
                                                float amplitude = kAmplitude) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kSampleRate;
    buffer->numChannels = kChannels;
    buffer->numFrames = static_cast<uint32_t>(kSampleRate * kDurationSeconds);
    buffer->interleavedData.resize(static_cast<size_t>(buffer->numFrames) * kChannels, 0.0f);

    for (uint32_t frame = 0; frame < buffer->numFrames; ++frame) {
        const double t = static_cast<double>(frame) / static_cast<double>(kSampleRate);
        const float sample = static_cast<float>(std::sin(kTau * frequencyHz * t) * amplitude);
        buffer->interleavedData[static_cast<size_t>(frame) * 2] = sample;
        buffer->interleavedData[static_cast<size_t>(frame) * 2 + 1] = sample;
    }
    return buffer;
}

bool addAudioClipToLane(TrackManager& trackManager,
                        const std::filesystem::path& tempRoot,
                        const std::string& stem,
                        PlaylistLaneID laneId,
                        double frequencyHz,
                        float amplitude = kAmplitude) {
    auto buffer = makeToneBuffer(frequencyHz, amplitude);
    const auto sourcePath = (tempRoot / (stem + ".wav")).string();
    const ClipSourceID sourceId = trackManager.getSourceManager().createRecordedSource(
        sourcePath, stem, buffer);
    if (!sourceId.isValid()) {
        return false;
    }

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = kDurationSeconds;
    payload.slices.push_back({0.0, kDurationSeconds, 0.0, static_cast<double>(buffer->numFrames)});

    const PatternID patternId =
        trackManager.getPatternManager().createAudioPattern(stem, kDurationBeats, payload);
    if (!patternId.isValid()) {
        return false;
    }

    const ClipInstanceID clipId =
        trackManager.getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, kDurationBeats);
    return clipId.isValid();
}

bool insertBuiltInComp(MixerChannel& channel) {
    InProcessPluginFactory factory;
    PluginInstancePtr plugin;
    factory.createPluginAsync(BuiltInPlugins::compInfo(), [&](PluginInstancePtr instance) {
        plugin = std::move(instance);
    });
    if (!plugin) {
        return false;
    }
    return channel.getEffectChain().insertPlugin(0, plugin);
}

std::shared_ptr<TrackManager> makeTrackManagerFixture(const std::filesystem::path& tempRoot,
                                                      FixtureMode mode) {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    trackManager->getPlaylistModel().setBPM(kBpm);

    if (mode == FixtureMode::Baseline) {
        trackManager->addChannel("Parity Track");
        const PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane("Parity Track");
        if (!addAudioClipToLane(*trackManager, tempRoot, "offline_export_parity_source", laneId, kFrequencyHz)) {
            return nullptr;
        }
        return trackManager;
    }

    auto* track1 = trackManager->addChannel("Track 1");
    auto* track2 = trackManager->addChannel("Track 2");
    if (!track1 || !track2) {
        return nullptr;
    }

    const PlaylistLaneID lane1 = trackManager->getPlaylistModel().createLane("Track 1");
    const PlaylistLaneID lane2 = trackManager->getPlaylistModel().createLane("Track 2");

    if (!addAudioClipToLane(*trackManager, tempRoot, "offline_export_track1", lane1, 220.0, 0.32f)) {
        return nullptr;
    }

    if (mode == FixtureMode::SidechainComp) {
        if (!addAudioClipToLane(*trackManager, tempRoot, "offline_export_track2", lane2, 110.0, 0.18f)) {
            return nullptr;
        }
        if (!insertBuiltInComp(*track2)) {
            return nullptr;
        }
    }

    AudioRoute route{};
    route.targetChannelId = track2->getChannelId();
    route.gain = 1.0f;
    route.postFader = true;
    route.sidechainOnly = (mode == FixtureMode::SidechainComp);
    track1->addSend(route);

    return trackManager;
}

bool decodeWav(const std::filesystem::path& path, DecodedAudio& out) {
    return decodeAudioFile(path.string(), out.samples, out.sampleRate, out.channels);
}

void configureEngineFixture(AudioEngine& engine, const std::shared_ptr<TrackManager>& trackManager) {
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(512, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager, static_cast<double>(kSampleRate)));
    engine.initialize();
}

std::vector<float> renderReferenceBuffer(AudioEngine& engine) {
    const uint32_t totalFrames = static_cast<uint32_t>(kSampleRate * kDurationSeconds);
    const uint32_t blockFrames = 512;
    std::vector<float> output;
    output.reserve(static_cast<size_t>(totalFrames) * kChannels);
    std::vector<float> block(static_cast<size_t>(blockFrames) * kChannels, 0.0f);

    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    uint32_t renderedFrames = 0;
    while (renderedFrames < totalFrames) {
        const uint32_t framesThisBlock = std::min(blockFrames, totalFrames - renderedFrames);
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, framesThisBlock, 0.0);
        output.insert(output.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(framesThisBlock * kChannels));
        renderedFrames += framesThisBlock;
    }

    engine.setTransportPlaying(false);
    return output;
}

float calculatePeak(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample));
    }
    return peak;
}

double calculateCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    long double meanA = 0.0;
    long double meanB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        meanA += a[i];
        meanB += b[i];
    }
    meanA /= static_cast<long double>(a.size());
    meanB /= static_cast<long double>(b.size());

    long double numerator = 0.0;
    long double denomA = 0.0;
    long double denomB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const long double da = static_cast<long double>(a[i]) - meanA;
        const long double db = static_cast<long double>(b[i]) - meanB;
        numerator += da * db;
        denomA += da * da;
        denomB += db * db;
    }

    if (denomA <= 0.0 || denomB <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(numerator / std::sqrt(denomA * denomB));
}

double rmsDifferenceDb(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    long double sumSquares = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const long double diff = static_cast<long double>(a[i]) - static_cast<long double>(b[i]);
        sumSquares += diff * diff;
    }

    const long double rms = std::sqrt(sumSquares / static_cast<long double>(a.size()));
    return 20.0 * std::log10(static_cast<double>(rms) + 1e-12);
}

bool runExportParityTest(FixtureMode mode, const char* label) {
    namespace fs = std::filesystem;
    const fs::path tempRoot = fs::temp_directory_path() / "aestra_offline_export_parity";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);

    auto exportTrackManager = makeTrackManagerFixture(tempRoot, mode);
    auto referenceTrackManager = makeTrackManagerFixture(tempRoot, mode);
    if (!exportTrackManager || !referenceTrackManager) {
        std::cerr << "Failed to create in-memory export fixture.\n";
        return false;
    }

    AudioEngine exportEngine;
    configureEngineFixture(exportEngine, exportTrackManager);
    AudioEngine referenceEngine;
    configureEngineFixture(referenceEngine, referenceTrackManager);

    const fs::path exportPath = tempRoot / (std::string("offline_export_") + label + ".wav");
    fs::remove(exportPath, ec);

    AudioExporter exporter(exportEngine, *exportTrackManager);
    AudioExporter::Config config;
    config.outputPath = exportPath.string();
    config.sampleRate = kSampleRate;
    config.numChannels = kChannels;
    config.bitDepth = AudioExporter::BitDepth::Float_32;
    config.scope = AudioExporter::RenderScope::FullSong;
    config.tailSeconds = 0.0;

    const auto exportResult = exporter.render(config);
    if (!exportResult.success) {
        std::cerr << "Exporter failed: " << exportResult.errorMessage << "\n";
        return false;
    }

    const std::vector<float> referenceSamples = renderReferenceBuffer(referenceEngine);

    DecodedAudio exported;
    if (!decodeWav(exportPath, exported)) {
        std::cerr << "Failed to decode exporter WAV.\n";
        return false;
    }

    if (exported.sampleRate != kSampleRate || exported.channels != kChannels) {
        std::cerr << "Unexpected output format.\n";
        return false;
    }

    if (exported.samples.empty() || referenceSamples.empty()) {
        std::cerr << "Rendered audio is empty.\n";
        return false;
    }

    const float exportPeak = calculatePeak(exported.samples);
    const float referencePeak = calculatePeak(referenceSamples);
    if (exportPeak < 0.05f || referencePeak < 0.05f) {
        std::cerr << "Rendered audio peak too low. exportPeak=" << exportPeak
                  << " referencePeak=" << referencePeak << "\n";
        return false;
    }

    if (exported.samples.size() != referenceSamples.size()) {
        std::cerr << "Sample count mismatch. export=" << exported.samples.size()
                  << " reference=" << referenceSamples.size() << "\n";
        return false;
    }

    const double correlation = calculateCorrelation(exported.samples, referenceSamples);
    const double diffDb = rmsDifferenceDb(exported.samples, referenceSamples);
    if (correlation < 0.995 || diffDb > -35.0) {
        std::cerr << "Exporter diverged from reference render path. correlation=" << correlation
                  << " rmsDiffDb=" << diffDb << "\n";
        return false;
    }

    std::cout << "Offline export parity [" << label << "] OK. peak=" << exportPeak
              << " correlation=" << correlation
              << " rmsDiffDb=" << diffDb << "\n";
    return true;
}

} // namespace

int main() {
    const bool baselineOk = runExportParityTest(FixtureMode::Baseline, "baseline");
    const bool sendOk = runExportParityTest(FixtureMode::AudibleSend, "audible_send");
    const bool sidechainOk = runExportParityTest(FixtureMode::SidechainComp, "sidechain_comp");
    return (baselineOk && sendOk && sidechainOk) ? 0 : 1;
}
