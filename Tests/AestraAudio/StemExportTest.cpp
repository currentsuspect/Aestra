// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// StemExportTest — V8-S1. One file per sounding track, each the track as it feeds the mix.
//   1. a stem is the track AFTER its fader (a 0.4 sine on a 0.5 fader is a 0.2 sine);
//   2. the stems sum to the master mix (no master stage in the test engine), so they line up;
//   3. only tracks with sound get a stem; a track with nothing routed to it does not;
//   4. file names are numbered, safe on every filesystem, and unique;
//   5. a failing or cancelled stem says which track and stops.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "IO/MiniAudioDecoder.h"
#include "IO/StemExport.h"

#include <algorithm>
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
    if (!cond)
        ++g_failures;
}
std::vector<float> sine(double hz, float amp, uint32_t frames, uint32_t sr) {
    std::vector<float> s(static_cast<size_t>(frames) * 2);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * hz * i / sr)) * amp;
        s[static_cast<size_t>(i) * 2] = s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}
double peakOf(const std::vector<float>& v, size_t from = 0) {
    double p = 0;
    for (size_t i = from; i < v.size(); ++i)
        p = std::max(p, static_cast<double>(std::fabs(v[i])));
    return p;
}
} // namespace

int main() {
    std::cout << "[1] names\n";
    check(stemFileName("Song", 1, "Kick") == "Song - 01 Kick.wav", "numbered");
    check(stemFileName("Song", 12, "Lead: v2/alt?") == "Song - 12 Lead_ v2_alt_.wav", "unsafe characters replaced");
    check(stemFileName("Song", 3, "Pad. ") == "Song - 03 Pad.wav", "trailing dot and space dropped");
    check(stemFileName("", 4, "") == "Stem - 04 Track.wav", "empty prefix and name still make a name");

    SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(cfg.sampleRate);
    tm->getPlaylistModel().setBPM(cfg.bpm);
    const uint32_t frames = cfg.sampleRate * 2;
    addAudioTrack(*tm, "Kick/Snare", sine(440.0, 0.4f, frames, cfg.sampleRate), frames, cfg);
    addAudioTrack(*tm, "Bass", sine(110.0, 0.3f, frames, cfg.sampleRate), frames, cfg);
    tm->addChannel("Empty bus"); // nothing routed: no stem
    tm->getChannel(0)->setVolume(0.5f);
    auto engine = std::make_unique<AudioEngine>();
    prepareEngine(*engine, tm, cfg);

    const fs::path dir = fs::temp_directory_path() / "aestra_stem_export_test";
    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << "[2] stems of a two-track session\n";
    check(stemChannelIds(*tm).size() == 2, "two tracks have sound; the empty bus does not");
    AudioExporter::Config base;
    base.scope = AudioExporter::RenderScope::FullSong;
    base.endBeat = 4.0;
    base.sampleRate = cfg.sampleRate;
    base.bitDepth = AudioExporter::BitDepth::Float_32;
    base.tailSeconds = 0.0;
    float lastProgress = 0.0f;
    const auto r = exportStems(*engine, *tm, base, dir.string(), "Song", true,
                               [&](float p) { lastProgress = std::max(lastProgress, p); });
    check(r.ok, "export succeeds: " + r.error);
    check(r.files.size() == 3, "two stems and the master");
    check(lastProgress > 0.99f, "progress reaches the end");
    std::vector<std::vector<float>> audio;
    for (const auto& f : r.files) {
        std::vector<float> a;
        uint32_t sr = 0, ch = 0;
        check(fs::exists(f.path) && decodeAudioFile(f.path, a, sr, ch) && sr == cfg.sampleRate && ch == 2,
              "decodes: " + fs::path(f.path).filename().string());
        audio.push_back(a);
    }
    if (audio.size() == 3 && audio[0].size() == audio[2].size() && audio[1].size() == audio[2].size()) {
        check(std::fabs(peakOf(audio[0], 4096) - 0.2) < 0.01, "first stem has its fader in it (0.4 x 0.5 = 0.2)");
        check(std::fabs(peakOf(audio[1], 4096) - 0.3) < 0.01, "second stem at unity fader (0.3)");
        double worst = 0;
        for (size_t i = 0; i < audio[2].size(); ++i)
            worst = std::max(worst, static_cast<double>(std::fabs(audio[0][i] + audio[1][i] - audio[2][i])));
        check(worst < 1e-4, "the stems sum to the master mix (worst " + std::to_string(worst) + ")");
    } else {
        check(false, "stems and master have the same length");
    }
    check(fs::path(r.files[0].path).filename() == "Song - 01 Kick_Snare.wav", "first file name is numbered and safe");

    std::cout << "[3] cancel stops and says which stem\n";
    int polls = 0;
    const auto c = exportStems(*engine, *tm, base, (dir / "cancel").string(), "Song", false, {},
                               [&]() { return ++polls > 2; });
    check(!c.ok && !c.error.empty() && c.error.find("Kick/Snare") != std::string::npos, "cancelled: " + c.error);

    std::cout << "[4] nothing to export\n";
    auto empty = std::make_shared<TrackManager>();
    auto emptyEngine = std::make_unique<AudioEngine>();
    prepareEngine(*emptyEngine, empty, cfg);
    const auto e = exportStems(*emptyEngine, *empty, base, (dir / "empty").string(), "X", false);
    check(!e.ok && !e.error.empty(), "an empty project refuses: " + e.error);

    fs::remove_all(dir, ec);
    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
