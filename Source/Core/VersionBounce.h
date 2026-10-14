// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TakeManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief What the dock shows about a version's bounce (SPEC-017, V81-V1): how long it is, how loud, and its shape.
 */
struct VersionBounceInfo {
    double seconds = 0.0;
    double lufs = -70.0;
    std::vector<float> peaks;  ///< 0..1, one per bar of the card's waveform
};

/**
 * @brief A bounce is the version rendered to a small WAV beside its snapshot, with a sidecar of what the card needs.
 * Everything here is pure or plain file IO, so a version can be bounced without a window.
 */
namespace VersionBounce {

constexpr int kBars = 72;
constexpr double kMaxSeconds = 600.0;  ///< a longer project is not bounced: the file would be huge

/// Loudest sample per bar, scaled so the loudest bar is 1. Silence is all zeros.
inline std::vector<float> peaks(const std::vector<float>& interleaved, uint32_t channels, int bars = kBars) {
    std::vector<float> out(static_cast<size_t>(std::max(bars, 1)), 0.0f);
    if (channels == 0 || interleaved.empty()) return out;
    const size_t frames = interleaved.size() / channels;
    for (size_t f = 0; f < frames; ++f) {
        const size_t bar = std::min(out.size() - 1, f * out.size() / frames);
        for (uint32_t c = 0; c < channels; ++c) out[bar] = std::max(out[bar], std::fabs(interleaved[f * channels + c]));
    }
    const float top = *std::max_element(out.begin(), out.end());
    if (top > 0.0f) for (auto& p : out) p /= top;
    return out;
}

/// 16-bit PCM WAV, little-endian, clipped at full scale. False when the file cannot be written.
inline bool writeWav16(const std::string& path, const std::vector<float>& interleaved, uint32_t channels, uint32_t rate) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out || channels == 0) return false;
    const uint32_t dataBytes = static_cast<uint32_t>(interleaved.size() * 2);
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8 * i)) & 0xff)); };
    const auto u16 = [&](uint16_t v) { out.put(static_cast<char>(v & 0xff)); out.put(static_cast<char>((v >> 8) & 0xff)); };
    out.write("RIFF", 4); u32(36 + dataBytes); out.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(static_cast<uint16_t>(channels)); u32(rate); u32(rate * channels * 2); u16(static_cast<uint16_t>(channels * 2)); u16(16);
    out.write("data", 4); u32(dataBytes);
    for (float s : interleaved) {
        const int v = static_cast<int>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
    return static_cast<bool>(out);
}

inline std::string fileNameFor(const std::string& takeId) { return takeId + ".bounce.wav"; }
inline std::string infoPathFor(const std::string& wavPath) { return wavPath + ".info"; }

inline bool writeInfo(const std::string& wavPath, const VersionBounceInfo& info) {
    std::ofstream out(infoPathFor(wavPath), std::ios::trunc);
    if (!out) return false;
    out << info.seconds << ' ' << info.lufs << '\n';
    for (float p : info.peaks) out << p << ' ';
    out << '\n';
    return static_cast<bool>(out);
}

inline bool readInfo(const std::string& wavPath, VersionBounceInfo& info) {
    std::ifstream in(infoPathFor(wavPath));
    if (!in) return false;
    VersionBounceInfo read;
    if (!(in >> read.seconds >> read.lufs)) return false;
    for (float p; in >> p;) read.peaks.push_back(std::clamp(p, 0.0f, 1.0f));
    if (read.peaks.empty()) return false;
    info = std::move(read);
    return true;
}

/// "2:41" for a length in seconds.
inline std::string lengthLabel(double seconds) {
    const int total = static_cast<int>(std::lround(std::max(0.0, seconds)));
    char buf[24];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

/// The project's mix, rendered; false when there is nothing to bounce.
using Render = std::function<bool(std::vector<float>& interleaved, uint32_t& channels, uint32_t& rate)>;
/// Integrated loudness of the render, LUFS.
using Measure = std::function<double(const std::vector<float>& interleaved, uint32_t channels, uint32_t rate)>;

/// Bounce a saved version: render, write the WAV and its card info, and record the file on the version.
/// Returns false (leaving the version as it was) when the render is empty, too long, or cannot be written.
inline bool bounceVersion(const std::string& projectPath, const std::string& takeId, const Render& render, const Measure& measure) {
    std::vector<float> audio;
    uint32_t channels = 0, rate = 0;
    if (!render || !render(audio, channels, rate) || audio.empty() || channels == 0 || rate == 0) return false;
    const double seconds = static_cast<double>(audio.size() / channels) / rate;
    if (seconds > kMaxSeconds) return false;

    const std::string name = fileNameFor(takeId);
    const std::filesystem::path wav = std::filesystem::path(TakeManager::getTakesDirectory(projectPath)) / name;
    std::error_code ec;
    std::filesystem::create_directories(wav.parent_path(), ec);
    if (!writeWav16(wav.string(), audio, channels, rate)) return false;
    VersionBounceInfo info;
    info.seconds = seconds;
    info.lufs = measure ? measure(audio, channels, rate) : -70.0;
    info.peaks = peaks(audio, channels);
    if (!writeInfo(wav.string(), info)) return false;
    return TakeManager::setVersionBounce(projectPath, takeId, name).ok;
}

} // namespace VersionBounce
} // namespace Aestra
