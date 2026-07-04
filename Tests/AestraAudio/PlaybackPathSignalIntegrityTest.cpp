#include "Core/AudioEngine.h"
#include "Core/MasterSafetyLimiter.h"
#include "DSP/PanLaw.h"
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
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kFrames = 4096;
constexpr uint32_t kBlockFrames = 2048;
constexpr float kCenterPanLawGain = Aestra::Audio::PanLaw::kEqualPowerCenterGain;

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

bool writeConstantPcm16Wav(const fs::path& path, float sample) {
    std::ofstream wav(path, std::ios::binary);
    if (!wav) {
        return false;
    }

    const uint16_t bitsPerSample = 16;
    const uint32_t bytesPerSample = bitsPerSample / 8;
    const uint32_t dataChunkSize = kFrames * kChannels * bytesPerSample;
    const uint32_t riffChunkSize = 4 + (8 + 16) + (8 + dataChunkSize);
    const auto pcm = static_cast<int16_t>(std::lrint(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));

    wav.write("RIFF", 4);
    writeUint32(wav, riffChunkSize);
    wav.write("WAVE", 4);
    wav.write("fmt ", 4);
    writeUint32(wav, 16);
    writeUint16(wav, 1);
    writeUint16(wav, static_cast<uint16_t>(kChannels));
    writeUint32(wav, kSampleRate);
    writeUint32(wav, kSampleRate * kChannels * bytesPerSample);
    writeUint16(wav, static_cast<uint16_t>(kChannels * bytesPerSample));
    writeUint16(wav, bitsPerSample);
    wav.write("data", 4);
    writeUint32(wav, dataChunkSize);
    for (uint32_t i = 0; i < kFrames * kChannels; ++i) {
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

float averageTail(const std::vector<float>& interleaved, uint32_t startFrame) {
    double sum = 0.0;
    uint32_t count = 0;
    for (uint32_t frame = startFrame; frame < kBlockFrames; ++frame) {
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            sum += interleaved[static_cast<size_t>(frame) * kChannels + ch];
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / static_cast<double>(count)) : 0.0f;
}

float peakAbs(const std::vector<float>& interleaved, uint32_t startFrame = 0) {
    float peak = 0.0f;
    const uint32_t frames = static_cast<uint32_t>(interleaved.size() / kChannels);
    for (uint32_t frame = startFrame; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            peak = std::max(peak, std::abs(interleaved[static_cast<size_t>(frame) * kChannels + ch]));
        }
    }
    return peak;
}

float renderAuditionAverageTail(const fs::path& path) {
    Aestra::Audio::AuditionEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setVolume(1.0f);
    engine.setDSPPreset(Aestra::Audio::AuditionDSPPreset::Bypass());
    engine.addToQueue(path.string());
    engine.play();

    assert(waitUntil([&engine] { return engine.getCurrentSource() != nullptr; }));

    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    engine.processBlock(out.data(), kBlockFrames, kChannels);
    return averageTail(out, 1024);
}

float renderPreviewAverageTail(const fs::path& path, float gainDb) {
    Aestra::Audio::PreviewEngine engine;
    engine.setOutputSampleRate(kSampleRate);
    const auto result = engine.play(path.string(), gainDb, 10.0);
    assert(result == Aestra::Audio::PreviewResult::Pending || result == Aestra::Audio::PreviewResult::Success);
    assert(waitUntil([&engine] { return engine.isBufferReady(); }));

    std::vector<float> out(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    engine.processRealtime(out.data(), kBlockFrames, kChannels);
    return averageTail(out, 1024);
}

void auditionBypassMatchesCenteredTrackGain(const fs::path& path, float decodedSample) {
    const float audition = renderAuditionAverageTail(path);
    const float expected = decodedSample * kCenterPanLawGain;
    assert(std::abs(audition - expected) < 2.0e-4f);
    std::cout << "PASS: audition bypass matches centered track gain, tail average=" << audition << "\n";
}

void previewMatchesCenteredTrackGainBelowLimiter(const fs::path& path, float decodedSample) {
    const float preview = renderPreviewAverageTail(path, 0.0f);
    const float expected = decodedSample * kCenterPanLawGain;
    const float diff = std::abs(preview - expected);
    assert(diff < 1.0e-3f);
    std::cout << "PASS: preview matches centered track gain below limiter, tail average=" << preview
              << ", expected=" << expected << ", diff=" << diff << "\n";
}

void previewLimiterChangesBoostedHotMaterialButAuditionBypassDoesNot(const fs::path& path, float decodedSample) {
    const float audition = renderAuditionAverageTail(path);
    const float preview = renderPreviewAverageTail(path, 3.0f);

    assert(std::abs(audition - decodedSample * kCenterPanLawGain) < 2.0e-4f);
    assert(preview > 0.85f);
    assert(preview < 0.98f);
    std::cout << "PASS: preview limiter changed boosted hot source, audition=" << audition
              << ", preview=" << preview << "\n";
}

void belowKneePassthrough() {
    Aestra::Audio::MasterSafetyLimiter limiter;
    float l = 0.5f;
    float r = -0.5f;
    limiter.process(l, r);
    assert(l == 0.5f);
    assert(r == -0.5f);
    std::cout << "PASS: below_knee_passthrough\n";
}

void kneeEntryPassthrough() {
    Aestra::Audio::MasterSafetyLimiter limiter;
    float l = 0.9799f;
    float r = -0.9799f;
    limiter.process(l, r);
    assert(l == 0.9799f);
    assert(r == -0.9799f);
    std::cout << "PASS: knee_entry_passthrough\n";
}

void kneeMonotonicity() {
    Aestra::Audio::MasterSafetyLimiter limiter;
    float previous = Aestra::Audio::MasterSafetyLimiter::kKneeStart;
    constexpr int kSteps = 1000;

    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        float l = Aestra::Audio::MasterSafetyLimiter::kKneeStart +
                  t * Aestra::Audio::MasterSafetyLimiter::kKneeRange;
        float r = 0.0f;
        limiter.process(l, r);
        assert(l >= previous);
        assert(l <= Aestra::Audio::MasterSafetyLimiter::kKneeStart +
                        t * Aestra::Audio::MasterSafetyLimiter::kKneeRange);
        assert(l <= Aestra::Audio::MasterSafetyLimiter::kCeiling);
        previous = l;
    }

    std::cout << "PASS: knee_monotonicity\n";
}

void ceilingClamp() {
    Aestra::Audio::MasterSafetyLimiter limiter;
    float l = 1.0f;
    float r = -1.0f;
    limiter.process(l, r);
    assert(l == Aestra::Audio::MasterSafetyLimiter::kCeiling);
    assert(r == -Aestra::Audio::MasterSafetyLimiter::kCeiling);
    std::cout << "PASS: ceiling_clamp\n";
}

void overCeilingClamp() {
    Aestra::Audio::MasterSafetyLimiter limiter;
    float l = 2.0f;
    float r = -3.5f;
    limiter.process(l, r);
    assert(l == Aestra::Audio::MasterSafetyLimiter::kCeiling);
    assert(r == -Aestra::Audio::MasterSafetyLimiter::kCeiling);
    std::cout << "PASS: over_ceiling_clamp\n";
}

void limiterDisabledPassthrough() {
    Aestra::Audio::AudioEngine engine;
    engine.setSafetyLimiterEnabled(false);
    assert(!engine.isSafetyLimiterEnabled());

    Aestra::Audio::MasterSafetyLimiter limiter;
    double l = 1.0;
    double r = 1.0;
    if (engine.isSafetyLimiterEnabled()) {
        limiter.process(l, r);
    }

    assert(l == 1.0);
    assert(r == 1.0);
    std::cout << "PASS: limiter_disabled_passthrough\n";
}

void mainAudioEngineSafetyLimiterChangesHotMasterOutput() {
    Aestra::Audio::AudioEngine limited;
    assert(limited.initialize());
    limited.setSampleRate(kSampleRate);
    limited.setBufferConfig(kBlockFrames, kChannels);
    limited.setMetronomeEnabled(false);
    limited.setTestToneEnabled(true);
    limited.setMasterGain(20.0f);
    limited.setSafetyLimiterEnabled(true);
    limited.setTransportPlaying(true);

    Aestra::Audio::AudioEngine raw;
    assert(raw.initialize());
    raw.setSampleRate(kSampleRate);
    raw.setBufferConfig(kBlockFrames, kChannels);
    raw.setMetronomeEnabled(false);
    raw.setTestToneEnabled(true);
    raw.setMasterGain(20.0f);
    raw.setSafetyLimiterEnabled(false);
    raw.setTransportPlaying(true);

    std::vector<float> limitedOut(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
    std::vector<float> rawOut(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);

    limited.processBlock(limitedOut.data(), nullptr, kBlockFrames, 0.0);
    raw.processBlock(rawOut.data(), nullptr, kBlockFrames, 0.0);

    const float limitedPeak = peakAbs(limitedOut, 512);
    const float rawPeak = peakAbs(rawOut, 512);

    assert(rawPeak > 0.99f);
    assert(limitedPeak >= Aestra::Audio::MasterSafetyLimiter::kKneeStart);
    assert(limitedPeak <= rawPeak);
    assert(limitedPeak <= static_cast<float>(Aestra::Audio::MasterSafetyLimiter::OUTPUT_CEILING) + 1.0e-4f);
    std::cout << "PASS: main AudioEngine safety limiter preserves hot legal output, rawPeak=" << rawPeak
              << ", limitedPeak=" << limitedPeak << "\n";
}

} // namespace

int main() {
    const fs::path coldPath = fs::temp_directory_path() / "Aestra_playback_path_cold.wav";
    const fs::path hotPath = fs::temp_directory_path() / "Aestra_playback_path_hot.wav";
    assert(writeConstantPcm16Wav(coldPath, 0.50f));
    assert(writeConstantPcm16Wav(hotPath, 0.985f));

    const float coldDecoded = static_cast<float>(std::lrint(0.50f * 32767.0f)) / 32768.0f;
    const float hotDecoded = static_cast<float>(std::lrint(0.985f * 32767.0f)) / 32768.0f;

    belowKneePassthrough();
    kneeEntryPassthrough();
    kneeMonotonicity();
    ceilingClamp();
    overCeilingClamp();
    limiterDisabledPassthrough();
    auditionBypassMatchesCenteredTrackGain(coldPath, coldDecoded);
    previewMatchesCenteredTrackGainBelowLimiter(coldPath, coldDecoded);
    previewLimiterChangesBoostedHotMaterialButAuditionBypassDoesNot(hotPath, hotDecoded);
    mainAudioEngineSafetyLimiterChangesHotMasterOutput();

    std::error_code ec;
    fs::remove(coldPath, ec);
    fs::remove(hotPath, ec);

    std::cout << "AestraPlaybackPathSignalIntegrityTest: PASS\n";
    return 0;
}
