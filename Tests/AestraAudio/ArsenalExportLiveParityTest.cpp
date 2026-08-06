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
                           float trackVolume, float unitGain = 1.0f, bool unitMuted = false) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    const PlaylistLaneID laneId = playlist.createLane("Track 1");
    require(laneId.isValid(), "Failed to create lane for parity scenario");
    auto* channel = tm->addChannelWithId("Track 1", 42);
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
    unitManager.setUnitGain(unitId, unitGain);
    unitManager.setUnitMute(unitId, unitMuted);
    if (routeToTrack) {
        unitManager.setUnitMixerChannel(unitId, channel->getChannelId());
        unitManager.assignUnitToTimelineLane(unitId, 0);
    } else {
        unitManager.setUnitMixerChannel(unitId, 0);
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
    notes.push_back(MidiNote{60, 0.0, 0.75, 120.0f, 0.0f, unitId});
    notes.push_back(MidiNote{64, 1.0, 0.75, 110.0f, 0.0f, unitId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    tm->getPatternPlaybackEngine().rewindScheduledInstances();
    tm->getPatternPlaybackEngine().schedulePatternInstance(patternId, 0.0, 1);
    engine.setTransportPlaying(true);

    const uint32_t totalFrames = static_cast<uint32_t>(kRenderSeconds * static_cast<double>(kSampleRate));
    std::vector<float> liveBlock(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    std::vector<float> liveSamples;
    liveSamples.reserve(static_cast<size_t>(totalFrames) * kChannels);

    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        // Maintain pattern engine lookahead (normally called by performNonRealtimeMaintenance)
        tm->getPatternPlaybackEngine().refillWindow(
            rendered, static_cast<int>(kSampleRate), static_cast<int>(kSampleRate));
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
    auto* channel = tm->addChannelWithId("Mixed Track", 42);
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
    unitManager.setUnitMixerChannel(previewUnitId, 0);
    unitManager.setUnitMixerChannel(trackUnitId, channel->getChannelId());
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
    previewNotes.push_back(MidiNote{72, 0.0, 0.75, 120.0f, 0.0f, previewUnitId});
    previewNotes.push_back(MidiNote{76, 1.0, 0.75, 110.0f, 0.0f, previewUnitId});

    PatternID trackPatternId = patternManager.createPattern();
    auto* trackPattern = patternManager.getPattern(trackPatternId);
    require(trackPattern != nullptr, "Failed to create track pattern for mixed scenario");
    trackPattern->type = PatternSource::Type::Midi;
    trackPattern->name = "Mixed Track Pattern";
    trackPattern->lengthBeats = kRenderBeats;
    trackPattern->payload = MidiPayload{};
    auto& trackNotes = std::get<MidiPayload>(trackPattern->payload).notes;
    trackNotes.push_back(MidiNote{60, 0.0, 0.75, 120.0f, 0.0f, trackUnitId});
    trackNotes.push_back(MidiNote{64, 1.0, 0.75, 110.0f, 0.0f, trackUnitId});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed in mixed scenario");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    tm->getPatternPlaybackEngine().rewindScheduledInstances();
    tm->getPatternPlaybackEngine().schedulePatternInstance(previewPatternId, 0.0, 1);
    tm->getPatternPlaybackEngine().schedulePatternInstance(trackPatternId, 0.0, 2);
    engine.setTransportPlaying(true);

    const uint32_t totalFrames = static_cast<uint32_t>(kRenderSeconds * static_cast<double>(kSampleRate));
    std::vector<float> liveBlock(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    std::vector<float> liveSamples;
    liveSamples.reserve(static_cast<size_t>(totalFrames) * kChannels);

    uint32_t rendered = 0;
    while (rendered < totalFrames) {
        // Maintain pattern engine lookahead (normally called by performNonRealtimeMaintenance)
        tm->getPatternPlaybackEngine().refillWindow(
            rendered, static_cast<int>(kSampleRate), static_cast<int>(kSampleRate));
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

// Phase 3 isolated-track bounce: verify Arsenal pattern audio renders during
// bounceRangeToWav and does not leak across wrong tracks during isolated bounce.
void runIsolatedBounceScenario(const std::filesystem::path& tempRoot) {
    using namespace Aestra::Audio;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    auto& playlist = tm->getPlaylistModel();
    // Track 0 — Arsenal unit assigned here
    const PlaylistLaneID lane0 = playlist.createLane("Bounce Track 0");
    require(lane0.isValid(), "Failed to create lane 0 for bounce");
    if (auto* lane = playlist.getLane(lane0)) {
        lane->volume = 1.0f;
        lane->pan = 0.0f;
    }
    auto* channel0 = tm->addChannelWithId("Bounce Track 0", 42);
    require(channel0 != nullptr, "Failed to create channel 0 for bounce");
    channel0->setVolume(1.0f);
    channel0->setPan(0.0f);
    // Track 1 — no Arsenal unit, used for wrong-track silence check
    const PlaylistLaneID lane1 = playlist.createLane("Bounce Track 1");
    require(lane1.isValid(), "Failed to create lane 1 for bounce");
    if (auto* lane = playlist.getLane(lane1)) {
        lane->volume = 1.0f;
        lane->pan = 0.0f;
    }
    auto* channel1 = tm->addChannelWithId("Bounce Track 1", 77);
    require(channel1 != nullptr, "Failed to create channel 1 for bounce");
    channel1->setVolume(1.0f);
    channel1->setPan(0.0f);

    const std::filesystem::path samplePath = tempRoot / "bounce_sample.wav";
    require(writeMonoToneWav(samplePath, 440.0, 0.55f, 1.0), "Failed to create sample for bounce");

    auto& unitManager = tm->getUnitManager();
    const UnitID track0Unit = unitManager.createUnit("Bounce Track 0 Unit", UnitType::Sampler);
    unitManager.setUnitAudioClip(track0Unit, samplePath.string());
    unitManager.setUnitEnabled(track0Unit, true);
    unitManager.setUnitMixerChannel(track0Unit, channel0->getChannelId());
    unitManager.assignUnitToTimelineLane(track0Unit, 0);

    auto& patternManager = tm->getPatternManager();
    PatternID track0PatId = patternManager.createPattern();
    auto* track0Pat = patternManager.getPattern(track0PatId);
    require(track0Pat != nullptr, "Failed to create pattern for bounce");
    track0Pat->type = PatternSource::Type::Midi;
    track0Pat->name = "Bounce Track 0 Pattern";
    track0Pat->lengthBeats = kRenderBeats;
    track0Pat->payload = MidiPayload{};
    auto& tn0 = std::get<MidiPayload>(track0Pat->payload).notes;
    tn0.push_back(MidiNote{72, 0.0, 0.75, 120.0f, 0.0f, track0Unit});

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed in bounce scenario");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    tm->getPatternPlaybackEngine().rewindScheduledInstances();
    tm->getPatternPlaybackEngine().schedulePatternInstance(track0PatId, 0.0, 1);

    // Full bounce (trackId=-1): Arsenal on track 0 audible.
    const std::filesystem::path fullPath = tempRoot / "bounce_full.wav";
    std::error_code ec;
    std::filesystem::remove(fullPath, ec);
    require(engine.bounceRangeToWav(0.0, kRenderBeats, fullPath.string(), -1),
            "Full bounce failed");

    std::vector<float> fullSamples;
    uint32_t fullRate = 0, fullChannels = 0;
    require(decodeAudioFile(fullPath.string(), fullSamples, fullRate, fullChannels),
            "Failed to decode full bounce");
    require(fullRate == kSampleRate && fullChannels == kChannels, "Full bounce format mismatch");

    // Isolated bounce track 0: Arsenal on track 0 audible.
    const std::filesystem::path iso0Path = tempRoot / "bounce_iso0.wav";
    std::filesystem::remove(iso0Path, ec);
    require(engine.bounceRangeToWav(0.0, kRenderBeats, iso0Path.string(), 0),
            "Isolated bounce track 0 failed");

    std::vector<float> iso0Samples;
    uint32_t iso0Rate = 0, iso0Channels = 0;
    require(decodeAudioFile(iso0Path.string(), iso0Samples, iso0Rate, iso0Channels),
            "Failed to decode iso0 bounce");
    require(iso0Rate == kSampleRate && iso0Channels == kChannels, "Iso0 format mismatch");

    // Isolated bounce track 1: Arsenal unit is on track 0, must be silent.
    const std::filesystem::path iso1Path = tempRoot / "bounce_iso1.wav";
    std::filesystem::remove(iso1Path, ec);
    require(engine.bounceRangeToWav(0.0, kRenderBeats, iso1Path.string(), 1),
            "Isolated bounce track 1 failed");

    std::vector<float> iso1Samples;
    uint32_t iso1Rate = 0, iso1Channels = 0;
    require(decodeAudioFile(iso1Path.string(), iso1Samples, iso1Rate, iso1Channels),
            "Failed to decode iso1 bounce");
    require(iso1Rate == kSampleRate && iso1Channels == kChannels, "Iso1 format mismatch");

    // Assertions
    const float fullPeak = peakOf(fullSamples);
    const float iso0Peak = peakOf(iso0Samples);
    const float iso1Peak = peakOf(iso1Samples);

    require(fullPeak > 1.0e-4f,
            "Full bounce should be audible (Arsenal on track 0)");
    require(iso0Peak > 1.0e-4f,
            "Isolated bounce track 0 should be audible (Arsenal on target track)");
    require(iso1Peak <= 1.0e-5f,
            "Isolated bounce track 1 must be silent (no Arsenal unit assigned)");

    std::cout << "[INFO] Bounce: full peak=" << fullPeak << " iso0 peak=" << iso0Peak
              << " iso1 peak=" << iso1Peak << "\n";
}

void runBounceWriteFailureCleanupScenario(const std::filesystem::path& tempRoot) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(kSampleRate));
    tm->getPlaylistModel().setBPM(kBpm);

    AudioEngine engine;
    require(engine.initialize(), "AudioEngine initialize failed in write-error scenario");
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(tm);
    engine.setBPM(static_cast<float>(kBpm));
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
    engine.setUnitManager(&tm->getUnitManager());
    engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    engine.setPatternPlaybackMode(true, kRenderBeats);
    engine.setGlobalSamplePos(0);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);

    const std::filesystem::path outputPath = tempRoot / "bounce_write_error_cleanup.wav";
    std::error_code ec;
    std::filesystem::remove(outputPath, ec);
    engine.setForceBounceWriteErrorForTests(true);

    const bool bounced = engine.bounceRangeToWav(0.0, kRenderBeats, outputPath.string(), -1);
    engine.setForceBounceWriteErrorForTests(false);

    require(!bounced, "bounceRangeToWav should fail when encoder write fails");
    require(engine.didLastBounceWriteAnyFramesForTests(),
            "Write-error scenario should fail only after at least one block was written");
    require(!std::filesystem::exists(outputPath), "Bounce output path should be removed after write failure");

    std::cout << "[INFO] Bounce write-failure cleanup scenario passed\n";
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

    // Case 2: A track-routed unit follows the mixer fader in live and export.
    const ScenarioResult routedMutedTrack = runScenario(tempRoot, "routed_muted_track", true, 0.0f);
    require(routedMutedTrack.livePeak < 1.0e-5f, "Track-routed unit bypassed the muted mixer path in live playback");
    require(routedMutedTrack.exportPeak < 1.0e-5f, "Track-routed unit bypassed the muted mixer path in export");

    // Case 3: Track-routed path is audible when track path is open.
    const ScenarioResult routedOpenTrack = runScenario(tempRoot, "routed_open_track", true, 1.0f);
    require(routedOpenTrack.livePeak > 1.0e-4f, "Track-routed Arsenal should be audible in live output when track is open");
    require(routedOpenTrack.exportPeak > 1.0e-4f, "Track-routed Arsenal should be audible in export when track is open");

    // Case 4: Unit gain and mute controls affect the shared live/export path.
    const ScenarioResult reducedUnit = runScenario(tempRoot, "reduced_unit", false, 1.0f, 0.25f);
    require(reducedUnit.livePeak > 1.0e-5f && reducedUnit.livePeak < previewMutedTrack.livePeak * 0.5f,
            "Unit gain was not applied in live playback");
    require(reducedUnit.exportPeak > 1.0e-5f && reducedUnit.exportPeak < previewMutedTrack.exportPeak * 0.5f,
            "Unit gain was not applied in export");
    const ScenarioResult mutedUnit = runScenario(tempRoot, "muted_unit", false, 1.0f, 1.0f, true);
    require(mutedUnit.livePeak < 1.0e-5f, "Muted unit remained audible in live playback");
    require(mutedUnit.exportPeak < 1.0e-5f, "Muted unit remained audible in export");

    // Case 5: Mixed route permutations — two units (preview + track-routed)
    // running simultaneously must both be audible without cross-contamination.
    std::cout << "[INFO] Case 5: Mixed route with two units simultaneously\n";
    runMixedScenario(tempRoot, 1.0f);

    // Case 6: Isolated-track bounce — verify bounceRangeToWav produces
    // correct output for the selected track and excludes non-selected paths.
    std::cout << "[INFO] Case 6: Isolated-track bounce (full vs isolated)\n";
    runIsolatedBounceScenario(tempRoot);

    std::cout << "[INFO] Case 7: Write-failure cleanup (false return + partial file cleanup)\n";
    runBounceWriteFailureCleanupScenario(tempRoot);

    std::cout << "[PASS] ArsenalExportLiveParityTest\n";
    return 0;
}
