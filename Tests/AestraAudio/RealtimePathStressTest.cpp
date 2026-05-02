// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "AudioEngine.h"
#include "Models/TrackManager.h"
#include "Playback/PreviewEngine.h"
#include "Plugin/SamplerPlugin.h"
#include "PluginHost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>
#include <vector>

using namespace Aestra::Audio;

namespace {

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

bool writeTestWav(const std::string& path, uint32_t sampleRate, uint32_t frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }

    constexpr uint16_t channels = 1;
    constexpr uint16_t bitsPerSample = 16;
    const uint32_t dataBytes = frames * channels * (bitsPerSample / 8);
    out.write("RIFF", 4);
    writeLe32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    writeLe32(out, 16);
    writeLe16(out, 1);
    writeLe16(out, channels);
    writeLe32(out, sampleRate);
    writeLe32(out, sampleRate * channels * (bitsPerSample / 8));
    writeLe16(out, channels * (bitsPerSample / 8));
    writeLe16(out, bitsPerSample);
    out.write("data", 4);
    writeLe32(out, dataBytes);

    constexpr double twoPi = 6.28318530717958647692;
    for (uint32_t i = 0; i < frames; ++i) {
        const float sample = std::sin(twoPi * 220.0 * static_cast<double>(i) / static_cast<double>(sampleRate)) * 0.25f;
        const auto pcm = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        writeLe16(out, static_cast<uint16_t>(pcm));
    }

    return true;
}

} // namespace

int main() {
    constexpr uint32_t kSampleRate = 48000;
    constexpr uint32_t kFrames = 64;
    constexpr uint32_t kChannels = 2;

    // Use a guaranteed-writable temp directory instead of hardcoded /tmp/
    std::error_code ec;
    auto tmpDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::cerr << "failed to resolve temp_directory_path: " << ec.message() << "\n";
        std::cerr << "cwd: " << std::filesystem::current_path(ec).string() << "\n";
        return 1;
    }
    auto wavPath = tmpDir / "aestra_realtime_path_stress.wav";

    // Ensure parent directory exists (handles CI environments with nested temp dirs)
    if (!tmpDir.empty() && !std::filesystem::exists(tmpDir, ec)) {
        std::filesystem::create_directories(tmpDir, ec);
        if (ec) {
            std::cerr << "failed to create temp dir: " << tmpDir.string() << ": " << ec.message() << "\n";
            return 1;
        }
    }

    if (!writeTestWav(wavPath.string(), kSampleRate, kSampleRate / 2)) {
        std::cerr << "failed to write test wav\n";
        std::cerr << "  absolute path: " << std::filesystem::absolute(wavPath).string() << "\n";
        std::cerr << "  cwd: " << std::filesystem::current_path(ec).string() << "\n";
        std::cerr << "  WAV write is required for sampler/preview load — aborting test\n";
        return 1;
    }

    AudioEngine engine;
    engine.setSampleRate(kSampleRate);
    engine.setBufferConfig(kFrames, kChannels);
    engine.setPatternPlaybackMode(true, 4.0);

    TrackManager tracks;
    tracks.setInputChannelCount(1);
    auto* channel = tracks.addChannel("Input Monitor");
    channel->setArmed(true);
    channel->setMonitoringEnabled(true);
    channel->setInputChannelIndex(0);
    tracks.publishInputMonitoringSnapshot();

    PreviewEngine preview;
    preview.setOutputSampleRate(kSampleRate);
    const auto previewResult = preview.play(wavPath.string(), -9.0f, 0.25);
    if (previewResult == PreviewResult::Failed) {
        std::cerr << "preview failed to start\n";
        return 1;
    }

    Plugins::SamplerPlugin sampler;
    if (!sampler.initialize(kSampleRate, kFrames) || !sampler.loadSample(wavPath.string())) {
        std::cerr << "sampler failed to load test wav\n";
        return 1;
    }
    sampler.activate();

    std::vector<float> engineOut(kFrames * kChannels, 0.0f);
    std::vector<float> monitorOut(kFrames * kChannels, 0.0f);
    std::vector<float> input(kFrames, 0.05f);
    std::vector<float> samplerLeft(kFrames, 0.0f);
    std::vector<float> samplerRight(kFrames, 0.0f);
    float* samplerOutputs[] = {samplerLeft.data(), samplerRight.data()};

    MidiBuffer midi;
    midi.addNoteOn(1, 60, 100, 0);

    const auto deadline = std::chrono::nanoseconds((static_cast<uint64_t>(kFrames) * 1000000000ull) / kSampleRate);
    uint64_t localOverruns = 0;

    for (int block = 0; block < 256; ++block) {
        if ((block % 7) == 0) {
            engine.setTransportPlaying(false);
            engine.setTransportPlaying(true);
        }

        std::fill(engineOut.begin(), engineOut.end(), 0.0f);
        std::fill(monitorOut.begin(), monitorOut.end(), 0.0f);

        const auto start = std::chrono::steady_clock::now();
        engine.processBlock(engineOut.data(), nullptr, kFrames, 0.0);
        tracks.mixInputMonitoring(input.data(), monitorOut.data(), kFrames, kChannels);
        preview.process(engineOut.data(), kFrames);
        sampler.process(nullptr, samplerOutputs, 0, 2, kFrames, block == 0 ? &midi : nullptr, nullptr);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        if (elapsed > deadline) {
            ++localOverruns;
        }
    }

    const auto& tel = engine.telemetry();
    std::cout << "localOverruns=" << localOverruns << "\n";
    std::cout << "engineOverruns=" << tel.getOverruns() << "\n";
    std::cout << "rtLockViolations=" << tel.getRtLockViolations() << "\n";
    std::cout << "rtLogViolations=" << tel.getRtLogViolations() << "\n";

    if (tel.getRtLockViolations() != 0 || tel.getRtLogViolations() != 0) {
        return 1;
    }

    return 0;
}
