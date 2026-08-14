// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// RealtimeExportParityTest — proves that the realtime playback path and the
// offline export path produce the same audio for an equivalent session, and
// that monitoring conveniences (preview ducking) cannot contaminate an export.
//
// Paths under test (see Aestra-Internals: aestra-docs/audio-integrity-infrastructure.md §1):
//   realtime: AudioEngine::processBlock driven block-by-block (as the device
//             callback does) — rendered here via GoldenAudio::renderBlocks.
//   offline:  AudioEngine::bounceRangeToWav → AudioExporter::render, which
//             drives the same processBlock with metronome/audition disabled
//             and writes Float_32 (no quantization/dither in the comparison).
//
// Case 2 probes a specific contamination vector: preview ducking is computed
// INSIDE processBlock from PreviewEngine::isAudiblyPlaying(), and the export
// forces transport playing — so an audible browser preview during an offline
// export can attenuate the exported file. This test reproduces it headlessly
// by pumping the preview exactly as the device callback would.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "DSP/ContinuousParamBuffer.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"
#include "Playback/PreviewEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;
namespace fs = std::filesystem;

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr uint32_t kSeconds = 2;
constexpr double kBeats = 4.0; // 2 s at 120 BPM

std::vector<float> makeSine(double freqHz, float amplitude, uint32_t frames, uint32_t sampleRate) {
    std::vector<float> s(static_cast<size_t>(frames) * 2, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * freqHz * static_cast<double>(i) / sampleRate)) *
                        amplitude;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

// Minimal 16-bit WAV writer for the preview source file (same as
// ExportBounceParityTest's inline writer).
bool writeWav16(const fs::path& path, const std::vector<float>& interleaved, uint32_t frames,
                uint32_t channels, uint32_t sampleRate) {
    std::ofstream f(path.string(), std::ios::binary);
    if (!f) return false;
    auto write16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    auto write32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const uint32_t dataSize = frames * channels * 2;
    f.write("RIFF", 4); write32(36 + dataSize); f.write("WAVE", 4);
    f.write("fmt ", 4); write32(16);
    write16(1); write16(static_cast<uint16_t>(channels));
    write32(sampleRate);
    write32(sampleRate * channels * 2);
    write16(static_cast<uint16_t>(channels * 2)); write16(16);
    f.write("data", 4); write32(dataSize);
    for (float s : interleaved) {
        const int16_t v = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        write16(static_cast<uint16_t>(v));
    }
    return f.good();
}

std::shared_ptr<TrackManager> buildSession(const SessionConfig& cfg, uint32_t totalFrames) {
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "ParityA", makeSine(440.0, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    addAudioTrack(*tm, "ParityB", makeSine(660.0, 0.20f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    return tm;
}

void applyParams(AudioEngine& engine) {
    auto params = std::make_shared<ContinuousParamBuffer>();
    params->setFaderDb(0, -3.0f);
    params->setPan(0, -0.4f);
    params->setFaderDb(1, -6.0f);
    params->setPan(1, 0.6f);
    engine.setContinuousParams(params);
}

double rmsOf(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double sumSq = 0.0;
    for (float s : v) sumSq += static_cast<double>(s) * static_cast<double>(s);
    return std::sqrt(sumSq / static_cast<double>(v.size()));
}

double rmsRange(const std::vector<float>& interleaved, size_t firstFrame, size_t endFrame, uint32_t channels) {
    if (channels == 0 || firstFrame >= endFrame || endFrame * channels > interleaved.size()) return 0.0;
    double sumSq = 0.0;
    const size_t firstSample = firstFrame * channels;
    const size_t endSample = endFrame * channels;
    for (size_t i = firstSample; i < endSample; ++i) {
        sumSq += static_cast<double>(interleaved[i]) * static_cast<double>(interleaved[i]);
    }
    return std::sqrt(sumSq / static_cast<double>(endSample - firstSample));
}

// Warm the engine's param/track state to steady state before the measured
// render, then rewind. This mirrors the real app, where exports run on the
// long-lived engine whose mixer state was applied long ago. (Finding, kept
// deliberately out of this test's assertions: on a FRESH engine, freshly set
// ContinuousParamBuffer values take one processBlock to reach the mix — at
// the exporter's 4096-frame block size that is the first ~85 ms. Documented
// in Aestra-Internals: aestra-docs/audio-integrity-infrastructure.md; real exports are unaffected
// because the app's engine is warm.)
void warmupEngine(AudioEngine& engine, const SessionConfig& cfg) {
    engine.setTransportPlaying(true);
    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    for (int i = 0; i < 8; ++i) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, cfg.blockSize, 0.0);
    }
    engine.setTransportPlaying(false);
    engine.setGlobalSamplePos(0);
}

bool runExport(const std::shared_ptr<TrackManager>& tm, const SessionConfig& cfg, const fs::path& outPath,
               PreviewEngine* preview, float duckAttenuationDb, std::vector<float>& decodedOut) {
    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    applyParams(engine);
    warmupEngine(engine, cfg);
    if (preview) {
        engine.setPreviewEngine(preview);
        engine.setPreviewDuckingAttenuationDb(duckAttenuationDb);
    }

    std::error_code ec;
    fs::remove(outPath, ec);
    if (!engine.bounceRangeToWav(0.0, kBeats, outPath.string(), -1)) {
        std::cerr << "bounceRangeToWav failed for " << outPath << "\n";
        return false;
    }
    uint32_t sr = 0, ch = 0;
    if (!decodeAudioFile(outPath.string(), decodedOut, sr, ch)) {
        std::cerr << "failed to decode " << outPath << "\n";
        return false;
    }
    if (sr != cfg.sampleRate || ch != cfg.channels) {
        std::cerr << "export format mismatch: " << sr << " Hz / " << ch << " ch, expected "
                  << cfg.sampleRate << " Hz / " << cfg.channels << " ch\n";
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Case 1: realtime processBlock output ≈ offline export output, same session
// -----------------------------------------------------------------------------
bool runParityCase(const std::shared_ptr<TrackManager>& tm, const SessionConfig& cfg,
                   const std::vector<float>& cleanExport) {
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;

    AudioEngine engine;
    prepareEngine(engine, tm, cfg);
    applyParams(engine);
    warmupEngine(engine, cfg);
    engine.setTransportPlaying(true);
    std::vector<float> realtime = renderBlocks(engine, totalFrames, cfg);
    engine.setTransportPlaying(false);

    // Compare the overlap, past the transport fade-in both paths perform.
    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t n = std::min(realtime.size(), cleanExport.size());
    if (n <= trimStart) {
        std::cerr << "not enough overlap to compare (" << n << " samples)\n";
        return false;
    }
    std::vector<float> rt(realtime.begin() + static_cast<ptrdiff_t>(trimStart),
                          realtime.begin() + static_cast<ptrdiff_t>(n));
    std::vector<float> ex(cleanExport.begin() + static_cast<ptrdiff_t>(trimStart),
                          cleanExport.begin() + static_cast<ptrdiff_t>(n));

    DiffReport r = compareBuffers(rt, ex, cfg.channels, cfg.sampleRate, 1e-6);
    const bool pass = (r.rmsErrorDb <= -120.0) && (r.maxAbsError <= 1e-6);
    printReport("Realtime_vs_Export_Parity", r, pass,
                "RMS <= -120 dB and maxAbs <= 1e-6 on the trimmed overlap");
    std::cout << "  realtime frames: " << realtime.size() / cfg.channels
              << ", export frames: " << cleanExport.size() / cfg.channels << "\n";
    return pass;
}

// -----------------------------------------------------------------------------
// Case 2: an audible preview must NOT attenuate an offline export
// -----------------------------------------------------------------------------
bool runDuckContaminationCase(const std::shared_ptr<TrackManager>& tm, const SessionConfig& cfg,
                              const std::vector<float>& cleanExport, const fs::path& tempRoot) {
    // Build a real preview source and make it audibly playing, exactly as the
    // device callback would: play() + pump processRealtime until the engine's
    // audibility latch (output peak) is set.
    const uint32_t previewFrames = cfg.sampleRate; // 1 s
    const fs::path previewWav = tempRoot / "parity_preview_src.wav";
    if (!writeWav16(previewWav, makeSine(330.0, 0.8f, previewFrames, cfg.sampleRate), previewFrames, 2,
                    cfg.sampleRate)) {
        std::cerr << "failed to write preview wav\n";
        return false;
    }

    PreviewEngine preview;
    preview.setOutputSampleRate(cfg.sampleRate);
    preview.play(previewWav.string(), 0.0f, 10.0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::vector<float> sink(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    while (!preview.isAudiblyPlaying() && std::chrono::steady_clock::now() < deadline) {
        std::fill(sink.begin(), sink.end(), 0.0f);
        preview.processRealtime(sink.data(), cfg.blockSize, cfg.channels);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!preview.isAudiblyPlaying()) {
        std::cerr << "could not bring preview to audible state; cannot probe contamination\n";
        return false;
    }

    // Export with the audible preview registered and ducking enabled (12 dB),
    // as it would be during normal app use.
    std::vector<float> duckedExport;
    if (!runExport(tm, cfg, tempRoot / "parity_ducked.wav", &preview, 12.0f, duckedExport)) return false;

    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t n = std::min(duckedExport.size(), cleanExport.size());
    std::vector<float> ducked(duckedExport.begin() + static_cast<ptrdiff_t>(trimStart),
                              duckedExport.begin() + static_cast<ptrdiff_t>(n));
    std::vector<float> clean(cleanExport.begin() + static_cast<ptrdiff_t>(trimStart),
                             cleanExport.begin() + static_cast<ptrdiff_t>(n));

    const double duckedRms = rmsOf(ducked);
    const double cleanRms = rmsOf(clean);
    const double levelDeltaDb =
        20.0 * std::log10(std::max(duckedRms, 1e-15) / std::max(cleanRms, 1e-15));

    DiffReport r = compareBuffers(ducked, clean, cfg.channels, cfg.sampleRate, 1e-6);
    const bool pass = (r.rmsErrorDb <= -120.0) && (r.maxAbsError <= 1e-6);
    printReport("Export_Immune_To_Preview_Ducking", r, pass,
                "export with audible preview must equal clean export (RMS <= -120 dB, maxAbs <= 1e-6)");
    std::cout << "  export level delta vs clean: " << levelDeltaDb << " dB"
              << (pass ? "" : "  <-- preview ducking contaminated the export") << "\n";
    return pass;
}

// -----------------------------------------------------------------------------
// Case 3: exporting above the live sample rate preserves the whole timeline
// -----------------------------------------------------------------------------
bool runCrossSampleRateCase(const SessionConfig& liveCfg, const fs::path& tempRoot) {
    constexpr uint32_t exportRate = 96000;
    const uint32_t sourceFrames = liveCfg.sampleRate * kSeconds;
    auto tm = buildSession(liveCfg, sourceFrames);

    AudioEngine engine;
    prepareEngine(engine, tm, liveCfg);
    applyParams(engine);
    warmupEngine(engine, liveCfg);

    AudioExporter exporter(engine, *tm);
    AudioExporter::Config config;
    config.outputPath = (tempRoot / "parity_48_to_96.wav").string();
    config.scope = AudioExporter::RenderScope::FullSong;
    config.startBeat = 0.0;
    config.endBeat = kBeats;
    config.sampleRate = exportRate;
    config.bitDepth = AudioExporter::BitDepth::Float_32;
    config.numChannels = liveCfg.channels;
    config.tailSeconds = 0.0;

    const auto result = exporter.render(config);
    if (!result.success) {
        std::cerr << "cross-sample-rate export failed: " << result.errorMessage << "\n";
        return false;
    }

    std::vector<float> decoded;
    uint32_t decodedRate = 0;
    uint32_t decodedChannels = 0;
    if (!decodeAudioFile(config.outputPath, decoded, decodedRate, decodedChannels)) {
        std::cerr << "failed to decode cross-sample-rate export\n";
        return false;
    }

    const size_t decodedFrames = decodedChannels > 0 ? decoded.size() / decodedChannels : 0;
    const size_t expectedFrames = static_cast<size_t>(exportRate) * kSeconds;
    const double firstHalfRms = rmsRange(decoded, exportRate / 4, exportRate, decodedChannels);
    const double secondHalfRms = rmsRange(decoded, exportRate, exportRate * 7 / 4, decodedChannels);
    const double halfDeltaDb =
        20.0 * std::log10(std::max(secondHalfRms, 1e-15) / std::max(firstHalfRms, 1e-15));

    const bool pass = decodedRate == exportRate && decodedChannels == liveCfg.channels &&
                      decodedFrames == expectedFrames && secondHalfRms > 0.01 && std::abs(halfDeltaDb) < 1.0 &&
                      engine.getSampleRate() == liveCfg.sampleRate &&
                      tm->getPlaylistModel().getProjectSampleRate() == static_cast<double>(liveCfg.sampleRate);
    std::cout << (pass ? "[PASS]" : "[FAIL]") << " Export_48k_To_96k_Full_Timeline"
              << " frames=" << decodedFrames << "/" << expectedFrames << " firstHalfRms=" << firstHalfRms
              << " secondHalfRms=" << secondHalfRms << " delta=" << halfDeltaDb << " dB\n";
    return pass;
}

// -----------------------------------------------------------------------------
// Case 4: isolated-track bounce honors clip Speed (playbackRate) and Pitch
// (pitchSemitones) — parity with live playback of the same session (#745, #746)
// -----------------------------------------------------------------------------
bool runIsolatedSpeedParityCase(const SessionConfig& cfg, const fs::path& tempRoot, float speed,
                                float pitchSemitones = 0.0f) {
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;

    // One-track session with the clip sped up/down and/or pitched. The edits
    // are applied before engine prepare, so the compiled graph carries them.
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "SpeedA", makeSine(440.0, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    const auto laneId = tm->getPlaylistModel().getLaneId(0);
    const auto* lane = tm->getPlaylistModel().getLane(laneId);
    if (!lane || lane->clips.empty()) {
        std::cerr << "no clip on lane 0; cannot apply edits\n";
        return false;
    }
    ClipEdits edits;
    edits.playbackRate = speed;
    edits.pitchSemitones = pitchSemitones;
    if (!tm->getPlaylistModel().setClipEdits(lane->clips.front().id, edits)) {
        std::cerr << "setClipEdits failed\n";
        return false;
    }

    // Live render of the sped session.
    AudioEngine liveEngine;
    prepareEngine(liveEngine, tm, cfg);
    warmupEngine(liveEngine, cfg);
    liveEngine.setTransportPlaying(true);
    std::vector<float> realtime = renderBlocks(liveEngine, totalFrames, cfg);
    liveEngine.setTransportPlaying(false);

    // Isolated-track bounce (trackId 0) of the same session.
    AudioEngine bounceEngine;
    prepareEngine(bounceEngine, tm, cfg);
    warmupEngine(bounceEngine, cfg);
    const fs::path outPath = tempRoot / "parity_speed_isolated.wav";
    if (!bounceEngine.bounceRangeToWav(0.0, kBeats, outPath.string(), 0)) {
        std::cerr << "isolated bounceRangeToWav failed for " << outPath << "\n";
        return false;
    }
    std::vector<float> bounced;
    uint32_t sr = 0, ch = 0;
    if (!decodeAudioFile(outPath.string(), bounced, sr, ch)) {
        std::cerr << "failed to decode " << outPath << "\n";
        return false;
    }
    if (sr != cfg.sampleRate || ch != cfg.channels) {
        std::cerr << "isolated bounce format mismatch: " << sr << " Hz / " << ch << " ch\n";
        return false;
    }

    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t n = std::min(realtime.size(), bounced.size());
    if (n <= trimStart) {
        std::cerr << "not enough overlap to compare (" << n << " samples)\n";
        return false;
    }
    std::vector<float> rt(realtime.begin() + static_cast<ptrdiff_t>(trimStart),
                          realtime.begin() + static_cast<ptrdiff_t>(n));
    std::vector<float> iso(bounced.begin() + static_cast<ptrdiff_t>(trimStart),
                           bounced.begin() + static_cast<ptrdiff_t>(n));

    DiffReport r = compareBuffers(rt, iso, cfg.channels, cfg.sampleRate, 1e-6);
    const bool pass = (r.rmsErrorDb <= -120.0) && (r.maxAbsError <= 1e-6);
    const std::string label = "Isolated_Bounce_Speed" + std::to_string(speed) + "x_Pitch" +
                              std::to_string(pitchSemitones) + "st_vs_Live";
    printReport(label, r, pass,
                "isolated bounce at speed " + std::to_string(speed) + " / pitch " +
                    std::to_string(pitchSemitones) +
                    " st must equal live playback with the same edits (RMS <= -120 dB, maxAbs <= 1e-6)");
    std::cout << "  live frames: " << realtime.size() / cfg.channels
              << ", isolated bounce frames: " << bounced.size() / cfg.channels << "\n";
    return pass;
}

// -----------------------------------------------------------------------------
// Case 5: splitting a varispeed clip must not change what the track plays —
// the two halves spliced together must equal the un-split clip outside the
// engine's mandatory 128-frame clip-edge fades (kClipEdgeFadeSamples /
// CLIP_EDGE_FADE_SAMPLES, both render paths apply them at every clip
// boundary). User report: "slicing pitched clips is not slicing as intended".
// -----------------------------------------------------------------------------
bool runSplitVarispeedParityCase(const SessionConfig& cfg, const fs::path& tempRoot, float speed,
                                 float pitchSemitones, double splitBeat) {
    // 3 s source: the clip (2 s of timeline at varispeed 2) consumes only the
    // first 2 s of source, so the right half after a beat-2 split has audible
    // content — the region the offset bug played at the wrong position.
    const uint32_t totalFrames = cfg.sampleRate * 3;

    auto buildSplitSession = [&](bool split) -> std::shared_ptr<TrackManager> {
        auto tm = std::make_shared<TrackManager>();
        tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
        addAudioTrack(*tm, "SplitVarispeed", makeSine(440.0, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);
        const auto laneId = tm->getPlaylistModel().getLaneId(0);
        const auto* lane = tm->getPlaylistModel().getLane(laneId);
        if (!lane || lane->clips.empty()) {
            return nullptr;
        }
        ClipEdits edits;
        edits.playbackRate = speed;
        edits.pitchSemitones = pitchSemitones;
        if (!tm->getPlaylistModel().setClipEdits(lane->clips.front().id, edits)) {
            return nullptr;
        }
        if (split && !tm->getPlaylistModel().splitClip(lane->clips.front().id, splitBeat).isValid()) {
            return nullptr;
        }
        return tm;
    };

    const auto whole = buildSplitSession(false);
    const auto split = buildSplitSession(true);
    if (!whole || !split) {
        std::cerr << "split parity session construction failed\n";
        return false;
    }

    auto bounceTrack = [&](const std::shared_ptr<TrackManager>& tm, const std::string& name) -> std::vector<float> {
        AudioEngine engine;
        prepareEngine(engine, tm, cfg);
        warmupEngine(engine, cfg);
        const fs::path outPath = tempRoot / ("parity_" + name + ".wav");
        std::vector<float> decoded;
        if (!engine.bounceRangeToWav(0.0, kBeats, outPath.string(), 0)) {
            return decoded;
        }
        uint32_t sr = 0, ch = 0;
        if (!decodeAudioFile(outPath.string(), decoded, sr, ch) || sr != cfg.sampleRate || ch != cfg.channels) {
            decoded.clear();
        }
        return decoded;
    };

    const std::vector<float> wholeBounce = bounceTrack(whole, "split_whole");
    const std::vector<float> splitBounce = bounceTrack(split, "split_halves");
    if (wholeBounce.empty() || splitBounce.empty()) {
        std::cerr << "split parity bounce failed\n";
        return false;
    }

    const size_t trimStart = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels;
    const size_t n = std::min(wholeBounce.size(), splitBounce.size());
    if (n <= trimStart) {
        std::cerr << "not enough overlap to compare split parity\n";
        return false;
    }

    // Exclude the 128-frame clip-edge fades both render paths apply at the
    // splice point: the split creates clip edges where the whole clip has
    // continuous content. Everything else must be bit-identical.
    const size_t splitFrame = static_cast<size_t>(splitBeat * cfg.sampleRate * 60.0 / cfg.bpm);
    constexpr uint32_t kEdgeFade = 128;
    const size_t fadeLo = (splitFrame > kEdgeFade) ? splitFrame - kEdgeFade : 0;
    const size_t fadeHi = std::min(splitFrame + kEdgeFade, n / cfg.channels);

    auto maskedCompare = [&](const std::vector<float>& a, const std::vector<float>& b) {
        double sumSq = 0.0;
        size_t compared = 0;
        double maxAbs = 0.0;
        size_t firstMismatch = SIZE_MAX;
        for (size_t f = trimStart / cfg.channels; f < n / cfg.channels; ++f) {
            if (f >= fadeLo && f < fadeHi) {
                continue;
            }
            for (uint32_t c = 0; c < cfg.channels; ++c) {
                const double da = a[f * cfg.channels + c];
                const double db = b[f * cfg.channels + c];
                const double err = std::abs(da - db);
                if (err > maxAbs) {
                    maxAbs = err;
                }
                if (err > 1e-6 && firstMismatch == SIZE_MAX) {
                    firstMismatch = f;
                }
                sumSq += err * err;
                ++compared;
            }
        }
        const double rms = std::sqrt(sumSq / static_cast<double>(compared));
        const double rmsDb = 20.0 * std::log10(std::max(rms, 1e-15));
        return std::make_tuple(rmsDb, maxAbs, firstMismatch, compared);
    };

    const auto [rmsDb, maxAbs, firstMismatch, compared] =
        maskedCompare(wholeBounce, splitBounce);
    const bool pass = (rmsDb <= -120.0) && (maxAbs <= 1e-6);
    const std::string label = "Split_Splice_Speed" + std::to_string(speed) + "x_Pitch" +
                              std::to_string(pitchSemitones) + "st_vs_Whole";
    std::cout << (pass ? "[PASS] " : "[FAIL] ") << label << "  rmsErr=" << rmsDb
              << " dB  maxAbsErr=" << maxAbs << "  compared=" << compared << " frames";
    if (!pass) {
        std::cout << "  firstMismatch=" << firstMismatch << " (" << (firstMismatch / static_cast<double>(cfg.sampleRate))
                  << " s)";
    }
    std::cout << "\n";
    std::cout << "  (fade zone excluded: frames " << fadeLo << ".." << fadeHi - 1
              << " around the splice at " << splitFrame << ")\n";
    std::cout << "  whole bounce frames: " << wholeBounce.size() / cfg.channels
              << ", split bounce frames: " << splitBounce.size() / cfg.channels << "\n";
    return pass;
}

// -----------------------------------------------------------------------------
// Case 6: isolated-track bounce excludes the master stage entirely
// (isolated-bounce contract, 2026-08-14). A master-routed clip (997 Hz) and a
// channel clip (440 Hz) share the session; bouncing the channel in isolation
// must deliver the 440 Hz tone and NOT the 997 Hz master content.
// -----------------------------------------------------------------------------
bool runIsolatedBounceExcludesMasterClips(const SessionConfig& cfg, const fs::path& tempRoot) {
    constexpr double kMasterToneHz = 997.0;
    constexpr double kChannelToneHz = 440.0;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;

    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));

    // Channel clip: 440 Hz sine via the standard harness (routes to channel 1).
    addAudioTrack(*tm, "ChannelA", makeSine(kChannelToneHz, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);

    // Master clip: same construction but the pattern targets mixerChannelId 0.
    {
        auto buffer = std::make_shared<AudioBufferData>();
        buffer->sampleRate = cfg.sampleRate;
        buffer->numChannels = cfg.channels;
        buffer->numFrames = totalFrames;
        buffer->interleavedData = makeSine(kMasterToneHz, 0.25f, totalFrames, cfg.sampleRate);

        const std::string path =
            (std::filesystem::temp_directory_path() / "aestra_rt_export_parity_master_clip.wav").string();
        ClipSourceID sourceId = tm->getSourceManager().createRecordedSource(path, "MasterClip", buffer);

        AudioSlicePayload payload;
        payload.audioSourceId = sourceId;
        payload.durationSeconds = static_cast<double>(totalFrames) / cfg.sampleRate;
        payload.slices.push_back({0.0, payload.durationSeconds, 0.0, static_cast<double>(totalFrames)});

        PlaylistLaneID laneId = tm->getPlaylistModel().createLane("MasterLane");
        const double durationBeats = payload.durationSeconds * (static_cast<double>(cfg.bpm) / 60.0);
        PatternID patternId = tm->getPatternManager().createAudioPattern("MasterClip", durationBeats, payload);
        tm->getPatternManager().setPatternMixerChannel(patternId, 0);
        const ClipInstanceID clipId = tm->getPlaylistModel().addClipFromPattern(laneId, patternId, 0.0, durationBeats);
        if (!tm->getPlaylistModel().setClipEdits(clipId, ClipEdits{})) {
            std::cerr << "setClipEdits (master clip) failed\n";
            return false;
        }
    }

    // Isolated-track bounce of the channel (trackId 0).
    AudioEngine bounceEngine;
    prepareEngine(bounceEngine, tm, cfg);
    warmupEngine(bounceEngine, cfg);
    const fs::path outPath = tempRoot / "parity_isolated_excludes_master.wav";
    if (!bounceEngine.bounceRangeToWav(0.0, kBeats, outPath.string(), 0)) {
        std::cerr << "isolated bounceRangeToWav failed\n";
        return false;
    }
    std::vector<float> bounced;
    uint32_t sr = 0, ch = 0;
    if (!decodeAudioFile(outPath.string(), bounced, sr, ch)) {
        std::cerr << "failed to decode isolated bounce\n";
        return false;
    }
    if (sr != cfg.sampleRate || ch != cfg.channels) {
        std::cerr << "isolated bounce format mismatch\n";
        return false;
    }

    // Steady-region correlation with each tone: amplitude estimate via
    // sine/cosine projection (both tones are separable by construction).
    const size_t firstFrame = static_cast<size_t>(cfg.blockSize) * 4;
    const size_t endFrame = static_cast<size_t>(totalFrames) - 256;
    const auto toneAmplitude = [&](double freq) {
        double sinAcc = 0.0, cosAcc = 0.0;
        for (size_t n = firstFrame; n < endFrame; ++n) {
            const double phase = kTau * freq * static_cast<double>(n) / sr;
            const double s = static_cast<double>(bounced[n * 2]);
            sinAcc += s * std::sin(phase);
            cosAcc += s * std::cos(phase);
        }
        const double count = static_cast<double>(endFrame - firstFrame);
        return 2.0 * std::sqrt(sinAcc * sinAcc + cosAcc * cosAcc) / count;
    };

    const double channelAmp = toneAmplitude(kChannelToneHz);
    const double masterAmp = toneAmplitude(kMasterToneHz);
    const bool pass = channelAmp > 0.15 && masterAmp < 0.005;
    std::cout << (pass ? "[PASS]" : "[FAIL]") << " Isolated_Bounce_Excludes_MasterClips"
              << " channel440=" << channelAmp << " master997=" << masterAmp << "\n";
    if (!pass) {
        std::cerr << "  contract: isolated bounce renders only the selected track's stage;"
                     " master content must be absent\n";
    }
    return pass;
}


} // namespace

int main() {
    std::cout << "=== Aestra Realtime/Export Parity Test ===\n\n";
    const fs::path tempRoot = fs::temp_directory_path() / "aestra_rt_export_parity";
    std::error_code ec;
    fs::create_directories(tempRoot, ec);

    SessionConfig cfg;
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;
    auto tm = buildSession(cfg, totalFrames);

    // Clean export once; both cases compare against it.
    std::vector<float> cleanExport;
    if (!runExport(tm, cfg, tempRoot / "parity_clean.wav", nullptr, 0.0f, cleanExport)) {
        return 1;
    }

    int failures = 0;
    if (!runParityCase(tm, cfg, cleanExport)) ++failures;
    if (!runDuckContaminationCase(tm, cfg, cleanExport, tempRoot)) ++failures;
    if (!runCrossSampleRateCase(cfg, tempRoot)) ++failures;
    for (float speed : {2.0f, 0.5f}) {
        if (!runIsolatedSpeedParityCase(cfg, tempRoot, speed)) ++failures;
    }
    if (!runIsolatedBounceExcludesMasterClips(cfg, tempRoot)) ++failures;
    // Pitch cases (#746): pure pitch, pitch cancelling speed, and interplay.
    for (const auto& [speed, pitch] : {std::pair{1.0f, 12.0f}, {2.0f, -12.0f}, {0.5f, 7.0f}}) {
        if (!runIsolatedSpeedParityCase(cfg, tempRoot, speed, pitch)) ++failures;
    }
    // Split cases: splicing the halves must equal the un-split clip, under
    // pitch varispeed and under speed varispeed (split at beat 2 of 4).
    for (const auto& [speed, pitch] : {std::pair{1.0f, 12.0f}, {2.0f, 0.0f}}) {
        if (!runSplitVarispeedParityCase(cfg, tempRoot, speed, pitch, 2.0)) ++failures;
    }

    fs::remove_all(tempRoot, ec);
    std::cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
