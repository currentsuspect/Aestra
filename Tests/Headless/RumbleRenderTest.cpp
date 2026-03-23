// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RumbleRenderTest — Headless terminal-first render harness for Aestra Rumble MVP

#include "Plugin/PluginHost.h"
#include "RumbleInstance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::MidiBuffer;
using Aestra::Plugins::RumbleInstance;

namespace {
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt_[4] = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;
    uint16_t audioFormat = 3;
    uint16_t numChannels = 2;
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 0;
    uint16_t blockAlign = 0;
    uint16_t bitsPerSample = 32;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};

bool writeWav(const std::string& path, const std::vector<float>& interleaved, uint32_t sampleRate, uint16_t channels) {
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return false;

    WavHeader header;
    header.sampleRate = sampleRate;
    header.numChannels = channels;
    header.bitsPerSample = 32;
    header.audioFormat = 3;
    header.blockAlign = channels * sizeof(float);
    header.byteRate = sampleRate * header.blockAlign;
    header.dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(float));
    header.fileSize = sizeof(WavHeader) - 8 + header.dataSize;

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(interleaved.data()), header.dataSize);
    return file.good();
}

struct RenderStats {
    float peak = 0.0f;
    double rms = 0.0;
    double tailRms = 0.0;
    bool hasNaN = false;
};

struct Preset {
    std::string name;
    float decay;
    float drive;
    float tone;
    float outputGain;
    uint8_t note;
};

RenderStats analyze(const std::vector<float>& samples) {
    RenderStats stats;
    if (samples.empty())
        return stats;

    long double sumSquares = 0.0;
    for (float s : samples) {
        if (!std::isfinite(s))
            stats.hasNaN = true;
        stats.peak = std::max(stats.peak, std::abs(s));
        sumSquares += static_cast<long double>(s) * static_cast<long double>(s);
    }

    stats.rms = std::sqrt(sumSquares / static_cast<long double>(samples.size()));

    const size_t tailStart = samples.size() > 96000 ? (samples.size() - 96000) : (samples.size() / 2);
    long double tailSquares = 0.0;
    for (size_t i = tailStart; i < samples.size(); ++i) {
        const float s = samples[i];
        tailSquares += static_cast<long double>(s) * static_cast<long double>(s);
    }
    const size_t tailCount = samples.size() - tailStart;
    if (tailCount > 0) {
        stats.tailRms = std::sqrt(tailSquares / static_cast<long double>(tailCount));
    }
    return stats;
}

bool renderPreset(const Preset& preset, const std::string& outputPath, RenderStats& outStats) {
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockSize = 512;
    constexpr double durationSeconds = 3.0;
    const uint32_t totalFrames = static_cast<uint32_t>(durationSeconds * sampleRate);
    const uint32_t totalBlocks = (totalFrames + blockSize - 1) / blockSize;

    RumbleInstance rumble;
    if (!rumble.initialize(sampleRate, blockSize)) {
        std::cerr << "Failed to initialize Rumble for preset " << preset.name << "\n";
        return false;
    }
    rumble.activate();
    rumble.setParameter(0, preset.decay);
    rumble.setParameter(1, preset.drive);
    rumble.setParameter(2, preset.tone);
    rumble.setParameter(3, preset.outputGain);

    std::vector<float> left(blockSize, 0.0f);
    std::vector<float> right(blockSize, 0.0f);
    float* outputs[2] = {left.data(), right.data()};
    std::vector<float> interleaved;
    interleaved.reserve(static_cast<size_t>(totalFrames) * 2);

    bool noteOnSent = false;
    bool noteOffSent = false;
    uint32_t renderedFrames = 0;

    for (uint32_t block = 0; block < totalBlocks; ++block) {
        MidiBuffer midi;

        if (!noteOnSent) {
            midi.addNoteOn(1, preset.note, 110, 0);
            noteOnSent = true;
        }

        const uint32_t noteOffFrame = static_cast<uint32_t>(sampleRate * 1.5);
        if (!noteOffSent && renderedFrames <= noteOffFrame && noteOffFrame < renderedFrames + blockSize) {
            midi.addNoteOff(1, preset.note, 0, noteOffFrame - renderedFrames);
            noteOffSent = true;
        }

        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        const uint32_t framesThisBlock = std::min(blockSize, totalFrames - renderedFrames);
        rumble.process(nullptr, outputs, 0, 2, framesThisBlock, &midi, nullptr);

        for (uint32_t i = 0; i < framesThisBlock; ++i) {
            interleaved.push_back(left[i]);
            interleaved.push_back(right[i]);
        }

        renderedFrames += framesThisBlock;
    }

    rumble.deactivate();
    rumble.shutdown();

    const auto stats = analyze(interleaved);
    outStats = stats;
    if (stats.hasNaN) {
        std::cerr << preset.name << ": render contains NaN/Inf samples\n";
        return false;
    }
    if (stats.peak <= 1.0e-5f) {
        std::cerr << preset.name << ": render is effectively silent\n";
        return false;
    }
    if (stats.peak > 0.98f) {
        std::cerr << preset.name << ": render peak too high: " << stats.peak << " (expected <= 0.98)\n";
        return false;
    }
    if (stats.tailRms >= stats.rms) {
        std::cerr << preset.name << ": tail failed to decay: tail RMS " << stats.tailRms
                  << " >= full RMS " << stats.rms << "\n";
        return false;
    }
    if (!writeWav(outputPath, interleaved, sampleRate, 2)) {
        std::cerr << preset.name << ": failed to write WAV: " << outputPath << "\n";
        return false;
    }

    std::cout << preset.name << " render complete\n";
    std::cout << "  Output: " << outputPath << "\n";
    std::cout << "  Peak: " << stats.peak << "\n";
    std::cout << "  RMS: " << stats.rms << "\n";
    std::cout << "  Tail RMS: " << stats.tailRms << "\n";
    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    const std::string outputBase = (argc >= 2) ? argv[1] : "rumble_test";

    const std::vector<Preset> presets = {
        {"clean", 0.48f, 0.04f, 0.22f, 0.58f, 36},
        {"balanced", 0.58f, 0.10f, 0.30f, 0.56f, 36},
        {"driven", 0.64f, 0.22f, 0.42f, 0.52f, 34},
    };

    std::vector<RenderStats> stats;
    stats.reserve(presets.size());

    for (const auto& preset : presets) {
        const std::string outputPath = outputBase + "_" + preset.name + ".wav";
        RenderStats presetStats;
        if (!renderPreset(preset, outputPath, presetStats)) {
            return 1;
        }
        stats.push_back(presetStats);
    }

    if (stats.size() >= 3) {
        const double cleanVsDrivenRmsDelta = std::abs(stats[0].rms - stats[2].rms);
        const double cleanVsDrivenPeakDelta = std::abs(stats[0].peak - stats[2].peak);
        if (cleanVsDrivenRmsDelta < 0.01) {
            std::cerr << "Preset differentiation too small: clean/driven RMS delta = " << cleanVsDrivenRmsDelta << "\n";
            return 1;
        }
        if (cleanVsDrivenPeakDelta < 0.02f) {
            std::cerr << "Preset differentiation too small: clean/driven peak delta = " << cleanVsDrivenPeakDelta << "\n";
            return 1;
        }
    }

    std::cout << "All Rumble presets rendered successfully\n";
    return 0;
}
