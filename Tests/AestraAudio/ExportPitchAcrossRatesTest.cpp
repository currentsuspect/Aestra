// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// An export at a rate other than the live engine's must keep the pitch (owner, 16 Nov 2026: "Export changes pitch"
// and "the whole beat is pitched"). A sampler unit plays a 220 Hz sample at its root note; the song is exported at
// 48000 (the engine's rate), 44100 and 96000, and the pitch of each file is measured. Every one must be 220 Hz.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "IO/AudioExporter.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace Aestra::Audio;

namespace {
constexpr uint32_t kEngineRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kBlockSize = 256;
constexpr double kBpm = 120.0;
constexpr double kToneHz = 220.0;
constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;
void check(bool c, const std::string& m) { if (!c) { std::printf("[FAIL] %s\n", m.c_str()); ++g_failures; } }

bool writeTone(const std::filesystem::path& path, double hz, uint32_t rate, double seconds) {
    const uint32_t frames = static_cast<uint32_t>(seconds * rate);
    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; ++i)
        pcm[i] = static_cast<int16_t>(std::sin(kTau * hz * i / rate) * 0.5 * 32767.0);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + frames * 2); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(rate);
    u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(frames * 2);
    out.write(reinterpret_cast<const char*>(pcm.data()), frames * 2);
    return out.good();
}

// Pitch from rising zero crossings over the loud middle of the file.
double measureHz(const std::vector<float>& interleaved, uint32_t channels, uint32_t rate) {
    std::vector<float> mono(interleaved.size() / channels);
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = interleaved[i * channels];
    const size_t from = mono.size() / 8, to = mono.size() * 3 / 8;
    int crossings = 0; double first = -1, last = -1;
    for (size_t i = from + 1; i < to; ++i)
        if (mono[i - 1] < 0.0f && mono[i] >= 0.0f) {
            const double t = (i - 1) + (-mono[i - 1]) / (mono[i] - mono[i - 1]);
            if (first < 0) first = t;
            last = t; ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
}

double exportAt(const std::filesystem::path& root, uint32_t exportRate) {
    GoldenAudio::SessionConfig cfg; // the live engine runs at 48000, as the app does on this machine
    auto tm = std::make_shared<TrackManager>();
    auto engine = std::make_unique<AudioEngine>();
    PluginManager::getInstance().initialize();
    tm->setOutputSampleRate(cfg.sampleRate);
    tm->getPlaylistModel().setBPM(cfg.bpm);
    auto* channel = tm->addChannel("Keys");
    const auto sample = root / "tone.wav";
    writeTone(sample, kToneHz, cfg.sampleRate, 3.0);
    auto& units = tm->getUnitManager();
    const UnitID unit = units.createUnit("Tone", UnitType::Sampler);
    units.setUnitAudioClip(unit, sample.string());
    units.setUnitEnabled(unit, true);
    units.setUnitGain(unit, 1.0f);
    units.setUnitMixerChannel(unit, channel->getChannelId());
    auto& patterns = tm->getPatternManager();
    const PatternID pid = patterns.createPattern();
    auto* pat = patterns.getPattern(pid);
    pat->type = PatternSource::Type::Midi;
    pat->lengthBeats = 8.0;
    pat->payload = MidiPayload{};
    std::get<MidiPayload>(pat->payload).notes.push_back(MidiNote{60, 0.0, 6.0, 1.0f, 0.0f, unit});
    auto& playlist = tm->getPlaylistModel();
    const auto lane = playlist.createLane("Keys");
    ClipInstance clip;
    clip.patternId = pid;
    clip.sourceId = pid.value;
    clip.startBeat = 0.0;
    clip.durationBeats = 8.0;
    playlist.addClip(lane, clip);
    GoldenAudio::prepareEngine(*engine, tm, cfg);
    engine->setUnitManager(&tm->getUnitManager());
    engine->setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());

    AudioExporter exporter(*engine, *tm);
    AudioExporter::Config c;
    c.scope = AudioExporter::RenderScope::FullSong;
    c.endBeat = 8.0;
    c.sampleRate = exportRate;
    c.bitDepth = AudioExporter::BitDepth::Float_32;
    c.tailSeconds = 0.0;
    c.scheduleTimelinePatterns = true;
    AudioExporter::RenderedAudio out;
    if (!exporter.renderToBuffer(c, out).ok() || out.interleaved.empty()) { check(false, "export rendered"); return 0.0; }
    check(out.sampleRate == exportRate, "the file's rate is the one asked for");
    return measureHz(out.interleaved, out.numChannels, out.sampleRate);
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "aestra_export_pitch";
    std::filesystem::create_directories(root);
    for (uint32_t rate : {48000u, 44100u, 96000u}) {
        const double hz = exportAt(root, rate);
        std::printf("export at %u Hz: measured %.2f Hz (sample is %.0f Hz)\n", rate, hz, kToneHz);
        check(std::fabs(hz - kToneHz) / kToneHz < 0.01, "an export at " + std::to_string(rate) + " Hz keeps the pitch");
    }
    std::filesystem::remove_all(root);
    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("ExportPitchAcrossRatesTest: all checks passed\n");
    return 0;
}
