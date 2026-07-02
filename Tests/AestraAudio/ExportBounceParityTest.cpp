// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ExportBounceParityTest — Verifies that master bounce output matches the sum of
// isolated track bounces for the same project. Spec §8.
//
// Pass criteria:
//   - Master bounce output ≈ sum(isolated track bounces)
//   - RMS difference < -80 dB (differences due to master stage: safety limiter, master gain)
//   - Both paths produce identical sample counts and channel formats

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Core/ChannelSlotMap.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr double kBpm = 120.0;
constexpr double kDurationBeats = 4.0;
constexpr double kDurationSeconds = 2.0;
constexpr double kFrequencyHz = 440.0;
constexpr float kAmplitude = 0.3f;
constexpr double kTau = 6.28318530717958647692;
constexpr double kPassRmsDb = -80.0; // More negative = better

struct DecodedAudio {
    std::vector<float> samples;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
};

bool decodeWav(const std::filesystem::path& path, DecodedAudio& out) {
    return decodeAudioFile(path.string(), out.samples, out.sampleRate, out.channels);
}

double computeRmsDb(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.0;
    double sumSq = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        double diff = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        sumSq += diff * diff;
    }
    double rms = std::sqrt(sumSq / static_cast<double>(a.size()));
    return 20.0 * std::log10(std::max(rms, 1e-15));
}

float computePeak(const std::vector<float>& samples) {
    float peak = 0.0f;
    for (float s : samples) peak = std::max(peak, std::abs(s));
    return peak;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path tempRoot = fs::temp_directory_path() / "aestra_bounce_parity";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);

    std::cout << "=== Aestra Export/Bounce Parity Test ===\n\n";

    // --- Create project with 3 tracks ---
    auto trackManager = std::make_shared<TrackManager>();
    trackManager->setOutputSampleRate(static_cast<double>(kSampleRate));

    for (int t = 0; t < 3; ++t) {
        std::string name = "Track_" + std::to_string(t);
        trackManager->addChannel(name);
    }

    // Create audio sources for each track at different frequencies
    static constexpr double freqs[] = {440.0, 660.0, 880.0};
    static constexpr float amps[] = {0.3f, 0.2f, 0.15f};

    for (int t = 0; t < 3; ++t) {
        uint32_t numFrames = static_cast<uint32_t>(kSampleRate * kDurationSeconds);
        auto buffer = std::make_shared<AudioBufferData>();
        buffer->sampleRate = kSampleRate;
        buffer->numChannels = kChannels;
        buffer->numFrames = numFrames;
        buffer->interleavedData.resize(static_cast<size_t>(numFrames) * kChannels, 0.0f);

        for (uint32_t i = 0; i < numFrames; ++i) {
            double tSec = static_cast<double>(i) / kSampleRate;
            float s = static_cast<float>(std::sin(kTau * freqs[t] * tSec)) * amps[t];
            buffer->interleavedData[static_cast<size_t>(i) * 2] = s;
            buffer->interleavedData[static_cast<size_t>(i) * 2 + 1] = s;
        }

        std::string stem = "bounce_source_" + std::to_string(t);
        fs::path wavPath = tempRoot / (stem + ".wav");
        std::string pathStr = wavPath.string();

        {
            std::ofstream f(pathStr, std::ios::binary);
            auto write16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
            auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
            uint32_t dataSize = static_cast<uint32_t>(numFrames * kChannels * 2);
            uint32_t fileSize = 36 + dataSize;
            f.write("RIFF", 4); write32(fileSize); f.write("WAVE", 4);
            f.write("fmt ", 4); write32(16);
            write16(1); write16(static_cast<uint16_t>(kChannels));
            write32(kSampleRate);
            write32(kSampleRate * kChannels * 2);
            write16(static_cast<uint16_t>(kChannels * 2)); write16(16);
            f.write("data", 4); write32(dataSize);
            for (float s : buffer->interleavedData) {
                int16_t sample = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
                write16(static_cast<uint16_t>(sample));
            }
        }

        ClipSourceID sourceId = trackManager->getSourceManager().createRecordedSource(
            pathStr, stem, buffer);
        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = kDurationSeconds;
        payload.slices.push_back({0.0, kDurationSeconds, 0.0, static_cast<double>(numFrames)});

        PlaylistLaneID laneId = trackManager->getPlaylistModel().createLane(stem);
        PatternID patternId = trackManager->getPatternManager().createAudioPattern(
            stem, kDurationBeats, payload);
        trackManager->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, kDurationBeats);
    }

    // --- Master bounce ---
    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(512, kChannels);
    engine.setTrackManager(trackManager);
    engine.setBPM(static_cast<float>(kBpm));
    trackManager->buildAndShareSlotMap();
    if (auto slotMap = trackManager->getChannelSlotMapShared()) {
        engine.setChannelSlotMap(slotMap);
    }
    engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*trackManager));
    engine.setSafetyLimiterEnabled(false);
    engine.initialize();

    fs::path masterPath = tempRoot / "master_bounce.wav";
    fs::remove(masterPath, ec);

    bool masterOk = engine.bounceRangeToWav(0.0, kDurationBeats, masterPath.string(), -1);
    if (!masterOk) {
        std::cerr << "Master bounce failed\n";
        return 1;
    }

    // --- Isolated track bounces ---
    std::vector<fs::path> trackPaths;
    for (int t = 0; t < 3; ++t) {
        fs::path trackPath = tempRoot / ("track_" + std::to_string(t) + "_bounce.wav");
        fs::remove(trackPath, ec);
        bool ok = engine.bounceRangeToWav(0.0, kDurationBeats, trackPath.string(), t);
        if (!ok) {
            std::cerr << "Isolated track " << t << " bounce failed\n";
            return 1;
        }
        trackPaths.push_back(trackPath);
    }

    // --- Verify ---
    // Decode master bounce
    DecodedAudio masterAudio;
    if (!decodeWav(masterPath, masterAudio)) {
        std::cerr << "Failed to decode master bounce\n";
        return 1;
    }

    float masterPeak = computePeak(masterAudio.samples);

    // Decode and sum isolated track bounces in software
    std::vector<float> sumSamples(masterAudio.samples.size(), 0.0f);
    for (int t = 0; t < 3; ++t) {
        DecodedAudio trackAudio;
        if (!decodeWav(trackPaths[t], trackAudio)) {
            std::cerr << "Failed to decode track " << t << " bounce\n";
            return 1;
        }

        if (trackAudio.sampleRate != masterAudio.sampleRate || trackAudio.channels != masterAudio.channels ||
            trackAudio.samples.size() != masterAudio.samples.size()) {
            std::cerr << "Track " << t << " bounce format/length mismatch: "
                      << trackAudio.sampleRate << " Hz, " << trackAudio.channels << " ch, "
                      << trackAudio.samples.size() << " samples vs master "
                      << masterAudio.sampleRate << " Hz, " << masterAudio.channels << " ch, "
                      << masterAudio.samples.size() << " samples\n";
            return 1;
        }

        for (size_t i = 0; i < sumSamples.size(); ++i) {
            sumSamples[i] += trackAudio.samples[i];
        }
    }

    // The master bounce includes transport fade-in (~256 samples) and clip-edge fades (~128 samples)
    // that the isolated bounce path doesn't replicate. Skip the first 512 samples to avoid these.
    static constexpr size_t kSkipSamples = 512;
    if (sumSamples.size() <= kSkipSamples || masterAudio.samples.size() <= kSkipSamples) {
        std::cerr << "Not enough samples to skip fade-in\n";
        return 1;
    }
    size_t compareSamples = masterAudio.samples.size() - kSkipSamples;
    std::vector<float> masterTrimmed(masterAudio.samples.begin() + static_cast<ptrdiff_t>(kSkipSamples),
                                     masterAudio.samples.begin() + static_cast<ptrdiff_t>(kSkipSamples + compareSamples));
    std::vector<float> sumTrimmed(sumSamples.begin() + static_cast<ptrdiff_t>(kSkipSamples),
                                  sumSamples.begin() + static_cast<ptrdiff_t>(kSkipSamples + compareSamples));

    double rmsDb = computeRmsDb(masterTrimmed, sumTrimmed);
    float sumPeak = computePeak(sumTrimmed);

    std::cout << "\nComparison (master vs sum of isolated):\n";
    std::cout << "  Master peak: " << masterPeak << "\n";
    std::cout << "  Sum peak:    " << sumPeak << "\n";
    std::cout << "  RMS diff:    " << rmsDb << " dB\n";
    std::cout << "  Threshold:   < " << kPassRmsDb << " dB\n";

    bool pass = (rmsDb <= kPassRmsDb);
    std::cout << "  Status: " << (pass ? "[PASS]" : "[FAIL]") << "\n";

    // Cleanup
    fs::remove(masterPath, ec);
    for (const auto& p : trackPaths) fs::remove(p, ec);
    fs::remove(tempRoot, ec);

    return pass ? 0 : 1;
}
