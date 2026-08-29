// © 2026 Aestra Studios — All Rights Reserved.
// AestraTransientMaterialLab — deterministic quality baseline generator for
// com.Aestrastudios.transient / Aestra Transient (P-1, v0.7.1 sprint).
//
// Not registered with CTest by design (same posture as the comp/EQ material
// labs): it is a lab tool that regenerates
// labs/transient/quality/transient_material_baseline.md. Build ad hoc:
//
//   g++ -std=c++17 -O2 -I AestraAudio/include -I AestraCore/include \
//     Tests/AestraAudio/AestraTransientMaterialLab.cpp \
//     build-p1-transient/AestraAudio/libAestraAudioLinux.a \
//     build-p1-transient/AestraCore/libAestraCore.a -o /tmp/aestra-transient-lab

#include "Plugin/AestraTransient.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraTransient;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 256;
constexpr size_t kFrames = 48000;
constexpr float kPi = 3.14159265358979323846f;

struct CaseResult {
    std::string name;
    float peakIn = 0.0f;
    float rmsIn = 0.0f;
    float peakOut = 0.0f;
    float rmsOut = 0.0f;
    double onsetRatioDb = 0.0; // out/in energy over the first 15 ms
    double tailRatioDb = 0.0;  // out/in energy over 150..350 ms
    uint32_t clippingCount = 0;
    uint32_t nanInfCount = 0;
    bool bypassParityPass = false;
    bool sane = false;
};

void configure(AestraTransient& plugin, float attack, float sustain) {
    plugin.initialize(kSampleRate, kBlockSize);
    plugin.setParameter(AestraTransient::kAttack, attack);
    plugin.setParameter(AestraTransient::kSustain, sustain);
    plugin.setParameter(AestraTransient::kOutput, 0.5f); // 0 dB
    plugin.setParameter(AestraTransient::kMix, 1.0f);
    plugin.setParameter(AestraTransient::kBypass, 0.0f);
    plugin.activate();
}

float peak(const std::vector<float>& data) {
    float value = 0.0f;
    for (float sample : data)
        value = std::max(value, std::abs(sample));
    return value;
}

float rms(const std::vector<float>& data) {
    double sum = 0.0;
    for (float sample : data)
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    return data.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(data.size())));
}

double windowEnergy(const std::vector<float>& data, size_t begin, size_t end) {
    double sum = 0.0;
    for (size_t i = begin; i < end && i < data.size(); ++i) {
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    return sum;
}

double ratioDb(double out, double in) {
    if (in <= 0.0)
        return out <= 0.0 ? 0.0 : 99.0;
    return 10.0 * std::log10(std::max(out, 1.0e-30) / in);
}

std::vector<float> process(AestraTransient& plugin, const std::vector<float>& input) {
    std::vector<float> output(input.size(), 0.0f);
    for (size_t offset = 0; offset < input.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.size() - offset));
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {output.data() + offset};
        plugin.process(inputs, outputs, 1, 1, frames);
    }
    return output;
}

bool bypassParity(const std::vector<float>& input) {
    AestraTransient plugin;
    configure(plugin, 0.9f, 0.1f);
    plugin.setParameter(AestraTransient::kBypass, 1.0f);
    const auto output = process(plugin, input);
    for (size_t i = 0; i < input.size(); ++i) {
        if (std::isnan(input[i]) && std::isnan(output[i]))
            continue;
        if (input[i] != output[i])
            return false;
    }
    return true;
}

CaseResult analyze(const std::string& name, const std::vector<float>& input, float attack, float sustain) {
    AestraTransient plugin;
    configure(plugin, attack, sustain);
    const auto output = process(plugin, input);

    const size_t onsetEnd = std::min(input.size(), static_cast<size_t>(0.015 * kSampleRate));
    const size_t tailBegin = std::min(input.size(), static_cast<size_t>(0.150 * kSampleRate));
    const size_t tailEnd = std::min(input.size(), static_cast<size_t>(0.350 * kSampleRate));

    CaseResult result;
    result.name = name;
    result.peakIn = peak(input);
    result.rmsIn = rms(input);
    result.peakOut = peak(output);
    result.rmsOut = rms(output);
    result.onsetRatioDb = ratioDb(windowEnergy(output, 0, onsetEnd), windowEnergy(input, 0, onsetEnd));
    result.tailRatioDb = ratioDb(windowEnergy(output, tailBegin, tailEnd), windowEnergy(input, tailBegin, tailEnd));
    result.bypassParityPass = bypassParity(input);

    for (float sample : output) {
        if (!std::isfinite(sample))
            ++result.nanInfCount;
        if (std::isfinite(sample) && std::abs(sample) > 1.0f)
            ++result.clippingCount;
    }

    result.sane = result.bypassParityPass && result.nanInfCount == 0 && std::isfinite(result.peakOut) &&
                  std::isfinite(result.rmsOut) && result.peakOut <= 64.0f; // input cap 16 x max gain 4
    if (name == "silence") {
        result.sane = result.sane && result.peakOut == 0.0f && result.rmsOut == 0.0f;
    }
    return result;
}

std::vector<float> silence() {
    return std::vector<float>(kFrames, 0.0f);
}

std::vector<float> sineTone() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = std::sin(2.0f * kPi * 1000.0f * static_cast<float>(i) / static_cast<float>(kSampleRate)) * 0.5f;
    }
    return data;
}

std::vector<float> bassPulse() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float gate = std::fmod(t, 0.5f) < 0.18f ? 1.0f : 0.0f;
        const float env = gate * std::exp(-std::fmod(t, 0.5f) * 8.0f);
        data[i] = std::sin(2.0f * kPi * 72.0f * t) * 0.85f * env;
    }
    return data;
}

std::vector<float> snareTransient() {
    std::vector<float> data(kFrames, 0.0f);
    uint32_t seed = 0xA35A17u;
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float local = std::fmod(t, 0.5f);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (static_cast<float>((seed >> 8) & 0xFFFFu) / 32767.5f) - 1.0f;
        data[i] = noise * 0.95f * std::exp(-local * 55.0f);
    }
    return data;
}

std::vector<float> vocalSustain() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float vibrato = 1.0f + 0.012f * std::sin(2.0f * kPi * 5.2f * t);
        data[i] = 0.42f * std::sin(2.0f * kPi * 220.0f * vibrato * t) + 0.16f * std::sin(2.0f * kPi * 440.0f * t) +
                  0.04f * std::sin(2.0f * kPi * 880.0f * t);
    }
    return data;
}

std::vector<float> chordPad() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        data[i] = 0.24f * std::sin(2.0f * kPi * 261.63f * t) + 0.22f * std::sin(2.0f * kPi * 329.63f * t) +
                  0.22f * std::sin(2.0f * kPi * 392.00f * t);
    }
    return data;
}

std::vector<float> simpleMixBus() {
    auto mix = chordPad();
    auto bass = bassPulse();
    auto snare = snareTransient();
    for (size_t i = 0; i < mix.size(); ++i) {
        mix[i] = std::clamp(mix[i] * 0.55f + bass[i] * 0.45f + snare[i] * 0.18f, -0.98f, 0.98f);
    }
    return mix;
}

std::vector<float> extremeSweep() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float hz = 20.0f + 10000.0f * t;
        data[i] = std::sin(2.0f * kPi * hz * t) * (0.1f + 19.9f * t);
    }
    return data;
}

std::string fixed(float value, int precision = 3) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string signedFixed(double value, int precision = 1) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << (value >= 0 ? "+" : "") << value;
    return out.str();
}

bool writeMarkdown(const std::string& path, const std::vector<CaseResult>& results) {
    const auto parent = std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec && !std::filesystem::exists(parent)) {
        std::cerr << "cannot create " << parent << ": " << ec.message() << "\n";
        return false;
    }
    std::ofstream out(path);
    if (!out) {
        std::cerr << "cannot open " << path << "\n";
        return false;
    }
    out << "# Aestra Transient V1 Quality Baseline\n\n";
    out << "Generated by `AestraTransientMaterialLab` for `com.Aestrastudios.transient`"
        << " / Aestra Transient (zero latency, 0 samples reported).\n\n";
    out << "Configs: **attack focus** = attack +100%, sustain neutral, mix 100%, output 0 dB;"
        << " **sustain focus** = attack neutral, sustain +100%, mix 100%, output 0 dB."
        << " Onset ratio = output/input energy over the first 15 ms (this window includes the"
        << " initial rise from silence, so sustained materials still show a shaped onset);"
        << " tail ratio = output/input energy over 150..350 ms. Steady materials (sine, vocal,"
        << " pad) are expected near +0.0 dB in the un-focused window because the fast and slow"
        << " envelopes converge in steady state.\n\n";
    out << "| Config | Material | Peak In | RMS In | Peak Out | RMS Out | Onset | Tail | Clips | NaN/Inf | Bypass | "
           "Sane |\n";
    out << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
    for (const auto& result : results) {
        out << "| " << (result.name.rfind("attack/", 0) == 0 ? "attack focus" : "sustain focus") << " "
            << "| " << result.name.substr(result.name.find('/') + 1) << " " << "| " << fixed(result.peakIn) << " | "
            << fixed(result.rmsIn) << " | " << fixed(result.peakOut) << " | " << fixed(result.rmsOut) << " | "
            << signedFixed(result.onsetRatioDb) << " dB | " << signedFixed(result.tailRatioDb) << " dB | "
            << result.clippingCount << " | " << result.nanInfCount << " | "
            << (result.bypassParityPass ? "pass" : "FAIL") << " | " << (result.sane ? "yes" : "NO") << " |\n";
    }
    out << "\nThe extreme sweep intentionally includes above-0-dBFS stress material; input is"
        << " clamped by the plugin's input sanitizer at +/-16.\n";
    out.flush();
    if (!out.good()) {
        std::cerr << "write failed while emitting " << path << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    struct Config {
        const char* label;
        float attack;
        float sustain;
    };
    const Config configs[] = {
        {"attack", 1.0f, 0.5f},
        {"sustain", 0.5f, 1.0f},
    };

    struct Material {
        const char* name;
        std::vector<float> (*fn)();
    };
    const Material materials[] = {
        {"silence", silence},
        {"sine_tone", sineTone},
        {"bass_pulse", bassPulse},
        {"snare_transient", snareTransient},
        {"vocal_sustain", vocalSustain},
        {"chord_pad", chordPad},
        {"simple_mix_bus", simpleMixBus},
        {"extreme_sweep", extremeSweep},
    };

    std::vector<CaseResult> results;
    for (const auto& config : configs) {
        for (const auto& material : materials) {
            const std::string name = std::string(config.label) + "/" + material.name;
            results.push_back(analyze(name, material.fn(), config.attack, config.sustain));
        }
    }

    const std::string path = "labs/transient/quality/transient_material_baseline.md";
    if (!writeMarkdown(path, results)) {
        return 1;
    }
    std::cout << "wrote " << path << " (" << results.size() << " cases)\n";

    int failures = 0;
    for (const auto& result : results) {
        if (!result.bypassParityPass || result.nanInfCount != 0 || !result.sane) {
            std::cout << "[FAIL] " << result.name << "\n";
            ++failures;
        }
    }
    if (failures > 0) {
        std::cout << failures << " case(s) failed sanity\n";
        return 1;
    }
    std::cout << "All material cases sane\n";
    return 0;
}
