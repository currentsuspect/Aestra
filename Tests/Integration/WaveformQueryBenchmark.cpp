// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Waveform query benchmark: measures the cache hot path the clip renderer hits per
// visible clip per frame. Mirrors Tests/Integration/ResamplerBenchmark.cpp.

#include "IO/WaveformCache.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr SampleIndex kNumFrames = 1440000; // 30 s @ 48 kHz
constexpr uint32_t kNumChannels = 2;

std::vector<float> makeSource() {
    std::vector<float> data(static_cast<size_t>(kNumFrames) * kNumChannels);
    for (SampleIndex f = 0; f < kNumFrames; ++f) {
        const double t = static_cast<double>(f) / 48000.0;
        data[static_cast<size_t>(f) * kNumChannels + 0] =
            static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979 * 220.0 * t));
        data[static_cast<size_t>(f) * kNumChannels + 1] =
            static_cast<float>(0.6 * std::sin(2.0 * 3.14159265358979 * 337.0 * t + 0.7));
    }
    return data;
}

double nowMs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

// What the pre-hotpath renderer effectively paid: public-API composition with
// by-value getPeakRange merging, plus two single-channel lock passes.
double legacyComposite(const WaveformCache& cache, double startSample, double endSample, uint32_t numPixels,
                       std::vector<WaveformPeak>& outL, std::vector<WaveformPeak>& outR) {
    const double t0 = nowMs();
    outL.resize(numPixels);
    outR.resize(numPixels);
    for (int rep = 0; rep < 2; ++rep) {
        const uint32_t ch = static_cast<uint32_t>(rep);
        const auto* level = cache.getLevel(cache.selectLevel((endSample - startSample) / numPixels));
        if (!level) break;
        for (uint32_t p = 0; p < numPixels; ++p) {
            const double ps = startSample + static_cast<double>(p) * (endSample - startSample) / numPixels;
            const double pe = startSample + static_cast<double>(p + 1) * (endSample - startSample) / numPixels;
            SampleIndex s = static_cast<SampleIndex>(std::floor(ps / level->samplesPerPeak));
            SampleIndex e = static_cast<SampleIndex>(std::ceil(pe / level->samplesPerPeak));
            e = std::max(e, s + 1);
            (rep == 0 ? outL : outR)[p] = level->getPeakRange(ch, s, e);
        }
    }
    return nowMs() - t0;
}

} // namespace

int main() {
    std::printf("building %.1fs stereo source...\n", kNumFrames / 48000.0);
    const auto data = makeSource();

    WaveformCache cache;
    auto buildT0 = nowMs();
    cache.buildFromRaw(data.data(), kNumFrames, kNumChannels);
    std::printf("cache build: %.1f ms (%zu levels, %zu KB)\n\n", nowMs() - buildT0, cache.getNumLevels(),
                cache.getMemoryUsage() / 1024);

    struct Case {
        const char* name;
        double start;
        double end;
        uint32_t width;
    };
    const Case cases[] = {
        {"clip ~400px, mid zoom", 0.0, 480000.0, 400},
        {"clip 1920px, coarse zoom", 0.0, 1440000.0, 1920},
        {"clip 1920px, deep zoom (direct path)", 1000.0, 1000.0 + 1920 * 64.0, 1920},
    };

    constexpr int kReps = 300;
    bool ok = true;
    for (const auto& c : cases) {
        std::vector<WaveformPeak> l, r, lRef, rRef;

        // Correctness gate: stereo API must equal legacy composition.
        legacyComposite(cache, c.start, c.end, c.width, lRef, rRef);
        cache.getPeaksForRangePreciseStereo(0, 1, c.start, c.end, c.width, l, r);
        for (uint32_t p = 0; p < c.width; ++p) {
            if (l[p].min != lRef[p].min || l[p].max != lRef[p].max || l[p].rms != lRef[p].rms ||
                r[p].min != rRef[p].min || r[p].max != rRef[p].max || r[p].rms != rRef[p].rms) {
                std::printf("FAIL %s: column %u diverges\n", c.name, p);
                ok = false;
                break;
            }
        }

        // Timing
        double legacyMs = 0.0, monoPairMs = 0.0, stereoMs = 0.0;
        for (int i = 0; i < kReps; ++i) {
            legacyMs += legacyComposite(cache, c.start, c.end, c.width, lRef, rRef);
            const double a0 = nowMs();
            cache.getPeaksForRangePrecise(0, c.start, c.end, c.width, l);
            cache.getPeaksForRangePrecise(1, c.start, c.end, c.width, r);
            monoPairMs += nowMs() - a0;
            const double b0 = nowMs();
            cache.getPeaksForRangePreciseStereo(0, 1, c.start, c.end, c.width, l, r);
            stereoMs += nowMs() - b0;
        }
        std::printf("%-38s legacy %8.3f ms | mono-pair %8.3f ms | stereo %8.3f ms  (%d reps)\n", c.name,
                    legacyMs / kReps, monoPairMs / kReps, stereoMs / kReps, kReps);
    }

    std::printf("\n%s\n", ok ? "WaveformQueryBenchmark: PASS" : "WaveformQueryBenchmark: FAIL");
    return ok ? 0 : 1;
}
