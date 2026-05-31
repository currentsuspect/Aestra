// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraEQMaterialLab — deterministic EQ material baseline generator.

#include "Plugin/AestraEQ.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraEQ;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 512;
constexpr size_t kFrames = 48000;
constexpr double kTau = 6.28318530717958647692;

struct CaseResult {
    std::string name;
    double peakIn = 0.0;
    double peakOut = 0.0;
    double rmsIn = 0.0;
    double rmsOut = 0.0;
    double rumbleDeltaDb = 0.0;
    double mudDeltaDb = 0.0;
    double presenceDeltaDb = 0.0;
    double airDeltaDb = 0.0;
    uint32_t nanInfCount = 0;
    bool sane = false;
};

struct CaseThresholds {
    double maxPeakOut = 16.0;
    double maxRumbleDeltaDb = 999.0;
    double maxMudDeltaDb = 999.0;
    double minPresenceDeltaDb = -999.0;
    double minAirDeltaDb = -999.0;
};

double peak(const std::vector<float>& data) {
    double value = 0.0;
    for (float sample : data)
        value = std::max(value, std::abs(static_cast<double>(sample)));
    return value;
}

double rms(const std::vector<float>& data) {
    double sum = 0.0;
    for (float sample : data)
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    return data.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(data.size()));
}

double toneMagnitude(const std::vector<float>& data, double frequencyHz) {
    double real = 0.0;
    double imag = 0.0;
    for (size_t i = 0; i < data.size(); ++i) {
        const double phase = kTau * frequencyHz * static_cast<double>(i) / kSampleRate;
        real += static_cast<double>(data[i]) * std::cos(phase);
        imag -= static_cast<double>(data[i]) * std::sin(phase);
    }
    return std::hypot(real, imag) / std::max(1.0, static_cast<double>(data.size()) * 0.5);
}

double deltaDb(const std::vector<float>& input, const std::vector<float>& output, double frequencyHz) {
    const double inMag = std::max(toneMagnitude(input, frequencyHz), 1.0e-12);
    const double outMag = std::max(toneMagnitude(output, frequencyHz), 1.0e-12);
    return 20.0 * std::log10(outMag / inMag);
}

void addTone(std::vector<float>& data, double frequencyHz, double amplitude, double phaseOffset = 0.0) {
    for (size_t i = 0; i < data.size(); ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        data[i] += static_cast<float>(amplitude * std::sin(kTau * frequencyHz * t + phaseOffset));
    }
}

float normalizedLogFrequency(double hz, double minHz, double maxHz) {
    const double clamped = std::clamp(hz, minHz, maxHz);
    return static_cast<float>(std::log(clamped / minHz) / std::log(maxHz / minHz));
}

float normalizedGainDb(double db) {
    return static_cast<float>(std::clamp((db + 18.0) / 36.0, 0.0, 1.0));
}

float normalizedQ(double q) {
    return static_cast<float>(std::clamp((q - 0.1) / 9.9, 0.0, 1.0));
}

float normalizedType(size_t typeIndex, size_t typeCount) {
    if (typeCount <= 1)
        return 0.0f;
    const double normalized = static_cast<double>(typeIndex) / static_cast<double>(typeCount - 1);
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

void configureCleanupEQ(AestraEQ& eq) {
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, normalizedLogFrequency(70.0, 20.0, 500.0));
    eq.setParameter(AestraEQ::kParamHPFSlope, 1.0f / 3.0f);
    eq.setParameter(AestraEQ::kParamLShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamLShFreq, normalizedLogFrequency(180.0, 40.0, 1000.0));
    eq.setParameter(AestraEQ::kParamLShGain, normalizedGainDb(-2.5));
    eq.setParameter(AestraEQ::kParamLShQ, normalizedQ(0.75));
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(360.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(-4.5));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(1.35));
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, normalizedLogFrequency(3200.0, 200.0, 16000.0));
    eq.setParameter(AestraEQ::kParamBell2Gain, normalizedGainDb(3.0));
    eq.setParameter(AestraEQ::kParamBell2Q, normalizedQ(1.6));
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, normalizedLogFrequency(9000.0, 2000.0, 20000.0));
    eq.setParameter(AestraEQ::kParamHShGain, normalizedGainDb(2.0));
    eq.setParameter(AestraEQ::kParamHShQ, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamLPFEnable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
}

void configurePresenceEQ(AestraEQ& eq) {
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamHPFEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHPFFreq, normalizedLogFrequency(45.0, 20.0, 500.0));
    eq.setParameter(AestraEQ::kParamHPFSlope, 0.0f);
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(360.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(-2.5));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(1.2));
    eq.setParameter(AestraEQ::kParamBell1Type, normalizedType(0, 4));
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, normalizedLogFrequency(3200.0, 200.0, 16000.0));
    eq.setParameter(AestraEQ::kParamBell2Gain, normalizedGainDb(4.0));
    eq.setParameter(AestraEQ::kParamBell2Q, normalizedQ(1.1));
    eq.setParameter(AestraEQ::kParamBell2Type, normalizedType(0, 4));
    eq.setParameter(AestraEQ::kParamHShEnable, 1.0f);
    eq.setParameter(AestraEQ::kParamHShFreq, normalizedLogFrequency(9000.0, 2000.0, 20000.0));
    eq.setParameter(AestraEQ::kParamHShGain, normalizedGainDb(2.5));
    eq.setParameter(AestraEQ::kParamHShQ, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
}

void configureResonanceNotchEQ(AestraEQ& eq) {
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(360.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(7.0));
    eq.setParameter(AestraEQ::kParamBell1Type, normalizedType(1, 4));
    eq.setParameter(AestraEQ::kParamBell2Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell2Freq, normalizedLogFrequency(3200.0, 200.0, 16000.0));
    eq.setParameter(AestraEQ::kParamBell2Gain, normalizedGainDb(1.5));
    eq.setParameter(AestraEQ::kParamBell2Q, normalizedQ(1.0));
    eq.setParameter(AestraEQ::kParamBell2Type, normalizedType(0, 4));
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
}

void configureTiltBrightenEQ(AestraEQ& eq) {
    eq.initialize(kSampleRate, kBlockSize);
    eq.activate();
    eq.setParameter(AestraEQ::kParamBell1Enable, 1.0f);
    eq.setParameter(AestraEQ::kParamBell1Freq, normalizedLogFrequency(1000.0, 80.0, 8000.0));
    eq.setParameter(AestraEQ::kParamBell1Gain, normalizedGainDb(12.0));
    eq.setParameter(AestraEQ::kParamBell1Q, normalizedQ(0.7));
    eq.setParameter(AestraEQ::kParamBell1Type, normalizedType(3, 4));
    eq.setParameter(AestraEQ::kParamBell2Enable, 0.0f);
    eq.setParameter(AestraEQ::kParamBypass, 0.0f);
}

std::vector<float> generatedMix() {
    std::vector<float> data(kFrames, 0.0f);
    uint32_t seed = 0xA357EA01u;
    for (size_t i = 0; i < data.size(); ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        seed = seed * 1664525u + 1013904223u;
        const double noise = (static_cast<double>((seed >> 8) & 0xFFFFu) / 32767.5) - 1.0;
        const double bass = 0.18 * std::sin(kTau * 55.0 * t);
        const double mud = 0.20 * std::sin(kTau * 360.0 * t);
        const double vocal = 0.13 * std::sin(kTau * 820.0 * t) + 0.07 * std::sin(kTau * 1640.0 * t);
        const double presence = 0.035 * std::sin(kTau * 3200.0 * t);
        const double air = 0.018 * std::sin(kTau * 10000.0 * t);
        const double rumble = 0.10 * std::sin(kTau * 35.0 * t);
        data[i] = static_cast<float>(rumble + bass + mud + vocal + presence + air + noise * 0.012);
    }
    return data;
}

std::vector<float> vocalPresenceMaterial() {
    std::vector<float> data(kFrames, 0.0f);
    addTone(data, 35.0, 0.025);
    addTone(data, 160.0, 0.10, 0.2);
    addTone(data, 360.0, 0.15, 0.4);
    addTone(data, 820.0, 0.16, 0.1);
    addTone(data, 1640.0, 0.08, 0.7);
    addTone(data, 3200.0, 0.045, 0.3);
    addTone(data, 10000.0, 0.018, 0.5);
    return data;
}

std::vector<float> resonantLoopMaterial() {
    std::vector<float> data(kFrames, 0.0f);
    addTone(data, 35.0, 0.018);
    addTone(data, 90.0, 0.12, 0.1);
    addTone(data, 360.0, 0.28, 0.3);
    addTone(data, 720.0, 0.08, 0.4);
    addTone(data, 3200.0, 0.035, 0.9);
    addTone(data, 10000.0, 0.012, 0.2);
    return data;
}

std::vector<float> tiltMaterial() {
    std::vector<float> data(kFrames, 0.0f);
    addTone(data, 35.0, 0.010);
    addTone(data, 100.0, 0.16, 0.1);
    addTone(data, 360.0, 0.10, 0.3);
    addTone(data, 1000.0, 0.12, 0.5);
    addTone(data, 3200.0, 0.050, 0.2);
    addTone(data, 10000.0, 0.026, 0.7);
    return data;
}

std::vector<float> process(AestraEQ& eq, const std::vector<float>& input) {
    std::vector<float> output(input.size(), 0.0f);
    for (size_t offset = 0; offset < input.size(); offset += kBlockSize) {
        const uint32_t frames = static_cast<uint32_t>(std::min<size_t>(kBlockSize, input.size() - offset));
        const float* inPtr = input.data() + offset;
        float* outPtr = output.data() + offset;
        eq.process(&inPtr, &outPtr, 1, 1, frames);
    }
    return output;
}

bool meetsThresholds(const CaseResult& result, const CaseThresholds& thresholds) {
    return result.nanInfCount == 0 && result.peakOut <= thresholds.maxPeakOut &&
           result.rumbleDeltaDb <= thresholds.maxRumbleDeltaDb && result.mudDeltaDb <= thresholds.maxMudDeltaDb &&
           result.presenceDeltaDb >= thresholds.minPresenceDeltaDb && result.airDeltaDb >= thresholds.minAirDeltaDb;
}

CaseResult analyzeMaterial(const std::string& name, const std::vector<float>& input, void (*configure)(AestraEQ&),
                           const CaseThresholds& thresholds) {
    AestraEQ eq;
    configure(eq);
    const auto output = process(eq, input);

    CaseResult result;
    result.name = name;
    result.peakIn = peak(input);
    result.peakOut = peak(output);
    result.rmsIn = rms(input);
    result.rmsOut = rms(output);
    result.rumbleDeltaDb = deltaDb(input, output, 35.0);
    result.mudDeltaDb = deltaDb(input, output, 360.0);
    result.presenceDeltaDb = deltaDb(input, output, 3200.0);
    result.airDeltaDb = deltaDb(input, output, 10000.0);

    for (float sample : output) {
        if (!std::isfinite(sample))
            ++result.nanInfCount;
    }
    result.sane = meetsThresholds(result, thresholds);
    return result;
}

std::string fixed(double value, int precision = 3) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string formatJsonNumber(double value, int precision = 3) {
    if (!std::isfinite(value))
        return "null";
    return fixed(value, precision);
}

std::string escapeForMarkdownTable(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '|':
            escaped += "\\|";
            break;
        case '\r':
        case '\n':
            escaped += ' ';
            break;
        default:
            escaped += c;
            break;
        }
    }
    return escaped;
}

std::string escapeForJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                escaped += buf;
            } else {
                escaped += static_cast<char>(c);
            }
            break;
        }
    }
    return escaped;
}

CaseThresholds thresholds(double rumbleMax, double mudMax, double presenceMin, double airMin) {
    CaseThresholds value;
    value.maxRumbleDeltaDb = rumbleMax;
    value.maxMudDeltaDb = mudMax;
    value.minPresenceDeltaDb = presenceMin;
    value.minAirDeltaDb = airMin;
    return value;
}

bool writeMarkdown(const std::string& path, const std::vector<CaseResult>& results) {
    std::ofstream out(path);
    if (!out.is_open())
        return false;
    out << "# Aestra EQ Material Baseline\n\n";
    out << "Generated by `AestraEQMaterialLab` for `com.Aestrastudios.eq` / `Aestra EQ`.\n\n";
    out << "Each row renders deterministic material through a production-style EQ move and checks "
           "finite output, bounded peak, and expected spectral deltas.\n\n";
    out << "| Material | Peak In | RMS In | Peak Out | RMS Out | 35 Hz | 360 Hz | 3.2 kHz | 10 kHz | "
           "NaN/Inf | Sane |\n";
    out << "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (const auto& result : results) {
        out << "| " << escapeForMarkdownTable(result.name) << " | " << fixed(result.peakIn) << " | "
            << fixed(result.rmsIn) << " | " << fixed(result.peakOut) << " | " << fixed(result.rmsOut) << " | "
            << fixed(result.rumbleDeltaDb, 2) << " dB | " << fixed(result.mudDeltaDb, 2) << " dB | "
            << fixed(result.presenceDeltaDb, 2) << " dB | " << fixed(result.airDeltaDb, 2) << " dB | "
            << result.nanInfCount << " | " << (result.sane ? "yes" : "no") << " |\n";
    }
    return static_cast<bool>(out);
}

bool writeJson(const std::string& path, const std::vector<CaseResult>& results) {
    std::ofstream out(path);
    if (!out.is_open())
        return false;
    out << "{\n";
    out << "  \"plugin_id\": \"com.Aestrastudios.eq\",\n";
    out << "  \"plugin_name\": \"Aestra EQ\",\n";
    out << "  \"baseline\": \"eq-v1-material-cases\",\n";
    out << "  \"sample_rate_hz\": 48000,\n";
    out << "  \"block_size\": 512,\n";
    out << "  \"materials\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        out << "    {\n";
        out << "      \"name\": \"" << escapeForJson(result.name) << "\",\n";
        out << "      \"peak_in\": " << formatJsonNumber(result.peakIn, 6) << ",\n";
        out << "      \"rms_in\": " << formatJsonNumber(result.rmsIn, 6) << ",\n";
        out << "      \"peak_out\": " << formatJsonNumber(result.peakOut, 6) << ",\n";
        out << "      \"rms_out\": " << formatJsonNumber(result.rmsOut, 6) << ",\n";
        out << "      \"rumble_delta_db\": " << formatJsonNumber(result.rumbleDeltaDb, 3) << ",\n";
        out << "      \"mud_delta_db\": " << formatJsonNumber(result.mudDeltaDb, 3) << ",\n";
        out << "      \"presence_delta_db\": " << formatJsonNumber(result.presenceDeltaDb, 3) << ",\n";
        out << "      \"air_delta_db\": " << formatJsonNumber(result.airDeltaDb, 3) << ",\n";
        out << "      \"nan_inf_count\": " << result.nanInfCount << ",\n";
        out << "      \"sane\": " << (result.sane ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace

int main() {
    std::vector<CaseResult> results;
    results.push_back(
        analyzeMaterial("cleanup_mix", generatedMix(), configureCleanupEQ, thresholds(-7.0, -3.0, 1.5, 0.8)));
    results.push_back(analyzeMaterial("vocal_presence", vocalPresenceMaterial(), configurePresenceEQ,
                                      thresholds(0.5, -1.5, 2.5, 1.5)));
    results.push_back(analyzeMaterial("resonance_notch", resonantLoopMaterial(), configureResonanceNotchEQ,
                                      thresholds(0.5, -14.0, 1.0, -0.5)));
    results.push_back(
        analyzeMaterial("tilt_brighten", tiltMaterial(), configureTiltBrightenEQ, thresholds(0.5, -2.0, 3.0, 4.0)));

    const std::string baseDir = std::string(AESTRA_LAB_OUTPUT_DIR) + "/";
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    if (ec || !std::filesystem::is_directory(baseDir)) {
        std::cerr << "Failed to create output directory: " << baseDir << "\n";
        return 1;
    }

    if (!writeMarkdown(baseDir + "eq_material_baseline.md", results)) {
        std::cerr << "Failed to write EQ markdown report\n";
        return 1;
    }
    if (!writeJson(baseDir + "eq_material_baseline.json", results)) {
        std::cerr << "Failed to write EQ JSON report\n";
        return 1;
    }

    bool allSane = true;
    for (const auto& result : results) {
        allSane = allSane && result.sane;
        std::cout << result.name << ": rumble=" << fixed(result.rumbleDeltaDb, 2)
                  << "dB mud=" << fixed(result.mudDeltaDb, 2) << "dB presence=" << fixed(result.presenceDeltaDb, 2)
                  << "dB air=" << fixed(result.airDeltaDb, 2) << "dB sane=" << (result.sane ? "yes" : "no") << "\n";
    }
    return allSane ? 0 : 1;
}
