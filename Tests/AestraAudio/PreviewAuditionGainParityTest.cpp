#include "Core/AudioEngine.h"
#include "Core/AudioGraph.h"
#include "DSP/PanLaw.h"
#include "IO/SamplePool.h"
#include "Models/TrackManager.h"
#include "Playback/AuditionEngine.h"
#include "Playback/PreviewEngine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kBlockFrames = 512;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kTotalFrames = kSampleRate;
constexpr float kSourceSample = 0.25f;
// stereo-balance law: unity at center (strip pan-law fix 2026-08-14)
constexpr float kExpectedOutput = kSourceSample;

void writeUint32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
    out.put(static_cast<char>((value >> 16) & 0xFF));
    out.put(static_cast<char>((value >> 24) & 0xFF));
}

void writeUint16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xFF));
    out.put(static_cast<char>((value >> 8) & 0xFF));
}

bool writeMonoConstantWav(const fs::path& path) {
    std::ofstream wav(path, std::ios::binary);
    if (!wav) {
        return false;
    }

    constexpr uint16_t channels = 1;
    constexpr uint16_t bitsPerSample = 16;
    constexpr uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t dataChunkSize = kTotalFrames * channels * bytesPerSample;
    const uint32_t riffChunkSize = 4 + (8 + 16) + (8 + dataChunkSize);
    const auto pcm = static_cast<int16_t>(std::lrint(kSourceSample * 32767.0f));

    wav.write("RIFF", 4);
    writeUint32(wav, riffChunkSize);
    wav.write("WAVE", 4);
    wav.write("fmt ", 4);
    writeUint32(wav, 16);
    writeUint16(wav, 1);
    writeUint16(wav, channels);
    writeUint32(wav, kSampleRate);
    writeUint32(wav, kSampleRate * channels * bytesPerSample);
    writeUint16(wav, channels * bytesPerSample);
    writeUint16(wav, bitsPerSample);
    wav.write("data", 4);
    writeUint32(wav, dataChunkSize);

    for (uint32_t i = 0; i < kTotalFrames; ++i) {
        writeUint16(wav, static_cast<uint16_t>(pcm));
    }
    return static_cast<bool>(wav);
}

template <typename Predicate>
bool waitUntil(Predicate&& predicate) {
    for (int i = 0; i < 200; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

float measuredMean(const std::vector<float>& interleaved) {
    double sum = 0.0;
    size_t count = 0;
    for (const float sample : interleaved) {
        sum += sample;
        ++count;
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

void requireNear(const char* label, float value, float expected, float tolerance) {
    const float diff = std::abs(value - expected);
    std::cout << label << "=" << value << " expected=" << expected << " diff=" << diff << "\n";
    assert(diff <= tolerance);
}

std::vector<float> renderMainEngineTrack() {
    auto source = std::make_shared<Aestra::Audio::AudioBuffer>();
    source->channels = 1;
    source->sampleRate = kSampleRate;
    source->numFrames = kTotalFrames;
    source->data.assign(kTotalFrames, kSourceSample);
    source->ready.store(true, std::memory_order_release);

    Aestra::Audio::AudioGraph graph;
    Aestra::Audio::TrackRenderState track;
    track.trackId = 1;
    track.trackIndex = 0;
    track.volume = 1.0f;
    track.pan = 0.0f;

    Aestra::Audio::ClipRenderState clip;
    clip.buffer = source;
    clip.audioData = source->data.data();
    clip.startSample = 0;
    clip.endSample = kTotalFrames;
    clip.totalFrames = kTotalFrames;
    clip.sourceSampleRate = kSampleRate;
    clip.channels = 1;
    clip.gain = 1.0f;
    track.clips.push_back(clip);
    graph.tracks.push_back(std::move(track));
    graph.timelineEndSample = kTotalFrames;

    Aestra::Audio::AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kBlockFrames, kChannels);
    assert(engine.initialize());
    engine.setGraph(graph);
    engine.setTransportPlaying(true);

    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    for (int i = 0; i < 4; ++i) {
        std::fill(out.begin(), out.end(), 0.0f);
        assert(engine.processBlock(out.data(), nullptr, kBlockFrames, 0.0) == 0);
    }
    return out;
}

std::vector<float> renderPreview(const fs::path& path) {
    Aestra::Audio::PreviewEngine preview;
    preview.setOutputSampleRate(kSampleRate);
    const auto result = preview.play(path.string(), 0.0f, 5.0);
    assert(result == Aestra::Audio::PreviewResult::Pending || result == Aestra::Audio::PreviewResult::Success);
    assert(waitUntil([&preview] { return preview.isBufferReady(); }));

    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    for (int i = 0; i < 4; ++i) {
        std::fill(out.begin(), out.end(), 0.0f);
        preview.processRealtime(out.data(), kBlockFrames, kChannels);
    }
    return out;
}

std::vector<float> renderAudition(const fs::path& path) {
    Aestra::Audio::AuditionEngine audition;
    audition.setSampleRate(kSampleRate);
    audition.setVolume(1.0f);
    audition.setDSPPreset(Aestra::Audio::AuditionDSPPreset::Bypass());
    audition.addToQueue(path.string());
    audition.play();
    assert(waitUntil([&audition] { return audition.getCurrentSource() != nullptr; }));

    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    audition.processBlock(out.data(), kBlockFrames, kChannels);
    return out;
}

std::vector<float> renderInputMonitoring() {
    Aestra::Audio::TrackManager tracks;
    tracks.setInputChannelCount(1);
    auto* channel = tracks.addChannel("Monitor");
    assert(channel != nullptr);
    channel->setArmed(true);
    channel->setMonitoringEnabled(true);
    channel->setInputChannelIndex(0);
    tracks.publishInputMonitoringSnapshot();

    std::vector<float> input(kBlockFrames, kSourceSample);
    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    tracks.mixInputMonitoring(input.data(), out.data(), kBlockFrames, kChannels);
    return out;
}

} // namespace

int main() {
    const fs::path path = fs::temp_directory_path() / "Aestra_preview_audition_gain_parity.wav";
    assert(writeMonoConstantWav(path));

    const auto mainOut = renderMainEngineTrack();
    const auto previewOut = renderPreview(path);
    const auto auditionOut = renderAudition(path);
    const auto monitorOut = renderInputMonitoring();

    const float mainMean = measuredMean(mainOut);
    const float previewMean = measuredMean(previewOut);
    const float auditionMean = measuredMean(auditionOut);
    const float monitorMean = measuredMean(monitorOut);

    requireNear("main", mainMean, kExpectedOutput, 0.0025f);
    requireNear("preview", previewMean, mainMean, 0.0035f);
    requireNear("audition", auditionMean, mainMean, 0.0035f);
    requireNear("monitor", monitorMean, mainMean, 0.0035f);

    std::error_code ec;
    fs::remove(path, ec);
    std::cout << "PreviewAuditionGainParityTest: PASS\n";
    return 0;
}
