// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"
#include "Models/PatternSource.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace Aestra::Audio;

namespace {
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockSize = 256;
constexpr double kBpm = 120.0;
constexpr double kRenderSeconds = 2.0;
constexpr double kRenderBeats = 4.0;
constexpr double kTau = 6.28318530717958647692;

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1;
    uint32_t sampleRate = kSampleRate;
    uint32_t byteRate = kSampleRate * 2;
    uint16_t blockAlign = 2;
    uint16_t bitsPerSample = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

bool writeMonoToneWav(const std::filesystem::path& path, double frequencyHz, float amplitude, double seconds) {
    if (seconds <= 0.0) {
        return false;
    }
    const uint32_t frames = static_cast<uint32_t>(seconds * static_cast<double>(kSampleRate));
    if (frames == 0) {
        return false;
    }

    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const float s = std::sin(kTau * frequencyHz * t) * amplitude;
        pcm[i] = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
    }

    WavHeader header;
    header.dataSize = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    header.fileSize = sizeof(WavHeader) - 8 + header.dataSize;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(pcm.data()), static_cast<std::streamsize>(header.dataSize));
    return out.good();
}

float peakOf(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) {
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

struct ScenarioResult {
    float livePeak = 0.0f;
    float exportPeak = 0.0f;
};

ScenarioResult runScenario(const std::filesystem::path& tempRoot,
                           const char* name,
                           bool routeToTrack,
                           float trackVolume) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID laneId = playlist.createLane("Track 1");
    require(laneId.isValid(), "Failed to create lane for parity scenario");
    auto* channel = tm->addChannel("Track 1");
    require(channel != nullptr, "Failed to create channel for parity scenario");
    if (auto* lane = playlist.getLane(laneId)) {
        lane->volume = trackVolume;
        lane->pan = 0.0f;
    }
    channel->setVolume(trackVolume);
    channel->setPan(0.0f);

    const std::filesystem::path samplePath = tempRoot / (std::string(name) + "_sample.wav");
    require(writeMonoToneWav(samplePath, 220.0, 0.55f, 1.0), "Failed to create sample wav for parity scenario");

    auto& unitManager = tm->getUnitManager();
    const UnitID unitId = unitManager.createUnit(std::string("Arsenal ") + name, UnitType::Sampler);
    unitManager.setUnitAudioClip(unitId, samplePath.string());
    unitManager.setUnitEnabled(unitId, true);
    if (routeToTrack) {
        unitManager.assignUnitToTimelineLane(unitId, 0);
    } else {
        unitManager.clearUnitTimelineLane(unitId);
    }

    auto& patternManager = tm->getPatternManager();
    PatternID patternId = patternManager.createPattern();
    auto* pattern = patternManager.getPattern(patternId);
    require(pattern != nullptr, "Failed to create MIDI pattern for parity scenario");
    pattern->type = PatternSource::Type::Midi;
    pattern->name = std::string("Parity Pattern ") + name;
    pattern->lengthBeats = kRenderBeats;
    pattern->payload = MidiPayload{};
    auto& notes = std::get<MidiPayload>(pattern->payload).notes;
    notes.push_back(MidiNote{60, 0.0, 0.75, 120.0f, unitId});
    notes.push_back(MidiNote{64, 1.0, 0.75, 110.0f, unitId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm, static_cast<double>(kSampleRate)));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    tm->getPatternPlaybackEngine().flush();
    tm->getPatternPlaybackEngine().schedulePatternInstance(patternId, 0.0, 1);
    engine.setTransportPlaying(true);

    const uint32_t totalFrames = static_cast<uint32_t>(kRenderSeconds * static_cast<double>(kSampleRate));
    std::vector<float> liveBlock(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    std::vector<float> liveSamples;
    liveSamples.reserve(static_cast<size_t>(totalFrames) * kChannels);

    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        const uint32_t framesThisBlock = std::min(kBlockSize, totalFrames - rendered);
        std::fill(liveBlock.begin(), liveBlock.end(), 0.0f);
        engine.processBlock(liveBlock.data(), nullptr, framesThisBlock, 0.0);
        liveSamples.insert(liveSamples.end(), liveBlock.begin(),
                           liveBlock.begin() + static_cast<std::ptrdiff_t>(framesThisBlock * kChannels));
        rendered += framesThisBlock;
    }
    engine.setTransportPlaying(false);

    const std::filesystem::path exportPath = tempRoot / (std::string(name) + "_export.wav");
    std::error_code ec;
    std::filesystem::remove(exportPath, ec);

    AudioExporter exporter(engine, *tm);
    AudioExporter::Config config;
    config.outputPath = exportPath.string();
    config.sampleRate = kSampleRate;
    config.numChannels = kChannels;
    config.bitDepth = AudioExporter::BitDepth::Float_32;
    config.scope = AudioExporter::RenderScope::Selection;
    config.startTimeSeconds = 0.0;
    config.endTimeSeconds = kRenderSeconds;
    config.tailSeconds = 0.0;
    const auto exportResult = exporter.render(config);
    require(exportResult.success, "AudioExporter render failed in parity scenario");

    std::vector<float> exportedSamples;
    uint32_t exportedRate = 0;
    uint32_t exportedChannels = 0;
    require(decodeAudioFile(exportPath.string(), exportedSamples, exportedRate, exportedChannels),
            "Failed to decode exported wav in parity scenario");
    require(exportedRate == kSampleRate, "Unexpected exported sample rate in parity scenario");
    require(exportedChannels == kChannels, "Unexpected exported channel count in parity scenario");

    ScenarioResult result;
    result.livePeak = peakOf(liveSamples);
    result.exportPeak = peakOf(exportedSamples);
    return result;
}
} // namespace

int main() {
    auto& pluginManager = PluginManager::getInstance();
    require(pluginManager.initialize(), "PluginManager initialize failed");

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "aestra_arsenal_parity_phase3";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);

    // Case 1: Preview route remains audible in live and export even when track path is muted.
    const ScenarioResult previewMutedTrack = runScenario(tempRoot, "preview_muted_track", false, 0.0f);
    require(previewMutedTrack.livePeak > 1.0e-4f, "PreviewToMaster should be audible in live output");
    require(previewMutedTrack.exportPeak > 1.0e-4f, "PreviewToMaster should currently participate in export");

    // Case 2: Current live/export authority path still renders track-routed Arsenal
    // in processBlock even when track volume is muted. This guards current behavior
    // until routing policy is intentionally changed.
    const ScenarioResult routedMutedTrack = runScenario(tempRoot, "routed_muted_track", true, 0.0f);
    require(routedMutedTrack.livePeak > 1.0e-4f,
            "Current policy regression: track-routed Arsenal became silent in muted-track live path");
    require(routedMutedTrack.exportPeak > 1.0e-4f,
            "Current policy regression: track-routed Arsenal became silent in muted-track export path");

    // Case 3: Track-routed path is audible when track path is open.
    const ScenarioResult routedOpenTrack = runScenario(tempRoot, "routed_open_track", true, 1.0f);
    require(routedOpenTrack.livePeak > 1.0e-4f, "Track-routed Arsenal should be audible in live output when track is open");
    require(routedOpenTrack.exportPeak > 1.0e-4f, "Track-routed Arsenal should be audible in export when track is open");

    std::cout << "[PASS] ArsenalExportLiveParityTest\n";
    return 0;
}
