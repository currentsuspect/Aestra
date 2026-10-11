// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PrefilterMemoryTest — the anti-alias copy of a downsampled source is a whole extra float32 copy of it,
// so it must not cost more memory than it has to:
//   1. applyStreaming() filters block by block (a compact PCM source is converted as it goes) and gives
//      EXACTLY apply()'s result, for blocks shorter and longer than the clip and for the clip's edges;
//   2. a filtered copy is made only for a source a clip plays, and is freed when no clip plays it.

#include "DSP/ClipPrefilter.h"
#include "Models/ClipSource.h"
#include "Models/TrackManager.h"

#include <cmath>
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

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
