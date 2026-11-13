// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// LoudnessExportTest — loudness target on export (DAW Essentials Checklist P0): the export lands on the target
// loudness with one gain, never pushes the true peak past the ceiling, and leaves silence alone.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "Analysis/AudioAnalysis.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"

#include <cmath>
#include <filesystem>
#include <iostream>

using namespace Aestra::Audio;
using namespace GoldenAudio;
namespace fs = std::filesystem;

namespace {
constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond) ++g_failures;
}
std::vector<float> tone(float amp, uint32_t frames, uint32_t sr, float clickAmp = 0.0f) {
    std::vector<float> s(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        float v = static_cast<float>(std::sin(kTau * 997.0 * i / sr)) * amp;
        if (clickAmp > 0.0f && i == frames / 2) v = clickAmp; // one near-full-scale click in a quiet mix
        s[static_cast<size_t>(i) * 2] = s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

/** Export a one-track session made of @p audio with a loudness target and measure the file. */
AudioAnalysis exportAndMeasure(const std::vector<float>& audio, double targetLufs, AudioExporter::Result& result,
                               const fs::path& file) {
    SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(cfg.sampleRate);
    tm->getPlaylistModel().setBPM(cfg.bpm);
    addAudioTrack(*tm, "Mix", audio, static_cast<uint32_t>(audio.size() / 2), cfg);
    auto engine = std::make_unique<AudioEngine>();
    prepareEngine(*engine, tm, cfg);
    AudioExporter::Config c;
    c.outputPath = file.string();
    c.scope = AudioExporter::RenderScope::FullSong;
    c.endBeat = 8.0;
    c.sampleRate = cfg.sampleRate;
    c.bitDepth = AudioExporter::BitDepth::Float_32;
    c.tailSeconds = 0.0;
    c.targetLoudness = true;
    c.loudnessTargetLufs = targetLufs;
    AudioExporter exporter(*engine, *tm);
    result = exporter.render(c);
    std::vector<float> decoded;
    uint32_t sr = 0, ch = 0;
    if (!result.success || !decodeAudioFile(file.string(), decoded, sr, ch)) return {};
    return analyzeAudio(decoded, ch, sr);
}
} // namespace

int main() {
    std::cout << "[1] the gain rule\n";
    bool limited = false;
    check(std::abs(AudioExporter::loudnessGainDb(-20.0, -14.0, -14.0, -1.0, limited) - 6.0) < 1e-9 && !limited,
          "-20 LUFS to -14: +6 dB");
    check(std::abs(AudioExporter::loudnessGainDb(-6.0, -1.5, -9.0, -1.0, limited) + 3.0) < 1e-9 && !limited,
          "a loud mix is turned down");
    check(std::abs(AudioExporter::loudnessGainDb(-26.0, -2.0, -14.0, -1.0, limited) - 1.0) < 1e-9 && limited,
          "the true-peak ceiling caps the gain and says so");
    check(AudioExporter::loudnessGainDb(AudioAnalysis::kSilenceDb, AudioAnalysis::kSilenceDb, -14.0, -1.0, limited) == 0.0,
          "silence is left as it is");

    const fs::path dir = fs::temp_directory_path() / "aestra_loudness_export_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const uint32_t sr = SessionConfig{}.sampleRate;

    std::cout << "[2] a quiet tone exported at -14 LUFS measures -14 LUFS\n";
    AudioExporter::Result r;
    auto a = exportAndMeasure(tone(0.05f, sr * 4, sr), -14.0, r, dir / "quiet.wav");
    check(r.success, "export succeeds: " + r.errorMessage);
    check(r.loudnessApplied && !r.loudnessLimitedByPeak, "loudness applied, not held by the ceiling");
    check(std::abs(a.integratedLufs + 14.0) < 0.3,
          "the file measures -14 LUFS (got " + std::to_string(a.integratedLufs) + ")");
    check(std::abs(r.finalLufs + 14.0) < 0.3, "the result reports where it landed");

    std::cout << "[3] a quiet mix with a loud click stops short at the true-peak ceiling\n";
    a = exportAndMeasure(tone(0.02f, sr * 4, sr, 0.9f), -9.0, r, dir / "click.wav");
    check(r.success && r.loudnessLimitedByPeak, "held by the ceiling, and the result says so");
    check(a.truePeakDbtp <= -1.0 + 0.3, "true peak stays at the ceiling (got " + std::to_string(a.truePeakDbtp) + ")");
    check(a.integratedLufs < -9.0 - 3.0, "and the loudness stops short of -9");

    fs::remove_all(dir, ec);
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "All loudness export checks passed\n";
    return 0;
}
