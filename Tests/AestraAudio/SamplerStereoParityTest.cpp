// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/SamplerPlugin.h"
#include "PluginHost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

bool writeMonoWav(const std::filesystem::path& path, uint32_t sampleRate, uint32_t frames) {
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
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        const float sample = static_cast<float>((std::sin(twoPi * 220.0 * t) * 0.22) +
                                                (std::sin(twoPi * 331.0 * t) * 0.08));
        const auto pcm = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767.0f);
        writeLe16(out, static_cast<uint16_t>(pcm));
    }

    return true;
}

bool renderStackedNotes(const std::filesystem::path& wavPath, uint8_t firstNote, uint8_t secondNote,
                        std::string* error) {
    constexpr uint32_t sampleRate = 48000;
    constexpr uint32_t blockFrames = 256;
    constexpr int blocks = 16;
    constexpr float epsilon = 1.0e-7f;

    Plugins::SamplerPlugin sampler;
    if (!sampler.initialize(sampleRate, blockFrames)) {
        *error = "sampler initialize failed";
        return false;
    }
    if (!sampler.loadSample(wavPath.string())) {
        *error = "sampler failed to load mono wav";
        return false;
    }
    sampler.setRootMidiNote(60);
    sampler.setMaxVoices(8);
    sampler.setEnvelope(0.001f, 0.001f, 1.0f, 0.05f);
    sampler.activate();

    std::vector<float> left(blockFrames, 0.0f);
    std::vector<float> right(blockFrames, 0.0f);
    float* outputs[] = {left.data(), right.data()};

    MidiBuffer midi;
    midi.addNoteOn(1, firstNote, 110, 0);
    midi.addNoteOn(1, secondNote, 110, 0);

    float maxAbsDiff = 0.0f;
    float maxSignal = 0.0f;

    for (int block = 0; block < blocks; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);

        sampler.process(nullptr, outputs, 0, 2, blockFrames, block == 0 ? &midi : nullptr, nullptr);

        for (uint32_t i = 0; i < blockFrames; ++i) {
            maxAbsDiff = std::max(maxAbsDiff, std::abs(left[i] - right[i]));
            maxSignal = std::max(maxSignal, std::max(std::abs(left[i]), std::abs(right[i])));
        }
    }

    if (maxSignal <= epsilon) {
        *error = "sampler output was silent";
        return false;
    }

    if (maxAbsDiff > epsilon) {
        *error = "mono sampler stack diverged between channels, max diff=" + std::to_string(maxAbsDiff);
        return false;
    }

    return true;
}

} // namespace

int main() {
    constexpr uint32_t sampleRate = 48000;

    std::error_code ec;
    const auto tmpDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::cerr << "failed to resolve temp directory: " << ec.message() << "\n";
        return 1;
    }

    const auto uniqueId =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
        std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    const auto wavPath = tmpDir / ("aestra_sampler_stereo_parity_mono_" + uniqueId + ".wav");
    if (!writeMonoWav(wavPath, sampleRate, sampleRate)) {
        std::cerr << "failed to write mono wav: " << wavPath << "\n";
        return 1;
    }

    std::string error;
    if (!renderStackedNotes(wavPath, 60, 60, &error)) {
        std::cerr << "same-pitch stack failed: " << error << "\n";
        std::filesystem::remove(wavPath, ec);
        return 1;
    }

    if (!renderStackedNotes(wavPath, 60, 72, &error)) {
        std::cerr << "different-pitch stack failed: " << error << "\n";
        std::filesystem::remove(wavPath, ec);
        return 1;
    }

    std::filesystem::remove(wavPath, ec);
    std::cout << "mono sampler stacked-note L/R parity passed\n";
    return 0;
}
