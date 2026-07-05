// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// TrackManagerMasterDuckingAuditTest — proves preview ducking affects master output
// after per-track metering.

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

bool writeSilentPreviewWav(const std::filesystem::path& path) {
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
    for (uint32_t i = 0; i < frames; ++i) {
        writeLe16(out, 0);
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
    trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationSeconds * 2.0);

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
    float duckGain{1.0f};
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

    const auto previewPath = std::filesystem::temp_directory_path() / "aestra_silent_preview_duck_audit.wav";
    require(writeSilentPreviewWav(previewPath), "failed to write silent preview wav");

    PreviewEngine preview;
    preview.setOutputSampleRate(kSampleRate);
    const PreviewResult result = preview.play(previewPath.string(), 0.0f, 0.5);
    require(result == PreviewResult::Success || result == PreviewResult::Pending, "preview failed to start");
    require(waitForPreviewReady(preview), "preview did not become ready");
    engine.setPreviewEngine(&preview);

    const Readout earlyDucked = renderLastBlock(engine, meters, 16);
    const Readout settledDucked = renderLastBlock(engine, meters, 1300);
    engine.setPreviewEngine(nullptr);
    preview.stop();
    engine.setTransportPlaying(false);

    const float expectedCenteredTrackPeak = kTrackSample * 0.70710678118f;
    const float earlyMasterRatio = earlyDucked.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float settledMasterRatio = settledDucked.masterPeak / std::max(baseline.masterPeak, 1.0e-9f);
    const float earlyTrackRatio = earlyDucked.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);
    const float settledTrackRatio = settledDucked.trackPeak / std::max(baseline.trackPeak, 1.0e-9f);

    std::cout << "baseline_track_peak=" << baseline.trackPeak << "\n";
    std::cout << "baseline_master_peak=" << baseline.masterPeak << "\n";
    std::cout << "baseline_output_peak=" << baseline.engineOutputPeak << "\n";
    std::cout << "early_ducked_track_peak=" << earlyDucked.trackPeak << "\n";
    std::cout << "early_ducked_master_peak=" << earlyDucked.masterPeak << "\n";
    std::cout << "early_ducked_output_peak=" << earlyDucked.engineOutputPeak << "\n";
    std::cout << "early_track_ratio=" << earlyTrackRatio << "\n";
    std::cout << "early_master_ratio=" << earlyMasterRatio << "\n";
    std::cout << "settled_ducked_track_peak=" << settledDucked.trackPeak << "\n";
    std::cout << "settled_ducked_master_peak=" << settledDucked.masterPeak << "\n";
    std::cout << "settled_ducked_output_peak=" << settledDucked.engineOutputPeak << "\n";
    std::cout << "settled_track_ratio=" << settledTrackRatio << "\n";
    std::cout << "settled_master_ratio=" << settledMasterRatio << "\n";

    require(std::abs(baseline.trackPeak - expectedCenteredTrackPeak) < 0.002f,
            "baseline track peak did not match centered pan-law level");
    require(std::abs(baseline.masterPeak - baseline.trackPeak) < 0.001f,
            "baseline master should match single track when preview is inactive");
    require(earlyTrackRatio > 0.99f && earlyTrackRatio < 1.01f,
            "early track meter changed while preview ducking was active");
    require(settledTrackRatio > 0.99f && settledTrackRatio < 1.01f,
            "settled track meter changed while preview ducking was active");
    require(earlyMasterRatio > 0.98f && earlyMasterRatio < 1.0f,
            "early master meter did not show the slow preview-duck ramp");
    require(settledMasterRatio > 0.49f && settledMasterRatio < 0.51f,
            "settled master meter did not duck by the expected 0.5 preview gain");
    require(std::abs(settledDucked.engineOutputPeak - settledDucked.masterPeak) < 0.001f,
            "engine output peak should match ducked master meter before callback preview mix");

    std::cout << "PASS: TrackManager master ducking audit reproduced master-only ducking with stable track meter\n";
    return 0;
}
