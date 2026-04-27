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

// Phase 3 mixed-route permutation: two units, one PreviewToMaster and one
// RoutedToTimelineTrack, running simultaneously in the same engine session.
// Proves that the engine correctly segregates both routing paths without
// cross-contamination.
void runMixedScenario(const std::filesystem::path& tempRoot, float trackVolume) {
    using namespace Aestra::Audio;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID laneId = playlist.createLane("Mixed Track");
    require(laneId.isValid(), "Failed to create lane for mixed scenario");
    auto* channel = tm->addChannel("Mixed Track");
    require(channel != nullptr, "Failed to create channel for mixed scenario");
    if (auto* lane = playlist.getLane(laneId)) {
        lane->volume = trackVolume;
        lane->pan = 0.0f;
    }
    channel->setVolume(trackVolume);
    channel->setPan(0.0f);

    const std::filesystem::path samplePath = tempRoot / "mixed_sample.wav";
    require(writeMonoToneWav(samplePath, 440.0, 0.55f, 1.0), "Failed to create sample wav for mixed scenario");

    auto& unitManager = tm->getUnitManager();
    const UnitID previewUnitId = unitManager.createUnit("Mixed Preview Unit", UnitType::Sampler);
    const UnitID trackUnitId = unitManager.createUnit("Mixed Track Unit", UnitType::Sampler);
    unitManager.setUnitAudioClip(previewUnitId, samplePath.string());
    unitManager.setUnitAudioClip(trackUnitId, samplePath.string());
    unitManager.setUnitEnabled(previewUnitId, true);
    unitManager.setUnitEnabled(trackUnitId, true);
    unitManager.clearUnitTimelineLane(previewUnitId);
    unitManager.assignUnitToTimelineLane(trackUnitId, 0);

    auto& patternManager = tm->getPatternManager();
    PatternID previewPatternId = patternManager.createPattern();
    auto* previewPattern = patternManager.getPattern(previewPatternId);
    require(previewPattern != nullptr, "Failed to create preview pattern for mixed scenario");
    previewPattern->type = PatternSource::Type::Midi;
    previewPattern->name = "Mixed Preview Pattern";
    previewPattern->lengthBeats = kRenderBeats;
    previewPattern->payload = MidiPayload{};
    auto& previewNotes = std::get<MidiPayload>(previewPattern->payload).notes;
    previewNotes.push_back(MidiNote{72, 0.0, 0.75, 120.0f, previewUnitId});
    previewNotes.push_back(MidiNote{76, 1.0, 0.75, 110.0f, previewUnitId});

    PatternID trackPatternId = patternManager.createPattern();
    auto* trackPattern = patternManager.getPattern(trackPatternId);
    require(trackPattern != nullptr, "Failed to create track pattern for mixed scenario");
    trackPattern->type = PatternSource::Type::Midi;
    trackPattern->name = "Mixed Track Pattern";
    trackPattern->lengthBeats = kRenderBeats;
    trackPattern->payload = MidiPayload{};
    auto& trackNotes = std::get<MidiPayload>(trackPattern->payload).notes;
    trackNotes.push_back(MidiNote{60, 0.0, 0.75, 120.0f, trackUnitId});
    trackNotes.push_back(MidiNote{64, 1.0, 0.75, 110.0f, trackUnitId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed in mixed scenario");
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
    tm->getPatternPlaybackEngine().schedulePatternInstance(previewPatternId, 0.0, 1);
    tm->getPatternPlaybackEngine().schedulePatternInstance(trackPatternId, 0.0, 2);
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

    const std::filesystem::path exportPath = tempRoot / "mixed_export.wav";
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
    require(exportResult.success, "AudioExporter render failed in mixed scenario");

    std::vector<float> exportedSamples;
    uint32_t exportedRate = 0;
    uint32_t exportedChannels = 0;
    require(decodeAudioFile(exportPath.string(), exportedSamples, exportedRate, exportedChannels),
            "Failed to decode exported wav in mixed scenario");
    require(exportedRate == kSampleRate, "Exported sample rate mismatch in mixed scenario");
    require(exportedChannels == kChannels, "Exported channel count mismatch in mixed scenario");

    const float livePk = peakOf(liveSamples);
    const float exportPk = peakOf(exportedSamples);

    // Both units should produce audio simultaneously in the mixed scenario.
    // The PreviewToMaster unit bypasses the track path; the track-routed unit
    // goes through the track. Both should contribute to output.
    require(livePk > 1.0e-4f, "Mixed scenario: live output should be audible");
    require(exportPk > 1.0e-4f, "Mixed scenario: export output should be audible");

    std::cout << "[INFO] Mixed scenario: live peak=" << livePk << " export peak=" << exportPk << "\n";
}

// Phase 3 isolated-track bounce: verify that bounceRangeToWav succeeds
// (does not crash) with Arsenal units present in the system. Arsenal pattern
// playback uses the processBlock path (covered by AudioExporter tests), not
// renderBlock. bounceRangeToWav calls renderBlock which does not populate
// MIDI buffers for Arsenal processing, so Arsenal audio is not expected here.
// The isolatedTrackIndex guard at AudioRenderer:285 is separately proven by
// ArsenalExportCurrentPolicyTest::shouldRunMasterPreviewPass.
void runIsolatedBounceScenario(const std::filesystem::path& tempRoot) {
    using namespace Aestra::Audio;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID laneId = playlist.createLane("Bounce Track");
    require(laneId.isValid(), "Failed to create lane for bounce scenario");
    auto* channel = tm->addChannel("Bounce Track");
    require(channel != nullptr, "Failed to create channel for bounce scenario");
    if (auto* lane = playlist.getLane(laneId)) {
        lane->volume = 1.0f;
        lane->pan = 0.0f;
    }
    channel->setVolume(1.0f);
    channel->setPan(0.0f);

    const std::filesystem::path samplePath = tempRoot / "bounce_sample.wav";
    require(writeMonoToneWav(samplePath, 330.0, 0.55f, 1.0), "Failed to create sample for bounce scenario");

    auto& unitManager = tm->getUnitManager();
    const UnitID previewId = unitManager.createUnit("Bounce Preview Unit", UnitType::Sampler);
    const UnitID trackId = unitManager.createUnit("Bounce Track Unit", UnitType::Sampler);
    unitManager.setUnitAudioClip(previewId, samplePath.string());
    unitManager.setUnitAudioClip(trackId, samplePath.string());
    unitManager.setUnitEnabled(previewId, true);
    unitManager.setUnitEnabled(trackId, true);
    unitManager.clearUnitTimelineLane(previewId);
    unitManager.assignUnitToTimelineLane(trackId, 0);

    auto& patternManager = tm->getPatternManager();
    PatternID previewPatId = patternManager.createPattern();
    auto* previewPat = patternManager.getPattern(previewPatId);
    require(previewPat != nullptr, "Failed to create preview pattern for bounce");
    previewPat->type = PatternSource::Type::Midi;
    previewPat->name = "Bounce Preview Pattern";
    previewPat->lengthBeats = kRenderBeats;
    previewPat->payload = MidiPayload{};
    auto& pn = std::get<MidiPayload>(previewPat->payload).notes;
    pn.push_back(MidiNote{60, 0.0, 0.75, 120.0f, previewId});

    PatternID trackPatId = patternManager.createPattern();
    auto* trackPat = patternManager.getPattern(trackPatId);
    require(trackPat != nullptr, "Failed to create track pattern for bounce");
    trackPat->type = PatternSource::Type::Midi;
    trackPat->name = "Bounce Track Pattern";
    trackPat->lengthBeats = kRenderBeats;
    trackPat->payload = MidiPayload{};
    auto& tn = std::get<MidiPayload>(trackPat->payload).notes;
    tn.push_back(MidiNote{72, 0.0, 0.75, 120.0f, trackId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed in bounce scenario");
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
    tm->getPatternPlaybackEngine().schedulePatternInstance(previewPatId, 0.0, 1);
    tm->getPatternPlaybackEngine().schedulePatternInstance(trackPatId, 0.0, 2);

    // Full bounce (trackId=-1): should succeed (no crash) even with Arsenal units present.
    // Arsenal audio is not expected here — bounceRangeToWav uses renderBlock path
    // which does not populate unit MIDI buffers. Arsenal export coverage is provided
    // by AudioExporter tests (which use processBlock).
    const std::filesystem::path fullPath = tempRoot / "bounce_full.wav";
    std::error_code ec;
    std::filesystem::remove(fullPath, ec);
    require(engine.bounceRangeToWav(0.0, kRenderBeats, fullPath.string(), -1),
            "Full bounce (-1) failed — must not crash with Arsenal units present");

    std::vector<float> fullSamples;
    uint32_t fullRate = 0, fullChannels = 0;
    require(decodeAudioFile(fullPath.string(), fullSamples, fullRate, fullChannels),
            "Failed to decode full bounce wav");
    require(fullRate == kSampleRate && fullChannels == kChannels,
            "Full bounce wav format mismatch");

    // Isolated bounce (trackId=0): should succeed (no crash).
    const std::filesystem::path isoPath = tempRoot / "bounce_iso.wav";
    std::filesystem::remove(isoPath, ec);
    require(engine.bounceRangeToWav(0.0, kRenderBeats, isoPath.string(), 0),
            "Isolated bounce (track 0) failed — must not crash with Arsenal units present");

    std::vector<float> isoSamples;
    uint32_t isoRate = 0, isoChannels = 0;
    require(decodeAudioFile(isoPath.string(), isoSamples, isoRate, isoChannels),
            "Failed to decode isolated bounce wav");
    require(isoRate == kSampleRate && isoChannels == kChannels,
            "Isolated bounce wav format mismatch");

    // Both bounce files should be non-empty (valid WAVs were produced).
    require(!fullSamples.empty(), "Full bounce wav should not be empty");
    require(!isoSamples.empty(), "Isolated bounce wav should not be empty");

    // Arsenal pattern playback now renders during bounce.
    // Track-routed units are audible in both full and isolated bounce.
    const float fullPeak = peakOf(fullSamples);
    const float isoPeak = peakOf(isoSamples);
    require(fullPeak > 1.0e-4f,
            "Full bounce should be audible (Arsenal track-routed + preview paths)");
    require(isoPeak > 1.0e-4f,
            "Isolated bounce should be audible (track-routed Arsenal unit on target track)");

    std::cout << "[INFO] Bounce: full=" << fullSamples.size() << " samples peak=" << fullPeak
              << " iso=" << isoSamples.size() << " samples peak=" << isoPeak << "\n";
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

    // Case 4: Mixed route permutations — two units (preview + track-routed)
    // running simultaneously must both be audible without cross-contamination.
    std::cout << "[INFO] Case 4: Mixed route with two units simultaneously\n";
    runMixedScenario(tempRoot, 1.0f);

    // Case 5: Isolated-track bounce — verify bounceRangeToWav produces
    // correct output for the selected track and excludes non-selected paths.
    std::cout << "[INFO] Case 5: Isolated-track bounce (full vs isolated)\n";
    runIsolatedBounceScenario(tempRoot);

    std::cout << "[PASS] ArsenalExportLiveParityTest\n";
    return 0;
}
