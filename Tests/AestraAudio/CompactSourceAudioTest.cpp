// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// CompactSourceAudioTest — a source that is exactly 16- or 24-bit PCM is kept as that PCM (half or
// three quarters of float32's memory) and must sound BIT-IDENTICAL:
//   1. tryCompact() is lossless or does nothing: exact values compact and read back equal; anything
//      else (arbitrary float, NaN, over full scale, more than two channels) stays float32 untouched;
//   2. the render kernel mixes a compact source exactly as it mixes the same samples as float32, for
//      every interpolator, ratio, varispeed, mono and stereo, block size, file edge and fade;
//   3. the other readers (extractRegion, waveform cache, peaks) agree with the float path.

#include "Core/ClipRenderKernel.h"
#include "IO/WaveformCache.h"
#include "Models/ClipRenderService.h"
#include "Models/ClipSource.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
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

// A float buffer whose samples are exactly k / 2^bits.
std::shared_ptr<AudioBufferData> pcmValued(uint32_t channels, uint64_t frames, int bits, uint32_t rate, unsigned seed) {
    auto b = std::make_shared<AudioBufferData>();
    b->numChannels = channels;
    b->numFrames = frames;
    b->sampleRate = rate;
    b->interleavedData.resize(frames * channels);
    std::mt19937 rng(seed);
    const double scale = bits == 16 ? 32768.0 : 8388608.0;
    for (uint64_t f = 0; f < frames; ++f) {
        for (uint32_t c = 0; c < channels; ++c) {
            const double tone = 0.55 * std::sin(0.031 * static_cast<double>(f) * (c + 1)) +
                                0.25 * std::sin(0.4 * static_cast<double>(f));
            const double noise = (static_cast<double>(rng() % 2001) - 1000.0) / 8000.0;
            const double v = std::max(-1.0, std::min(0.999, tone + noise));
            b->interleavedData[f * channels + c] = static_cast<float>(std::nearbyint(v * scale) / scale);
        }
    }
    return b;
}

bool sameBits(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0);
}

// Renders the whole clip block by block into a stereo double buffer.
std::vector<double> renderAll(const ClipRenderState& clip, uint64_t projectFrames, uint32_t block, uint32_t engineRate,
                              Interpolators::InterpolationQuality q) {
    std::vector<double> out(static_cast<size_t>(projectFrames) * 2, 0.0);
    std::vector<double> blockBuf(static_cast<size_t>(block) * 2);
    for (uint64_t start = 0; start < projectFrames; start += block) {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(block, projectFrames - start));
        std::fill(blockBuf.begin(), blockBuf.end(), 0.0);
        (void)ClipRenderKernel::renderClipInto(clip, start, start + n, blockBuf.data(), engineRate, q);
        std::copy(blockBuf.begin(), blockBuf.begin() + static_cast<size_t>(n) * 2, out.begin() + static_cast<size_t>(start) * 2);
    }
    return out;
}

ClipRenderState stateFor(const std::shared_ptr<const AudioBufferData>& buf, uint64_t startSample, uint64_t endSample,
                         double sampleOffset) {
    ClipRenderState s;
    s.bufferOwner = buf;
    s.audioData = buf->isCompact() ? nullptr : buf->interleavedData.data();
    s.totalFrames = buf->numFrames;
    s.sourceSampleRate = buf->sampleRate;
    s.channels = buf->numChannels;
    s.startSample = startSample;
    s.endSample = endSample;
    s.sampleOffset = sampleOffset;
    s.gain = 0.8f;
    s.pan = 0.25f;
    return s;
}
} // namespace

int main() {
    std::cout << "[1] tryCompact is lossless or nothing\n";
    for (int bits : {16, 24}) {
        auto b = pcmValued(2, 5000, bits, 48000, 1);
        const auto original = b->interleavedData;
        const bool compacted = b->tryCompact();
        check(compacted && b->storage == (bits == 16 ? AudioBufferData::Storage::Pcm16 : AudioBufferData::Storage::Pcm24),
              std::to_string(bits) + "-bit valued samples compact to " + std::to_string(bits / 8) + " bytes");
        check(b->interleavedData.empty() && b->residentBytes() == original.size() * (bits / 8), "and the float copy is gone");
        std::vector<float> back(original.size());
        b->copyFloats(0, back.size(), back.data());
        check(back == original, "every sample reads back exactly");
        check(b->toFloat() == original && b->sampleAt(1234) == original[1234], "toFloat and sampleAt agree");
        check(b->isValid() && b->storedSamples() == original.size(), "still a valid buffer of the same length");
        std::vector<float> window(7, 9.0f);
        b->copyFloats(original.size() - 3, 7, window.data());
        check(window[0] == original[original.size() - 3] && window[3] == 0.0f && window[6] == 0.0f,
              "a window past the end is zero-filled, not read");
        b->expandToFloat();
        check(!b->isCompact() && b->interleavedData == original, "expandToFloat restores float32 exactly");
    }
    {
        auto b = pcmValued(2, 1000, 16, 48000, 2);
        b->interleavedData[500] = 0.123456789f; // not on any PCM grid
        check(!b->tryCompact() && !b->isCompact(), "one off-grid sample keeps the whole buffer float32");
        auto c = pcmValued(2, 1000, 16, 48000, 3);
        c->interleavedData[10] = std::nanf("");
        check(!c->tryCompact() && !c->isCompact(), "a NaN keeps it float32");
        auto d = pcmValued(2, 1000, 16, 48000, 4);
        d->interleavedData[10] = 1.5f;
        check(!d->tryCompact() && !d->isCompact(), "a sample over full scale keeps it float32");
        auto e = pcmValued(2, 1000, 24, 48000, 5);
        e->interleavedData[7] = std::nextafter(e->interleavedData[7], 2.0f);
        check(!e->tryCompact(), "a 25-bit value keeps it float32");
        auto f = pcmValued(2, 1000, 16, 48000, 6);
        f->interleavedData[0] = -1.0f;
        check(f->tryCompact() && f->sampleAt(0) == -1.0f, "-1.0 (full-scale negative) is representable in 16-bit");
    }

    std::cout << "[2] the kernel mixes a compact source bit-identically\n";
    using Q = Interpolators::InterpolationQuality;
    struct Case { const char* name; uint32_t ch; int bits; uint32_t srcRate; float speed; float pitch; uint32_t block; double offset; };
    const Case cases[] = {
        {"stereo16 same rate", 2, 16, 48000, 1.0f, 0.0f, 512, 0.0},
        {"stereo16 same rate, slip", 2, 16, 48000, 1.0f, 0.0f, 333, 1500.0},
        {"stereo24 44.1k to 48k", 2, 24, 44100, 1.0f, 0.0f, 512, 0.0},
        {"stereo16 96k to 48k", 2, 16, 96000, 1.0f, 0.0f, 1024, 100.0},
        {"stereo16 varispeed 0.5", 2, 16, 48000, 0.5f, 0.0f, 256, 0.0},
        {"stereo16 varispeed 3.7 + pitch", 2, 16, 48000, 3.7f, 5.0f, 4096, 0.0},
        {"stereo24 pitch -7", 2, 24, 48000, 1.0f, -7.0f, 64, 0.0},
        {"mono16 same rate", 1, 16, 48000, 1.0f, 0.0f, 512, 0.0},
        {"mono16 44.1k to 48k", 1, 16, 44100, 1.0f, 0.0f, 700, 0.0},
        {"mono24 varispeed 1.3", 1, 24, 48000, 1.3f, 0.0f, 8192, 50.0},
        {"stereo16 one-frame blocks", 2, 16, 44100, 1.0f, 0.0f, 1, 0.0},
        {"stereo16 huge blocks", 2, 16, 44100, 1.0f, 0.0f, 20000, 0.0},
    };
    const Q qualities[] = {Q::Cubic, Q::Sinc8, Q::Sinc16, Q::Sinc32, Q::Sinc64};
    const char* qualityNames[] = {"Cubic", "Sinc8", "Sinc16", "Sinc32", "Sinc64"};
    int checked = 0, bad = 0;
    for (const auto& c : cases) {
        auto floatBuf = pcmValued(c.ch, 6000, c.bits, c.srcRate, 11);
        auto compactBuf = std::make_shared<AudioBufferData>(*floatBuf);
        if (!compactBuf->tryCompact()) {
            check(false, std::string(c.name) + ": could not compact the fixture");
            continue;
        }
        const uint64_t clipStart = 777;                                 // not block aligned
        const uint64_t clipEnd = clipStart + 14000;                      // runs past the source for ratio < 1
        for (size_t qi = 0; qi < 5; ++qi) {
            auto reference = stateFor(floatBuf, clipStart, clipEnd, c.offset);
            auto compact = stateFor(compactBuf, clipStart, clipEnd, c.offset);
            reference.playbackRate = compact.playbackRate = c.speed;
            reference.pitchSemitones = compact.pitchSemitones = c.pitch;
            reference.fadeInSamples = compact.fadeInSamples = 300;
            reference.fadeOutSamples = compact.fadeOutSamples = 900;
            const auto want = renderAll(reference, clipEnd + 600, c.block, 48000, qualities[qi]);
            const auto got = renderAll(compact, clipEnd + 600, c.block, 48000, qualities[qi]);
            ++checked;
            if (!sameBits(want, got)) {
                ++bad;
                check(false, std::string(c.name) + " / " + qualityNames[qi] + ": output differs from the float32 render");
            }
        }
    }
    check(bad == 0, std::to_string(checked) + " renders (interpolators x ratios x block sizes x mono/stereo x edges) are bit-identical");

    std::cout << "[3] the other readers agree\n";
    {
        auto floatBuf = pcmValued(2, 30000, 16, 48000, 21);
        auto compactBuf = std::make_shared<AudioBufferData>(*floatBuf);
        compactBuf->tryCompact();
        auto a = ClipRenderService::extractRegion(*floatBuf, 1234, 9000);
        auto b = ClipRenderService::extractRegion(*compactBuf, 1234, 9000);
        check(a && b && a->interleavedData == b->interleavedData && !b->isCompact(),
              "extractRegion returns the same float32 region from either storage");
        check(floatBuf->peakAbs() == compactBuf->peakAbs(), "peakAbs agrees");

        WaveformCache wf, wc;
        wf.buildFromBuffer(*floatBuf, 64, 4);
        wc.buildFromBuffer(*compactBuf, 64, 4);
        bool same = true;
        for (size_t level = 0; level < 4; ++level) {
            const auto* x = wf.getLevel(level);
            const auto* y = wc.getLevel(level);
            same = same && x && y && x->numPeaks == y->numPeaks;
            for (uint64_t p = 0; same && p < x->numPeaks; ++p)
                for (uint32_t ch = 0; ch < 2 && same; ++ch) {
                    const auto px = x->getPeak(ch, p), py = y->getPeak(ch, p);
                    same = px.min == py.min && px.max == py.max && px.rms == py.rms;
                }
        }
        check(same, "the waveform cache has identical peaks at every level");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
