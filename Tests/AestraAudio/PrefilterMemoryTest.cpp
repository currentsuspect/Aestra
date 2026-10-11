// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PrefilterMemoryTest — the anti-alias copy of a downsampled source is a whole extra float32 copy of it,
// so it must not cost more memory than it has to:
//   1. applyStreaming() filters block by block (a compact PCM source is converted as it goes) and gives
//      EXACTLY apply()'s result, for blocks shorter and longer than the clip and for the clip's edges;
//   2. a filtered copy is made only for a source a clip plays, and is freed when no clip plays it.

#include "Core/ClipRenderKernel.h"
#include "DSP/ClipPrefilter.h"
#include "Models/ClipSource.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

std::shared_ptr<AudioBufferData> pcm16Source(uint32_t channels, uint64_t frames, uint32_t rate, unsigned seed) {
    auto b = std::make_shared<AudioBufferData>();
    b->numChannels = channels;
    b->numFrames = frames;
    b->sampleRate = rate;
    b->interleavedData.resize(frames * channels);
    std::mt19937 rng(seed);
    for (auto& s : b->interleavedData)
        s = static_cast<float>(static_cast<int>(rng() % 60001) - 30000) / 32768.0f;
    return b;
}

bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
} // namespace

int main() {
    std::cout << "[1] applyStreaming == apply, bit for bit\n";
    struct Case { uint32_t ch; uint64_t frames; uint32_t src; uint32_t dst; };
    for (const Case c : {Case{2, 300, 96000, 48000}, Case{2, 40000, 96000, 48000}, Case{1, 70001, 88200, 44100},
                         Case{2, 16384, 96000, 48000}, Case{2, 16385, 192000, 48000}}) {
        auto floatSrc = pcm16Source(c.ch, c.frames, c.src, 7);
        auto compact = std::make_shared<AudioBufferData>(*floatSrc);
        compact->tryCompact();
        const auto h = ClipPrefilter::design(c.src, c.dst);
        std::vector<float> want(floatSrc->interleavedData.size());
        ClipPrefilter::apply(floatSrc->interleavedData.data(), want.data(), c.frames, c.ch, h);
        for (const auto* source : {floatSrc.get(), compact.get()}) {
            std::vector<float> got(want.size());
            ClipPrefilter::applyStreaming(
                [source, ch = c.ch](uint64_t first, uint64_t n, float* dst) { source->copyFloats(first * ch, n * ch, dst); },
                got.data(), c.frames, c.ch, h);
            check(bitEqual(want, got), std::to_string(c.frames) + " frames x" + std::to_string(c.ch) + " " +
                                           std::to_string(c.src) + "->" + std::to_string(c.dst) +
                                           (source->isCompact() ? " from compact PCM" : " from float32"));
        }
    }

    std::cout << "[1b] decimated output == every Nth frame of apply(), bit for bit\n";
    for (const Case c : {Case{2, 5000, 96000, 48000}, Case{2, 70001, 192000, 48000}, Case{1, 40000, 88200, 44100}}) {
        auto src = pcm16Source(c.ch, c.frames, c.src, 9);
        const auto h = ClipPrefilter::design(c.src, c.dst);
        const uint32_t n = ClipPrefilter::decimationFactor(c.src, c.dst);
        std::vector<float> full(src->interleavedData.size());
        ClipPrefilter::apply(src->interleavedData.data(), full.data(), c.frames, c.ch, h);
        const uint64_t outFrames = (c.frames + n - 1) / n;
        std::vector<float> want(outFrames * c.ch), got(outFrames * c.ch);
        for (uint64_t m = 0; m < outFrames; ++m)
            for (uint32_t ch = 0; ch < c.ch; ++ch)
                want[m * c.ch + ch] = full[m * n * c.ch + ch];
        ClipPrefilter::applyStreaming(
            [&src, ch = c.ch](uint64_t first, uint64_t cnt, float* dst) { src->copyFloats(first * ch, cnt * ch, dst); },
            got.data(), c.frames, c.ch, h, n);
        check(n >= 2 && bitEqual(want, got), "x" + std::to_string(n) + " decimation of " + std::to_string(c.frames) +
                                                  " frames at " + std::to_string(c.src) + "->" + std::to_string(c.dst));
    }
    check(ClipPrefilter::decimationFactor(96000, 48000) == 2 && ClipPrefilter::decimationFactor(192000, 48000) == 4 &&
              ClipPrefilter::decimationFactor(88200, 48000) == 1 && ClipPrefilter::decimationFactor(48000, 48000) == 1 &&
              ClipPrefilter::decimationFactor(44100, 48000) == 1,
          "only whole-number ratios decimate (88.2k -> 48k and anything not downsampled keep the full-rate copy)");

    std::cout << "[1c] the kernel plays the decimated copy as it played the full-rate one\n";
    {
        constexpr uint32_t kSrc = 96000, kDst = 48000;
        auto src = pcm16Source(2, 48000, kSrc, 11);
        const char* sourceName = std::getenv("PFM_SOURCE") ? std::getenv("PFM_SOURCE") : "noise";
        if (std::string(sourceName) == "music") { // tones to 15 kHz plus broadband at about -40 dB
            std::mt19937 rng(5);
            for (uint64_t f = 0; f < src->numFrames; ++f)
                for (uint32_t ch = 0; ch < 2; ++ch) {
                    double v = 0.0;
                    for (int t = 0; t < 9; ++t) v += 0.07 * std::sin(6.2831853 * (200.0 * (t + 1) * (t + 1) + 37 * ch) * f / kSrc);
                    v += 0.01 * (static_cast<int>(rng() % 20001) - 10000) / 10000.0;
                    src->interleavedData[f * 2 + ch] = static_cast<float>(std::nearbyint(v * 32768.0) / 32768.0);
                }
        }
        const auto h = ClipPrefilter::design(kSrc, kDst);
        auto fullBuf = std::make_shared<AudioBufferData>();
        fullBuf->numChannels = 2; fullBuf->numFrames = src->numFrames; fullBuf->sampleRate = kSrc;
        fullBuf->interleavedData.resize(src->interleavedData.size());
        ClipPrefilter::apply(src->interleavedData.data(), fullBuf->interleavedData.data(), src->numFrames, 2, h);
        auto decBuf = std::make_shared<AudioBufferData>();
        decBuf->numChannels = 2; decBuf->numFrames = src->numFrames / 2; decBuf->sampleRate = kDst;
        decBuf->interleavedData.resize(decBuf->numFrames * 2);
        ClipPrefilter::applyStreaming(
            [&src](uint64_t first, uint64_t cnt, float* dst) { src->copyFloats(first * 2, cnt * 2, dst); },
            decBuf->interleavedData.data(), src->numFrames, 2, h, 2);

        for (const float speed : {1.0f, 0.75f, 1.5f}) {
            auto render = [&](const std::shared_ptr<AudioBufferData>& buf) {
                ClipRenderState c;
                c.bufferOwner = buf; c.audioData = buf->interleavedData.data(); c.totalFrames = buf->numFrames;
                c.sourceSampleRate = buf->sampleRate; c.channels = 2; c.startSample = 4000; c.endSample = 4000 + 20000;
                c.sampleOffset = 0.0; c.playbackRate = speed; c.gain = 1.0f;
                std::vector<double> out(2 * 30000, 0.0);
                (void)ClipRenderKernel::renderClipInto(c, 0, 30000, out.data(), kDst, Interpolators::InterpolationQuality::Sinc64);
                return out;
            };
            const auto a = render(fullBuf), b = render(decBuf);
            double worst = 0.0, peak = 0.0;
            // Skip the clip's 128-frame edge ramps and the file's first/last kernel reach, where both paths clamp.
            for (size_t i = 2 * (4000 + 400); i + 2 * 3000 < a.size(); ++i) {
                worst = std::max(worst, std::fabs(a[i] - b[i]));
                peak = std::max(peak, std::fabs(a[i]));
            }
            std::cout << "    speed " << speed << ": worst difference " << worst << " (peak " << peak << ")\n";
            if (speed == 1.0f) {
                check(worst == 0.0, "speed 1: the decimated copy plays BIT-IDENTICALLY to the full-rate copy");
            } else {
                std::cout << "    (at another speed they differ: why TrackManager keeps the full-rate copy there)\n";
            }
        }
    }

    std::cout << "[2] a filtered copy exists only for a source a clip plays\n";
    {
        TrackManager tm;
        tm.setOutputSampleRate(48000.0);
        tm.getPlaylistModel().setProjectSampleRate(48000.0);
        tm.getPlaylistModel().setBPM(120.0);

        auto played = pcm16Source(2, 96000, 96000, 1);   // 1 s at 96 kHz
        auto unused = pcm16Source(2, 96000, 96000, 2);
        const ClipSourceID playedId = tm.getSourceManager().createRecordedSource("played.wav", "played", played);
        const ClipSourceID unusedId = tm.getSourceManager().createRecordedSource("unused.wav", "unused", unused);

        AudioSlicePayload payload;
        payload.audioSourceId = playedId;
        payload.durationSeconds = 1.0;
        payload.slices.push_back({0.0, 1.0, 0.0, 96000.0});
        const PatternID pid = tm.getPatternManager().createAudioPattern("played", 2.0, payload);
        const auto lane = tm.getPlaylistModel().createLane("L");
        const ClipInstanceID clip = tm.getPlaylistModel().addClipFromPattern(lane, pid, 0.0, 2.0);

        tm.ensureClipPrefilters();
        tm.waitForClipPrefilters();
        check(tm.getSourceManager().getSource(playedId)->getFilteredBufferFor(48000) != nullptr,
              "the played source got its anti-alias copy");
        check(tm.getSourceManager().getSource(unusedId)->getFilteredBufferFor(48000) == nullptr,
              "the unused source did not");

        tm.getPlaylistModel().removeClip(clip);
        tm.ensureClipPrefilters();
        check(!tm.getSourceManager().getSource(playedId)->hasFilteredVariant(),
              "and the copy is freed once no clip plays it");
    }

    std::cout << "[3] decimated at speed 1; the full-rate copy once a clip plays it at another speed (and it sticks)\n";
    {
        TrackManager tm;
        tm.setOutputSampleRate(48000.0);
        tm.getPlaylistModel().setProjectSampleRate(48000.0);
        tm.getPlaylistModel().setBPM(120.0);
        auto source = pcm16Source(2, 48000, 96000, 3);
        const ClipSourceID id = tm.getSourceManager().createRecordedSource("s96.wav", "s96", source);
        AudioSlicePayload payload;
        payload.audioSourceId = id;
        payload.durationSeconds = 0.5;
        payload.slices.push_back({0.0, 0.5, 0.0, 48000.0});
        const PatternID pid = tm.getPatternManager().createAudioPattern("s96", 1.0, payload);
        const auto lane = tm.getPlaylistModel().createLane("L");
        const ClipInstanceID clip = tm.getPlaylistModel().addClipFromPattern(lane, pid, 0.0, 1.0);
        tm.getPlaylistModel().setClipEdits(clip, ClipEdits{});

        auto variantRate = [&]() {
            tm.ensureClipPrefilters();
            tm.waitForClipPrefilters();
            const auto v = tm.getSourceManager().getSource(id)->getFilteredBufferFor(48000);
            return v ? v->sampleRate : 0u;
        };
        check(variantRate() == 48000, "unity speed: the copy is stored decimated, at the project rate");
        const auto decimated = tm.getSourceManager().getSource(id)->getFilteredBufferFor(48000);
        check(decimated && decimated->numFrames == 24000, "half the frames (24000 of 48000)");

        ClipEdits slow;
        slow.playbackRate = 0.8f;
        tm.getPlaylistModel().setClipEdits(clip, slow);
        tm.ensureClipPrefilters(); // queues the upgrade; the decimated copy keeps playing meanwhile
        check(tm.getSourceManager().getSource(id)->getFilteredBufferFor(48000) != nullptr,
              "while the full-rate copy builds, the decimated one is still there");
        check(variantRate() == 96000, "a clip at another speed: the full-rate copy replaces it");

        tm.getPlaylistModel().setClipEdits(clip, ClipEdits{});
        check(variantRate() == 96000, "back at unity the full-rate copy stays (no rebuild churn)");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
