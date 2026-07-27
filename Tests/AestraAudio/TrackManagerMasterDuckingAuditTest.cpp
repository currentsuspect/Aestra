// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerMasterDuckingAuditTest — guards preview ducking so it only affects
// the master when preview output is actually audible.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/TrackManager.h"
#include "Playback/PreviewEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockSize = 256;
constexpr uint32_t kTotalFrames = kSampleRate * 8;
constexpr float kTrackSample = 0.25f;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

void writeLe16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeLe32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 24) & 0xff));
}

bool writePreviewWav(const std::filesystem::path& path, float sampleValue) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    constexpr uint16_t channels = 1;
    constexpr uint16_t bitsPerSample = 16;
    const uint32_t frames = kTotalFrames;
    const uint32_t dataBytes = frames * channels * (bitsPerSample / 8);
    out.write("RIFF", 4);
    writeLe32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, kSampleRate);
    writeLe32(out, kSampleRate * channels * (bitsPerSample / 8));
    writeLe16(out, channels * (bitsPerSample / 8));
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, dataBytes);
    const auto pcm = static_cast<int16_t>(std::clamp(sampleValue, -1.0f, 1.0f) * 32767.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        writeLe16(out, static_cast<uint16_t>(pcm));
    }
    return out.good();
}

std::shared_ptr<TrackManager> createOneTrackManager() {
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));
    trackManager->getPlaylistModel().setBPM(120.0);
    auto* channel = trackManager->addChannel("Track 1");
    require(channel != nullptr, "failed to add TrackManager channel");
    channel->setVolume(1.0f);
    channel->setPan(0.0f);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = kSampleRate;
    buffer->numChannels = kChannels;
    buffer->numFrames = kTotalFrames;
    buffer->interleavedData.assign(static_cast<size_t>(kTotalFrames) * kChannels, kTrackSample);

    const std::string sourcePath =
        (std::filesystem::temp_directory_path() / "aestra_trackmanager_duck_audit_source.wav").string();
    const ClipSourceID sourceId = trackManager->getSourceManager().createRecordedSource(sourcePath, "duck_audit", buffer);
    require(sourceId.value != 0, "failed to create recorded source");

    const double durationSeconds = static_cast<double>(kTotalFrames) / static_cast<double>(kSampleRate);
    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = durationSeconds;
    payload.slices.push_back({0.0, durationSeconds, 0.0, static_cast<double>(kTotalFrames)});

    const PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane("Track 1");
    const PatternID patternId =
        trackManager->getPatternManager().createAudioPattern("duck_audit_pattern", durationSeconds * 2.0, payload);
    trackManager->getPatternManager().setPatternMixerChannel(patternId, channel->getChannelId());
    const ClipInstanceID clipId =
        trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationSeconds * 2.0);
    require(trackManager->getPlaylistModel().setClipEdits(clipId, ClipEdits{}),
            "failed to configure unity-gain ducking audit clip");

    trackManager->buildAndShareSlotMap();
    return trackManager;
}

bool waitForPreviewReady(PreviewEngine& preview) {
    for (int i = 0; i < 200; ++i) {
        if (preview.isBufferReady() && preview.isPlaying()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

struct Readout {
    float trackPeak{0.0f};
    float masterPeak{0.0f};
    float engineOutputPeak{0.0f};
};

struct DuckScenario {
    float trackRatio{0.0f};
    float masterRatio{0.0f};
    AudioEngine::PreviewDuckSource source{AudioEngine::PreviewDuckSource::None};
};

Readout renderLastBlock(AudioEngine& engine, const std::shared_ptr<MeterSnapshotBuffer>& meters, uint32_t blocks) {
    std::vector<float> block(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    Readout readout;
    for (uint32_t blockIndex = 0; blockIndex < blocks; ++blockIndex) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, kBlockSize, 0.0);
    }

    for (float sample : block) {
        readout.engineOutputPeak = std::max(readout.engineOutputPeak, std::abs(sample));
    }

    const auto track = meters->readMeter(0);
    const auto master = meters->readMeter(ChannelSlotMap::MASTER_SLOT_INDEX);
    readout.trackPeak = std::max(track.peakL, track.peakR);
    readout.masterPeak = std::max(master.peakL, master.peakR);
    return readout;
}

void primePreviewAudibility(PreviewEngine& preview) {
    std::vector<float> previewBlock(static_cast<size_t>(kBlockSize) * kChannels, 0.0f);
    preview.processRealtime(previewBlock.data(), kBlockSize, kChannels);
}

DuckScenario renderAudiblePreviewScenario(float attenuationDb) {
    auto trackManager = createOneTrackManager();
    auto meters = std::make_shared<MeterSnapshotBuffer>();

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(trackManager);
    engine.setMeterSnapshots(meters);
    engine.setContinuousParams(trackManager->getContinuousParams());
    engine.setChannelSlotMap(trackManager->getChannelSlotMapShared());
    engine.setSafetyLimiterEnabled(false);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setPreviewDuckingAttenuationDb(attenuationDb);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    require(engine.initialize(), "AudioEngine initialize failed in configurable duck scenario");
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    const Readout baseline = renderLastBlock(engine, meters, 16);

    const auto previewPath = std::filesystem::temp_directory_path() /
                             ("aestra_configurable_preview_duck_" + std::to_string(static_cast<int>(attenuationDb)) +
                              ".wav");
    require(writePreviewWav(previewPath, 0.25f), "failed to write configurable audible preview wav");

    PreviewEngine preview;
    preview.setOutputSampleRate(kSampleRate);
    const PreviewResult result = preview.play(previewPath.string(), 0.0f, 0.5);
    require(result == PreviewResult::Success || result == PreviewResult::Pending,
            "configurable audible preview failed to start");
    require(waitForPreviewReady(preview), "configurable audible preview did not become ready");
    engine.setPreviewEngine(&preview);
    primePreviewAudibility(preview);

    const Readout ducked = renderLastBlock(engine, meters, 6);
    DuckScenario scenario;
    scenario.trackRatio = ducked.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);
    scenario.masterRatio = ducked.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    scenario.source = engine.getPreviewDuckSource();

    engine.setPreviewEngine(nullptr);
    preview.stop();
    engine.setTransportPlaying(false);
    return scenario;
}

} // namespace

int main() {
    auto trackManager = createOneTrackManager();
    auto meters = std::make_shared<MeterSnapshotBuffer>();

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockSize, kChannels);
    engine.setTrackManager(trackManager);
    engine.setMeterSnapshots(meters);
    engine.setContinuousParams(trackManager->getContinuousParams());
    engine.setChannelSlotMap(trackManager->getChannelSlotMapShared());
    engine.setSafetyLimiterEnabled(false);
    engine.setMetronomeEnabled(false);
    engine.setAuditionModeEnabled(false);
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    require(engine.initialize(), "AudioEngine initialize failed");
    engine.setGlobalSamplePos(0);
    engine.setTransportPlaying(true);

    const Readout baseline = renderLastBlock(engine, meters, 16);

    PreviewEngine inactivePreview;
    inactivePreview.setOutputSampleRate(kSampleRate);
    engine.setPreviewEngine(&inactivePreview);
    const Readout inactivePreviewReadout = renderLastBlock(engine, meters, 6);

    const auto silentPreviewPath = std::filesystem::temp_directory_path() / "aestra_silent_preview_duck_audit.wav";
    require(writePreviewWav(silentPreviewPath, 0.0f), "failed to write silent preview wav");

    PreviewEngine silentPreview;
    silentPreview.setOutputSampleRate(kSampleRate);
    const PreviewResult silentResult = silentPreview.play(silentPreviewPath.string(), 0.0f, 0.5);
    require(silentResult == PreviewResult::Success || silentResult == PreviewResult::Pending,
            "silent preview failed to start");
    require(waitForPreviewReady(silentPreview), "silent preview did not become ready");
    engine.setPreviewEngine(&silentPreview);
    primePreviewAudibility(silentPreview);
    const Readout silentPreviewReadout = renderLastBlock(engine, meters, 6);
    silentPreview.stop();

    const auto audiblePreviewPath = std::filesystem::temp_directory_path() / "aestra_audible_preview_duck_audit.wav";
    require(writePreviewWav(audiblePreviewPath, 0.25f), "failed to write audible preview wav");

    PreviewEngine audiblePreview;
    audiblePreview.setOutputSampleRate(kSampleRate);
    const PreviewResult audibleResult = audiblePreview.play(audiblePreviewPath.string(), 0.0f, 0.5);
    require(audibleResult == PreviewResult::Success || audibleResult == PreviewResult::Pending,
            "audible preview failed to start");
    require(waitForPreviewReady(audiblePreview), "audible preview did not become ready");
    engine.setPreviewEngine(&audiblePreview);
    primePreviewAudibility(audiblePreview);

    const Readout settledDucked = renderLastBlock(engine, meters, 6);
    const auto duckSource = engine.getPreviewDuckSource();

    audiblePreview.stop();
    const Readout heldAfterStop = renderLastBlock(engine, meters, 1);
    const auto heldSource = engine.getPreviewDuckSource();
    const Readout releasedAfterHold = renderLastBlock(engine, meters, 64);
    const auto releasedSource = engine.getPreviewDuckSource();
    engine.setPreviewEngine(nullptr);
    engine.setTransportPlaying(false);

    const float expectedCenteredTrackPeak = kTrackSample * 0.70710678118f;
    const float inactivePreviewMasterRatio = inactivePreviewReadout.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float silentPreviewMasterRatio = silentPreviewReadout.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float settledMasterRatio = settledDucked.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float heldMasterRatio = heldAfterStop.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float releasedMasterRatio = releasedAfterHold.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float inactivePreviewTrackRatio = inactivePreviewReadout.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);
    const float silentPreviewTrackRatio = silentPreviewReadout.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);
    const float settledTrackRatio = settledDucked.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);
    const DuckScenario threeDbScenario = renderAudiblePreviewScenario(3.0f);
    const DuckScenario offScenario = renderAudiblePreviewScenario(0.0f);

    std::cout << "baseline_track_peak=" << baseline.trackPeak << "\n";
    std::cout << "baseline_master_peak=" << baseline.masterPeak << "\n";
    std::cout << "baseline_output_peak=" << baseline.engineOutputPeak << "\n";
    std::cout << "inactive_preview_track_peak=" << inactivePreviewReadout.trackPeak << "\n";
    std::cout << "inactive_preview_master_peak=" << inactivePreviewReadout.masterPeak << "\n";
    std::cout << "inactive_preview_track_ratio=" << inactivePreviewTrackRatio << "\n";
    std::cout << "inactive_preview_master_ratio=" << inactivePreviewMasterRatio << "\n";
    std::cout << "silent_preview_track_peak=" << silentPreviewReadout.trackPeak << "\n";
    std::cout << "silent_preview_master_peak=" << silentPreviewReadout.masterPeak << "\n";
    std::cout << "silent_preview_track_ratio=" << silentPreviewTrackRatio << "\n";
    std::cout << "silent_preview_master_ratio=" << silentPreviewMasterRatio << "\n";
    std::cout << "settled_ducked_track_peak=" << settledDucked.trackPeak << "\n";
    std::cout << "settled_ducked_master_peak=" << settledDucked.masterPeak << "\n";
    std::cout << "settled_ducked_output_peak=" << settledDucked.engineOutputPeak << "\n";
    std::cout << "settled_track_ratio=" << settledTrackRatio << "\n";
    std::cout << "settled_master_ratio=" << settledMasterRatio << "\n";
    std::cout << "held_after_stop_master_ratio=" << heldMasterRatio << "\n";
    std::cout << "released_after_hold_master_ratio=" << releasedMasterRatio << "\n";
    std::cout << "three_db_track_ratio=" << threeDbScenario.trackRatio << "\n";
    std::cout << "three_db_master_ratio=" << threeDbScenario.masterRatio << "\n";
    std::cout << "off_track_ratio=" << offScenario.trackRatio << "\n";
    std::cout << "off_master_ratio=" << offScenario.masterRatio << "\n";

    require(std::abs(baseline.trackPeak - expectedCenteredTrackPeak) < 0.002f,
            "baseline track peak did not match centered pan-law level");
    require(std::abs(baseline.masterPeak - baseline.trackPeak) < 0.001f,
            "baseline master should match single track when preview is inactive");
    require(inactivePreviewTrackRatio > 0.99f && inactivePreviewTrackRatio < 1.01f,
            "inactive preview changed track meter");
    require(inactivePreviewMasterRatio > 0.99f && inactivePreviewMasterRatio < 1.01f,
            "inactive preview ducked the master");
    require(silentPreviewTrackRatio > 0.99f && silentPreviewTrackRatio < 1.01f,
            "silent preview changed track meter");
    require(silentPreviewMasterRatio > 0.99f && silentPreviewMasterRatio < 1.01f,
            "silent preview ducked the master");
    require(settledTrackRatio > 0.99f && settledTrackRatio < 1.01f,
            "settled track meter changed while preview ducking was active");
    require(settledMasterRatio > 0.49f && settledMasterRatio < 0.51f,
            "audible preview did not duck to the expected 0.5 gain within the 50ms fade window");
    require(duckSource == AudioEngine::PreviewDuckSource::BrowserPreview,
            "audible preview did not publish browser-preview duck source");
    require(heldMasterRatio > 0.49f && heldMasterRatio < 0.51f,
            "preview duck released immediately instead of holding briefly after preview stopped");
    require(heldSource == AudioEngine::PreviewDuckSource::BrowserPreview,
            "preview duck source cleared during hold window");
    require(releasedMasterRatio > 0.99f && releasedMasterRatio < 1.01f,
            "preview duck did not release back to unity after hold and release window");
    require(releasedSource == AudioEngine::PreviewDuckSource::None,
            "preview duck source did not clear after release");
    require(std::abs(settledDucked.engineOutputPeak - settledDucked.masterPeak) < 0.001f,
            "engine output peak should match ducked master meter before callback preview mix");
    require(threeDbScenario.trackRatio > 0.99f && threeDbScenario.trackRatio < 1.01f,
            "-3dB duck scenario changed track meter");
    require(threeDbScenario.masterRatio > 0.70f && threeDbScenario.masterRatio < 0.72f,
            "-3dB duck scenario did not reach configured target");
    require(threeDbScenario.source == AudioEngine::PreviewDuckSource::BrowserPreview,
            "-3dB duck scenario did not publish browser-preview source");
    require(offScenario.trackRatio > 0.99f && offScenario.trackRatio < 1.01f,
            "duck-off scenario changed track meter");
    require(offScenario.masterRatio > 0.99f && offScenario.masterRatio < 1.01f,
            "duck-off scenario attenuated master despite audible preview");
    require(offScenario.source == AudioEngine::PreviewDuckSource::None,
            "duck-off scenario published a duck source");

    std::cout << "PASS: TrackManager master ducking is audible-preview gated, configurable, visible, and timed\n";
    return 0;
}
