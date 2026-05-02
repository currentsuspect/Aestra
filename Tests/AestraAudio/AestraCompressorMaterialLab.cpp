// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompressorMaterialLab — deterministic quality baseline generator.

#include "Plugin/AestraComp.h"

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

using Aestra::Audio::Plugins::AestraComp;

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
    float maxGainReductionDb = 0.0f;
    float averageGainReductionDb = 0.0f;
    uint32_t clippingCount = 0;
    uint32_t nanInfCount = 0;
    bool bypassParityPass = false;
    float maxAbsSample = 0.0f;
    bool sane = false;
};

float thresholdNorm(float db) { return (db + 60.0f) / 60.0f; }
float ratioNorm(float ratio) { return (ratio - 1.0f) / 19.0f; }
float attackNorm(float ms) { return (ms - 0.1f) / 99.9f; }
float releaseNorm(float ms) { return (ms - 10.0f) / 990.0f; }
float gainNorm(float db) { return (db + 24.0f) / 48.0f; }

void configureBaseline(AestraComp& comp) {
    comp.initialize(kSampleRate, kBlockSize);
    comp.setParameter(AestraComp::kThreshold, thresholdNorm(-24.0f));
    comp.setParameter(AestraComp::kRatio, ratioNorm(4.0f));
    comp.setParameter(AestraComp::kAttack, attackNorm(10.0f));
    comp.setParameter(AestraComp::kRelease, releaseNorm(120.0f));
    comp.setParameter(AestraComp::kKnee, 6.0f / 24.0f);
    comp.setParameter(AestraComp::kMakeup, 3.0f / 24.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kBypass, 0.0f);
    comp.setParameter(AestraComp::kInputGain, gainNorm(0.0f));
    comp.setParameter(AestraComp::kOutputGain, gainNorm(0.0f));
    comp.setParameter(AestraComp::kDetectorHPF, 0.0f);
    comp.activate();
}

float peak(const std::vector<float>& data) {
    float value = 0.0f;
    for (float sample : data) value = std::max(value, std::abs(sample));
    return value;
}

float rms(const std::vector<float>& data) {
    double sum = 0.0;
    for (float sample : data) sum += static_cast<double>(sample) * static_cast<double>(sample);
    return data.empty() ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(data.size())));
}

std::vector<float> process(AestraComp& comp, const std::vector<float>& input, float& maxGR, float& averageGR) {
    std::vector<float> output(input.size(), 0.0f);
    double grSum = 0.0;
    uint32_t blockCount = 0;
    maxGR = 0.0f;

    for (size_t offset = 0; offset < input.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.size() - offset));
        const float* inputs[] = {input.data() + offset};
        float* outputs[] = {output.data() + offset};
        comp.process(inputs, outputs, 1, 1, frames);
        const float blockGR = comp.getCurrentGainReductionDb();
        maxGR = std::max(maxGR, blockGR);
        grSum += blockGR;
        ++blockCount;
    }

    averageGR = blockCount == 0 ? 0.0f : static_cast<float>(grSum / static_cast<double>(blockCount));
    return output;
}

bool bypassParity(const std::vector<float>& input) {
    AestraComp comp;
    configureBaseline(comp);
    comp.setParameter(AestraComp::kBypass, 1.0f);
    float maxGR = 0.0f;
    float avgGR = 0.0f;
    const auto output = process(comp, input, maxGR, avgGR);
    for (size_t i = 0; i < input.size(); ++i) {
        if (std::isnan(input[i]) && std::isnan(output[i])) continue;
        if (input[i] != output[i]) return false;
    }
    return true;
}

CaseResult analyze(const std::string& name, const std::vector<float>& input) {
    AestraComp comp;
    configureBaseline(comp);
    float maxGR = 0.0f;
    float avgGR = 0.0f;
    const auto output = process(comp, input, maxGR, avgGR);

    CaseResult result;
    result.name = name;
    result.peakIn = peak(input);
    result.rmsIn = rms(input);
    result.peakOut = peak(output);
    result.rmsOut = rms(output);
    result.maxGainReductionDb = maxGR;
    result.averageGainReductionDb = avgGR;
    result.bypassParityPass = bypassParity(input);
    result.maxAbsSample = result.peakOut;

    for (float sample : output) {
        if (!std::isfinite(sample)) ++result.nanInfCount;
        if (std::isfinite(sample) && std::abs(sample) > 1.0f) ++result.clippingCount;
    }

    result.sane = result.bypassParityPass && result.nanInfCount == 0 && std::isfinite(result.peakOut) &&
                  std::isfinite(result.rmsOut) && result.maxAbsSample <= 16.0f;
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
        data[i] = 0.42f * std::sin(2.0f * kPi * 220.0f * vibrato * t) +
                  0.16f * std::sin(2.0f * kPi * 440.0f * t) +
                  0.04f * std::sin(2.0f * kPi * 880.0f * t);
    }
    return data;
}

std::vector<float> chordPad() {
    std::vector<float> data(kFrames, 0.0f);
    for (size_t i = 0; i < data.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        data[i] = 0.24f * std::sin(2.0f * kPi * 261.63f * t) +
                  0.22f * std::sin(2.0f * kPi * 329.63f * t) +
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

void writeJson(const std::string& path, const std::vector<CaseResult>& results) {
    std::ofstream out(path);
    out << "{\n";
    out << "  \"plugin_id\": \"com.Aestrastudios.comp\",\n";
    out << "  \"plugin_name\": \"Aestra Compressor\",\n";
    out << "  \"baseline\": \"compressor-v1\",\n";
    out << "  \"sample_rate_hz\": 48000,\n";
    out << "  \"block_size\": 256,\n";
    out << "  \"settings\": {\n";
    out << "    \"threshold_db\": -24.0,\n";
    out << "    \"ratio\": 4.0,\n";
    out << "    \"attack_ms\": 10.0,\n";
    out << "    \"release_ms\": 120.0,\n";
    out << "    \"knee_db\": 6.0,\n";
    out << "    \"makeup_gain_db\": 3.0,\n";
    out << "    \"mix_percent\": 100.0,\n";
    out << "    \"input_gain_db\": 0.0,\n";
    out << "    \"output_gain_db\": 0.0,\n";
    out << "    \"detector_hpf\": \"off\",\n";
    out << "    \"bypass\": false\n";
    out << "  },\n";
    out << "  \"materials\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n";
        out << "      \"name\": \"" << r.name << "\",\n";
        out << "      \"peak_in\": " << fixed(r.peakIn, 6) << ",\n";
        out << "      \"rms_in\": " << fixed(r.rmsIn, 6) << ",\n";
        out << "      \"peak_out\": " << fixed(r.peakOut, 6) << ",\n";
        out << "      \"rms_out\": " << fixed(r.rmsOut, 6) << ",\n";
        out << "      \"max_gain_reduction_db\": " << fixed(r.maxGainReductionDb, 3) << ",\n";
        out << "      \"average_gain_reduction_db\": " << fixed(r.averageGainReductionDb, 3) << ",\n";
        out << "      \"clipping_count\": " << r.clippingCount << ",\n";
        out << "      \"nan_inf_count\": " << r.nanInfCount << ",\n";
        out << "      \"bypass_parity_pass\": " << (r.bypassParityPass ? "true" : "false") << ",\n";
        out << "      \"max_abs_sample\": " << fixed(r.maxAbsSample, 6) << ",\n";
        out << "      \"sane\": " << (r.sane ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
}

bool writeMarkdown(const std::string& path, const std::vector<CaseResult>& results) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "# Aestra Compressor V1 Quality Baseline\n\n";
    out << "Generated by `AestraCompressorMaterialLab` for `com.Aestrastudios.comp` / `Aestra Compressor`.\n\n";
    out << "Settings: threshold -24 dB, ratio 4:1, attack 10 ms, release 120 ms, knee 6 dB, makeup +3 dB, "
           "mix 100%, input 0 dB, output 0 dB, Detector HPF off, latency 0 samples.\n\n";
    out << "| Material | Peak In | RMS In | Peak Out | RMS Out | Max GR | Avg GR | Clips | NaN/Inf | Max Abs | Bypass | Sane |\n";
    out << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
    for (const auto& r : results) {
        out << "| " << r.name << " | " << fixed(r.peakIn) << " | " << fixed(r.rmsIn) << " | "
            << fixed(r.peakOut) << " | " << fixed(r.rmsOut) << " | " << fixed(r.maxGainReductionDb, 1)
            << " dB | " << fixed(r.averageGainReductionDb, 1) << " dB | " << r.clippingCount << " | "
            << r.nanInfCount << " | " << fixed(r.maxAbsSample) << " | "
            << (r.bypassParityPass ? "pass" : "fail") << " | " << (r.sane ? "yes" : "no") << " |\n";
    }
    out << "\nThe extreme sweep intentionally includes above-0-dBFS stress material. "
           "V1 does not clip or limit hot output by design.\n";
    return static_cast<bool>(out);
}

} // namespace

int main() {
    std::vector<std::pair<std::string, std::vector<float>>> cases;
    cases.push_back({"silence", silence()});
    cases.push_back({"sine_tone", sineTone()});
    cases.push_back({"bass_pulse", bassPulse()});
    cases.push_back({"snare_transient", snareTransient()});
    cases.push_back({"vocal_sustain", vocalSustain()});
    cases.push_back({"chord_pad", chordPad()});
    cases.push_back({"simple_mix_bus", simpleMixBus()});
    cases.push_back({"extreme_sweep", extremeSweep()});

    std::vector<CaseResult> results;
    bool allSane = true;
    for (const auto& item : cases) {
        results.push_back(analyze(item.first, item.second));
        allSane = allSane && results.back().sane;
    }

    const std::string baseDir = std::string(AESTRA_SOURCE_DIR) + "/labs/compressor/quality/";
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    if (ec || !std::filesystem::is_directory(baseDir)) {
        std::cerr << "Failed to create output directory: " << baseDir << "\n";
        return 1;
    }
    if (!writeMarkdown(baseDir + "compressor_quality_baseline.md", results)) {
        std::cerr << "Failed to write compressor markdown report\n";
        return 1;
    }
    writeJson(baseDir + "compressor_quality_baseline.json", results);
    if (!std::filesystem::exists(baseDir + "compressor_quality_baseline.json")) {
        std::cerr << "Failed to write compressor JSON report\n";
        return 1;
    }

    for (const auto& result : results) {
        std::cout << result.name << ": peakOut=" << fixed(result.peakOut)
                  << " maxGR=" << fixed(result.maxGainReductionDb, 1)
                  << " sane=" << (result.sane ? "yes" : "no") << "\n";
    }

    return allSane ? 0 : 1;
}
