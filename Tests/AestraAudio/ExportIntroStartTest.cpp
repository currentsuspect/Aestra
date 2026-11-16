// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A bounce must begin where the timeline begins (owner, 16 Nov 2026: Audition "is cutting off the intro"). A sampler
// note on beat 1 and an audio clip on beat 1 each start with a loud first sample; the first frame of the rendered
// file that is audible is found, and it must be within about ten milliseconds of the start, not a block (90 ms) or
// a latency later.

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

// A tone that starts at full level (cosine), so the very first sample is loud.
bool writeLoud(const std::filesystem::path& path, uint32_t rate, double seconds) {
    const double hz = 220.0;
    const uint32_t frames = static_cast<uint32_t>(seconds * rate);
    std::vector<int16_t> pcm(frames);
    for (uint32_t i = 0; i < frames; ++i)
        pcm[i] = static_cast<int16_t>(std::cos(kTau * hz * i / rate) * 0.8 * 32767.0);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("RIFF", 4); u32(36 + frames * 2); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(rate);
    u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(frames * 2);
    out.write(reinterpret_cast<const char*>(pcm.data()), frames * 2);
    return out.good();
}

// The first frame whose magnitude exceeds a small threshold.
double firstAudibleMs(const std::vector<float>& interleaved, uint32_t channels, uint32_t rate) {
    for (size_t f = 0; f < interleaved.size() / channels; ++f)
        for (uint32_t c = 0; c < channels; ++c)
            if (std::fabs(interleaved[f * channels + c]) > 0.02f) return 1000.0 * static_cast<double>(f) / rate;
    return -1.0;
}

// kind 0: a pattern note on the sampler at beat 0; kind 1: an audio clip on the timeline at beat 0.
double startMs(const std::filesystem::path& root, int kind, uint32_t exportRate) {
    GoldenAudio::SessionConfig cfg;
    auto tm = std::make_shared<TrackManager>();
    auto engine = std::make_unique<AudioEngine>();
    PluginManager::getInstance().initialize();
    tm->setOutputSampleRate(cfg.sampleRate);
    tm->getPlaylistModel().setBPM(cfg.bpm);
    auto& playlist = tm->getPlaylistModel();
    if (kind != 1) {
        auto* channel = tm->addChannel("Keys");
        const auto sample = root / "loud.wav";
        writeLoud(sample, cfg.sampleRate, 3.0);
        auto& units = tm->getUnitManager();
        const UnitID unit = units.createUnit("Loud", UnitType::Sampler);
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
        std::get<MidiPayload>(pat->payload).notes.push_back(MidiNote{60, kind == 2 ? 1.0 : 0.0, 6.0, 1.0f, 0.0f, unit});
        const auto lane = playlist.createLane("Keys");
        ClipInstance clip;
        clip.patternId = pid;
        clip.sourceId = pid.value;
        clip.startBeat = 0.0;
        clip.durationBeats = 8.0;
        playlist.addClip(lane, clip);
    } else {
        std::vector<float> stereo(static_cast<size_t>(cfg.sampleRate) * 3 * 2);
        for (size_t i = 0; i < stereo.size() / 2; ++i)
            stereo[i * 2] = stereo[i * 2 + 1] = 0.8f * static_cast<float>(std::cos(kTau * 220.0 * i / cfg.sampleRate));
        GoldenAudio::addAudioTrackAtBeat(*tm, "Intro", stereo, cfg.sampleRate * 3, cfg, 0.0);
    }
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
    if (!exporter.renderToBuffer(c, out).ok() || out.interleaved.empty()) { check(false, "the song rendered"); return -1.0; }
    return firstAudibleMs(out.interleaved, out.numChannels, out.sampleRate);
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() / "aestra_export_intro";
    std::filesystem::create_directories(root);
    const char* names[] = {"a sampler note on the first beat", "an audio clip on the first beat", "a sampler note on beat 2 (the control: its time is its own)"};
    for (int kind = 0; kind < 3; ++kind)
        for (uint32_t rate : {48000u, 44100u}) {
            const double ms = startMs(root, kind, rate);
            std::printf("%s, exported at %u Hz: first audible at %.2f ms\n", names[kind], rate, ms);
            const double expected = kind == 2 ? 500.0 : 0.0; // beat 2 at 120 bpm is half a second in
            check(ms >= expected && ms < expected + 10.0, std::string(names[kind]) + " is heard at its own time at " + std::to_string(rate) + " Hz");
        }
    std::filesystem::remove_all(root);
    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("ExportIntroStartTest: all checks passed\n");
    return 0;
}
