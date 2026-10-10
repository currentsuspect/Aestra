// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// GraphRenderBufferTest — V8-W4, the graph-level renderer.
//
// AudioExporter::renderToBuffer() renders the project's master output into
// memory. It is NOT a second renderer: it is AudioExporter::render() with an
// in-memory block sink, so the claims below hold by construction and this test
// pins them:
//   1. the memory render is bit-identical to the file render of the same range;
//   2. it matches live playback (the same -120 dB bar RealtimeExportParityTest
//      holds the file path to);
//   3. a sub-range renders exactly that range (what Bounce in Place will ask);
//   4. a sink that stops the render leaves nothing on disk, and every render
//      leaves the engine as it found it.

#include "GoldenAudio/GoldenAudioHarness.h"

#include "DSP/ContinuousParamBuffer.h"
#include "IO/AudioExporter.h"
#include "IO/MiniAudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace Aestra::Audio;
using namespace GoldenAudio;
namespace fs = std::filesystem;

namespace {

constexpr double kTau = 6.28318530717958647692;
constexpr uint32_t kSeconds = 2;
constexpr double kBeats = 4.0; // 2 s at 120 BPM

int g_failures = 0;
void check(bool cond, const std::string& what) {
    std::cout << "  " << (cond ? "PASS " : "FAIL ") << what << "\n";
    if (!cond) {
        ++g_failures;
    }
}

std::vector<float> makeSine(double freqHz, float amplitude, uint32_t frames, uint32_t sampleRate) {
    std::vector<float> s(static_cast<size_t>(frames) * 2, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = static_cast<float>(std::sin(kTau * freqHz * static_cast<double>(i) / sampleRate)) * amplitude;
        s[static_cast<size_t>(i) * 2] = v;
        s[static_cast<size_t>(i) * 2 + 1] = v;
    }
    return s;
}

std::shared_ptr<TrackManager> buildSession(const SessionConfig& cfg) {
    const uint32_t totalFrames = cfg.sampleRate * kSeconds;
    auto tm = std::make_shared<TrackManager>();
    tm->setOutputSampleRate(static_cast<double>(cfg.sampleRate));
    addAudioTrack(*tm, "A", makeSine(440.0, 0.30f, totalFrames, cfg.sampleRate), totalFrames, cfg);
    addAudioTrack(*tm, "B", makeSine(660.0, 0.20f, totalFrames, cfg.sampleRate), totalFrames, cfg);
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

// The app's engine is long-lived and warm; warm ours the same way the parity test does.
void warm(AudioEngine& engine, const SessionConfig& cfg) {
    engine.setTransportPlaying(true);
    std::vector<float> block(static_cast<size_t>(cfg.blockSize) * cfg.channels, 0.0f);
    for (int i = 0; i < 8; ++i) {
        std::fill(block.begin(), block.end(), 0.0f);
        engine.processBlock(block.data(), nullptr, cfg.blockSize, 0.0);
    }
    engine.setTransportPlaying(false);
    engine.setGlobalSamplePos(0);
}

AudioExporter::Config baseConfig(const SessionConfig& cfg) {
    AudioExporter::Config c;
    c.scope = AudioExporter::RenderScope::FullSong;
    c.startBeat = 0.0;
    c.endBeat = kBeats;
    c.sampleRate = cfg.sampleRate;
    c.bitDepth = AudioExporter::BitDepth::Float_32;
    c.numChannels = cfg.channels;
    c.tailSeconds = 0.0;
    return c;
}

} // namespace

int main() {
    SessionConfig cfg;
    const fs::path tmp = fs::temp_directory_path() / "aestra_graph_render_buffer_test";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    auto freshEngine = [&](const std::shared_ptr<TrackManager>& tm) {
        auto engine = std::make_unique<AudioEngine>();
        prepareEngine(*engine, tm, cfg);
        applyParams(*engine);
        warm(*engine, cfg);
        return engine;
    };

    std::cout << "[1] memory render == file render, bit for bit\n";
    std::vector<float> fileAudio;
    AudioExporter::RenderedAudio mem;
    {
        auto tm = buildSession(cfg);
        auto engine = freshEngine(tm);
        AudioExporter exporter(*engine, *tm);
        auto c = baseConfig(cfg);
        c.outputPath = (tmp / "file.wav").string();
        const auto r = exporter.render(c);
        check(r.success, "the file render succeeds");
        uint32_t sr = 0, ch = 0;
        check(decodeAudioFile(c.outputPath, fileAudio, sr, ch) && sr == cfg.sampleRate && ch == cfg.channels,
              "the file decodes at the render's rate and channel count");
    }
    {
        auto tm = buildSession(cfg);
        auto engine = freshEngine(tm);
        AudioExporter exporter(*engine, *tm);
        const auto r = exporter.renderToBuffer(baseConfig(cfg), mem);
        check(r.success, "the memory render succeeds");
        check(r.outputPath.empty(), "no output path is reported for a memory render");
        check(mem.sampleRate == cfg.sampleRate && mem.numChannels == cfg.channels, "the buffer carries its format");
        check(mem.frames == static_cast<uint64_t>(cfg.sampleRate) * kSeconds, "two seconds of frames");
        check(mem.interleaved.size() == mem.frames * mem.numChannels, "the buffer holds exactly frames * channels samples");
    }
    {
        const auto d = compareBuffers(mem.interleaved, fileAudio, cfg.channels, cfg.sampleRate, 0.0);
        check(mem.interleaved.size() == fileAudio.size(), "same length as the file render");
        check(d.maxAbsError == 0.0, "every sample identical to the Float_32 file render");
        check(d.peakActual > 0.1, "the render is not silence");
    }

    std::cout << "[2] memory render == live playback\n";
    {
        auto tm = buildSession(cfg);
        auto engine = freshEngine(tm);
        engine->setTransportPlaying(true);
        const std::vector<float> realtime = renderBlocks(*engine, mem.frames, cfg);
        engine->setTransportPlaying(false);
        const size_t trim = static_cast<size_t>(cfg.blockSize) * 4 * cfg.channels; // past the transport fade-in both paths do
        const size_t n = std::min(realtime.size(), mem.interleaved.size());
        check(n > trim, "enough overlap to compare");
        std::vector<float> rt(realtime.begin() + static_cast<ptrdiff_t>(trim), realtime.begin() + static_cast<ptrdiff_t>(n));
        std::vector<float> ex(mem.interleaved.begin() + static_cast<ptrdiff_t>(trim),
                              mem.interleaved.begin() + static_cast<ptrdiff_t>(n));
        const auto d = compareBuffers(rt, ex, cfg.channels, cfg.sampleRate, 1e-6);
        printReport("MemoryRender_vs_Realtime", d, d.rmsErrorDb <= -120.0 && d.maxAbsError <= 1e-6,
                    "RMS <= -120 dB and maxAbs <= 1e-6, the bar the file path is held to");
        check(d.rmsErrorDb <= -120.0 && d.maxAbsError <= 1e-6, "matches playback to the same bar as the export");
    }

    std::cout << "[3] a sub-range renders exactly that range\n";
    {
        auto tm = buildSession(cfg);
        auto engine = freshEngine(tm);
        AudioExporter exporter(*engine, *tm);
        auto c = baseConfig(cfg);
        c.scope = AudioExporter::RenderScope::Selection;
        c.startTimeSeconds = 0.5;
        c.endTimeSeconds = 1.5;
        AudioExporter::RenderedAudio part;
        const auto r = exporter.renderToBuffer(c, part);
        check(r.success, "the range render succeeds");
        check(part.frames == cfg.sampleRate, "one second of frames for a one-second range");
        const size_t from = static_cast<size_t>(cfg.sampleRate / 2) * cfg.channels;
        const size_t len = static_cast<size_t>(part.frames) * cfg.channels;
        check(from + len <= mem.interleaved.size(), "the range lies inside the full render");
        if (from + len <= mem.interleaved.size()) {
            const std::vector<float> slice(mem.interleaved.begin() + static_cast<ptrdiff_t>(from),
                                           mem.interleaved.begin() + static_cast<ptrdiff_t>(from + len));
            const auto d = compareBuffers(part.interleaved, slice, cfg.channels, cfg.sampleRate, 1e-6);
            // The engine fades the transport in over 256 frames so live playback never clicks. An offline
            // render must not: a range that starts mid-song has to begin at full level, or a bounce of it
            // would not equal what played (this measured 0.47 of error here before the engine skipped it).
            double startError = 0.0;
            for (size_t i = 0; i < static_cast<size_t>(256) * cfg.channels; ++i) {
                startError = std::max(startError, static_cast<double>(std::fabs(slice[i] - part.interleaved[i])));
            }
            check(startError == 0.0, "the first 256 frames of a mid-song range are not faded in");
            check(d.rmsErrorDb <= -120.0 && d.maxAbsError <= 1e-6, "the range equals the same slice of the full render");
        }
    }

    std::cout << "[4] a stopping sink leaves nothing behind; the engine is restored\n";
    {
        auto tm = buildSession(cfg);
        auto engine = freshEngine(tm);
        const auto rateBefore = engine->getSampleRate();
        const bool playingBefore = engine->isTransportPlaying();
        const auto posBefore = engine->getGlobalSamplePos();
        AudioExporter exporter(*engine, *tm);
        auto c = baseConfig(cfg);
        c.outputPath = (tmp / "must_not_exist.wav").string();
        int blocks = 0;
        c.blockSink = [&](const float*, uint32_t, uint32_t) { return ++blocks < 3; };
        const auto r = exporter.render(c);
        check(!r.success, "a sink that returns false fails the render");
        check(r.errorMessage.find("sink") != std::string::npos, "and says why");
        check(blocks == 3, "the render stopped at the block the sink refused");
        check(!fs::exists(c.outputPath), "no file was created for a sink render");
        check(engine->getSampleRate() == rateBefore && engine->isTransportPlaying() == playingBefore &&
                  engine->getGlobalSamplePos() == posBefore,
              "the engine's rate, transport and position are as they were");
        const auto again = exporter.renderToBuffer(baseConfig(cfg), mem);
        check(again.success, "the exporter renders again afterwards");
    }

    fs::remove_all(tmp, ec);
    std::cout << (g_failures == 0 ? "[PASS] GraphRenderBufferTest\n" : "[FAIL] GraphRenderBufferTest\n");
    return g_failures == 0 ? 0 : 1;
}
