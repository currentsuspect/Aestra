// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ExportSchedulesPatternsTest — issue #1397.
//
// File > Export Audio on a project that has not played since it was opened wrote digital silence, and said it
// succeeded: pattern clips reach their instruments through the pattern scheduler, which only Play fills, and the
// export never filled it. The exporter can now put the timeline in for the exact range it renders
// (Config::scheduleTimelinePatterns) and takes it out again. What must hold:
//   1. a fresh project, never played, exports SILENCE without the option (the bug, pinned) and real audio with it;
//   2. an export leaves the scheduler as it found it: two exports in a row are identical;
//   3. a selection range renders audio too, not only the full song;
//   4. stems carry the option through (every stem is rendered with the timeline in).

#include "GoldenAudio/GoldenAudioHarness.h"

#include "IO/AudioExporter.h"
#include "IO/StemExport.h"
#include "Plugin/PluginManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;
using namespace GoldenAudio;

namespace {

constexpr double kTau = 6.28318530717958647692;
int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond)
        ++g_failures;
}

bool writeToneWav(const std::string& path, double hz, double amp, uint32_t frames, uint32_t sr) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    const uint32_t dataBytes = frames * 2;
    const uint32_t riff = 36 + dataBytes, fmt = 16, byteRate = sr * 2;
    const uint16_t pcmFmt = 1, ch = 1, align = 2, bits = 16;
    out.write("RIFF", 4); out.write(reinterpret_cast<const char*>(&riff), 4); out.write("WAVEfmt ", 8);
    out.write(reinterpret_cast<const char*>(&fmt), 4); out.write(reinterpret_cast<const char*>(&pcmFmt), 2);
    out.write(reinterpret_cast<const char*>(&ch), 2); out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&byteRate), 4); out.write(reinterpret_cast<const char*>(&align), 2);
    out.write(reinterpret_cast<const char*>(&bits), 2); out.write("data", 4);
    out.write(reinterpret_cast<const char*>(&dataBytes), 4);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t v = static_cast<int16_t>(std::sin(kTau * hz * i / sr) * amp * 32767.0);
        out.write(reinterpret_cast<const char*>(&v), 2);
    }
    return out.good();
}

double peak(const std::vector<float>& v) {
    double p = 0.0;
    for (float s : v)
        p = std::max(p, static_cast<double>(std::fabs(s)));
    return p;
}

// A project that has NEVER played: one sampler unit, one pattern clip, notes at beat 0 and beat 4.
struct Session {
    SessionConfig cfg;
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();

    Session() {
        PluginManager::getInstance().initialize();
        tm->setOutputSampleRate(cfg.sampleRate);
        tm->getPlaylistModel().setBPM(cfg.bpm);
        auto* channel = tm->addChannel("Keys");
        const std::string wav = (std::filesystem::temp_directory_path() / "export_sched_tone.wav").string();
        writeToneWav(wav, 440.0, 0.8, cfg.sampleRate * 2, cfg.sampleRate);
        auto& units = tm->getUnitManager();
        const UnitID unit = units.createUnit("Keys", UnitType::Sampler);
        units.setUnitAudioClip(unit, wav);
        units.setUnitEnabled(unit, true);
        units.setUnitGain(unit, 1.0f);
        units.setUnitMixerChannel(unit, channel->getChannelId());
        auto& patterns = tm->getPatternManager();
        const PatternID pid = patterns.createPattern();
        auto* pat = patterns.getPattern(pid);
        pat->type = PatternSource::Type::Midi;
        pat->lengthBeats = 8.0;
        pat->payload = MidiPayload{};
        auto& notes = std::get<MidiPayload>(pat->payload).notes;
        notes.push_back(MidiNote{60, 0.0, 1.0, 1.0f, 0.0f, unit});
        notes.push_back(MidiNote{60, 4.0, 1.0, 1.0f, 0.0f, unit});
        auto& pm = tm->getPlaylistModel();
        const auto lane = pm.createLane("Keys");
        ClipInstance ci;
        ci.patternId = pid;
        ci.sourceId = pid.value;
        ci.startBeat = 0.0;
        ci.durationBeats = 8.0;
        pm.addClip(lane, ci);
        prepareEngine(*engine, tm, cfg);
        engine->setUnitManager(&tm->getUnitManager());
        engine->setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
    }

    std::vector<float> render(bool schedule, AudioExporter::RenderScope scope = AudioExporter::RenderScope::FullSong,
                              double startSeconds = 0.0, double endSeconds = 0.0) {
        AudioExporter exporter(*engine, *tm);
        AudioExporter::Config c;
        c.scope = scope;
        c.endBeat = 8.0;
        c.startTimeSeconds = startSeconds;
        c.endTimeSeconds = endSeconds;
        c.sampleRate = cfg.sampleRate;
        c.bitDepth = AudioExporter::BitDepth::Float_32;
        c.tailSeconds = 0.0;
        c.scheduleTimelinePatterns = schedule;
        AudioExporter::RenderedAudio out;
        exporter.renderToBuffer(c, out);
        return out.interleaved;
    }
};

} // namespace

int main() {
    std::cout << "[1] a project that has never played: silence without the option, audio with it\n";
    {
        Session s;
        const auto silent = s.render(false);
        check(!silent.empty() && peak(silent) < 1e-4, "without the option the export is digital silence (the bug), peak " + std::to_string(peak(silent)));
        const auto audible = s.render(true);
        check(peak(audible) > 0.05, "with the option the export has the notes, peak " + std::to_string(peak(audible)));
    }

    std::cout << "[2] the export leaves the scheduler as it found it\n";
    {
        Session s;
        const auto a = s.render(true);
        const auto b = s.render(true);
        check(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()), "two exports in a row are identical");
        const auto after = s.render(false);
        check(peak(after) < 1e-4, "and nothing is left scheduled for the next export that does not ask (peak " + std::to_string(peak(after)) + ")");
    }

    std::cout << "[3] a selection range renders audio too\n";
    {
        Session s;
        // Beats 4-6 at 120 BPM = 2.0 s to 3.0 s: the second note.
        const auto sel = s.render(true, AudioExporter::RenderScope::Selection, 2.0, 3.0);
        check(peak(sel) > 0.05, "selection over the second note has it, peak " + std::to_string(peak(sel)));
        // Beats 1-3 = 0.5 s to 1.5 s: no note starts in the window (the first has ended).
        const auto quiet = s.render(true, AudioExporter::RenderScope::Selection, 1.0, 1.5);
        check(peak(quiet) < 0.2, "a window with no note in it is quiet, peak " + std::to_string(peak(quiet)));
    }

    std::cout << "[4] stems carry the option through\n";
    {
        Session s;
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "export_sched_stems";
        std::filesystem::remove_all(dir);
        AudioExporter::Config c;
        c.scope = AudioExporter::RenderScope::FullSong;
        c.endBeat = 8.0;
        c.sampleRate = s.cfg.sampleRate;
        c.bitDepth = AudioExporter::BitDepth::Float_32;
        c.tailSeconds = 0.0;
        c.scheduleTimelinePatterns = true;
        const auto result = exportStems(*s.engine, *s.tm, c, dir.string(), "Stem", true, nullptr, nullptr);
        check(result.ok && !result.files.empty(), "stems exported: " + result.error);
        double best = -200.0;
        for (const auto& f : result.files)
            best = std::max(best, static_cast<double>(f.result.peakDb));
        check(best > -30.0, "at least one stem has the notes (loudest stem peak " + std::to_string(best) + " dB)");
    }

    std::cout << (g_failures ? "FAILED\n" : "All export-scheduling tests passed.\n");
    return g_failures ? 1 : 0;
}
