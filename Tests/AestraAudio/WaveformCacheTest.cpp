// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WaveformCache.h"

#include <cstdint>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;

namespace {

bool fail(const std::string& message) {
    std::cerr << "FAIL: " << message << '\n';
    return false;
}

bool approxEqual(float a, float b, float epsilon = 1.0e-5f) {
    return std::fabs(a - b) <= epsilon;
}

[[maybe_unused]] std::vector<float> makeStereoFixture() {
    return {
        0.10f, -0.20f,
        0.30f,  0.50f,
       -0.40f,  0.70f,
        0.80f, -0.10f,
       -0.60f,  0.20f,
        0.05f, -0.90f,
        0.90f,  0.40f,
       -0.20f,  0.60f
    };
}

// 1. Peak generation correctness (mono synthetic buffer)
bool testMonoPeakCorrectness() {
    // 4 frames mono: [0.0, -0.8, 0.6, 0.2]
    // samplesPerPeak = 2 -> 2 peaks
    // Peak 0: min=-0.8, max=0.0, rms=sqrt((0 + 0.64)/2)=sqrt(0.32)=0.565685...
    // Peak 1: min=0.0 (0.2 is >0), wait min=0.2? No, 0.6 and 0.2 are both positive.
    // Peak 1: min=0.2, max=0.6, rms=sqrt((0.36 + 0.04)/2)=sqrt(0.20)=0.4472136
    std::vector<float> data = {0.0f, -0.8f, 0.6f, 0.2f};
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 4, 1, 2, 1);

    if (!cache.isReady()) return fail("mono cache should be ready");
    const WaveformMipLevel* level = cache.getLevel(0);
    if (!level) return fail("level 0 missing");
    if (level->numPeaks != 2) return fail("expected 2 peaks");

    WaveformPeak p0 = level->getPeak(0, 0);
    if (!approxEqual(p0.min, -0.8f)) return fail("mono p0 min");
    if (!approxEqual(p0.max, 0.0f)) return fail("mono p0 max");
    if (!approxEqual(p0.rms, 0.565685f, 1.0e-4f)) return fail("mono p0 rms");
    if (p0.count != 2) return fail("mono p0 count");

    WaveformPeak p1 = level->getPeak(0, 1);
    if (!approxEqual(p1.min, 0.2f)) return fail("mono p1 min");
    if (!approxEqual(p1.max, 0.6f)) return fail("mono p1 max");
    if (!approxEqual(p1.rms, 0.4472136f, 1.0e-4f)) return fail("mono p1 rms");
    if (p1.count != 2) return fail("mono p1 count");

    return true;
}

// 2. Stereo peak correctness
bool testStereoPeakCorrectness() {
    std::vector<float> data = {
        0.50f, -0.90f,   // frame 0
        -0.30f,  0.10f,  // frame 1
        0.80f, -0.20f,   // frame 2
        0.00f,  0.60f    // frame 3
    };
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 4, 2, 2, 1); // 2 samples/peak, 1 level

    const WaveformMipLevel* level = cache.getLevel(0);
    if (!level) return fail("stereo level 0 missing");

    // Channel 0: [0.5, -0.3] -> min=-0.3, max=0.5, rms=sqrt((0.25+0.09)/2)=sqrt(0.17)
    WaveformPeak ch0p0 = level->getPeak(0, 0);
    if (!approxEqual(ch0p0.min, -0.3f)) return fail("stereo ch0p0 min");
    if (!approxEqual(ch0p0.max, 0.5f)) return fail("stereo ch0p0 max");
    if (!approxEqual(ch0p0.rms, std::sqrt(0.17f), 1.0e-4f)) return fail("stereo ch0p0 rms");

    // Channel 1: [-0.9, 0.1] -> min=-0.9, max=0.1, rms=sqrt((0.81+0.01)/2)=sqrt(0.41)
    WaveformPeak ch1p0 = level->getPeak(1, 0);
    if (!approxEqual(ch1p0.min, -0.9f)) return fail("stereo ch1p0 min");
    if (!approxEqual(ch1p0.max, 0.1f)) return fail("stereo ch1p0 max");
    if (!approxEqual(ch1p0.rms, std::sqrt(0.41f), 1.0e-4f)) return fail("stereo ch1p0 rms");

    // Ensure channels are not mixed
    if (ch0p0.min == ch1p0.min && ch0p0.max == ch1p0.max) {
        // They happen to differ, but if they were identical that would be suspicious.
        // We already checked exact values above.
    }

    return true;
}

// 3. LOD aggregation correctness
bool testLodAggregation() {
    std::vector<float> data = {
        0.0f, -0.8f,   // peak 0
        0.6f,  0.2f    // peak 1
    };
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 2, 1, 1, 2); // 1 sample/peak, 2 levels

    const WaveformMipLevel* level0 = cache.getLevel(0);
    const WaveformMipLevel* level1 = cache.getLevel(1);
    if (!level0 || !level1) return fail("missing LOD levels");

    if (level0->samplesPerPeak != 1) return fail("lod0 samplesPerPeak");
    if (level1->samplesPerPeak != 4) return fail("lod1 samplesPerPeak");
    if (level1->numPeaks != 1) return fail("lod1 numPeaks");

    WaveformPeak lod0p0 = level0->getPeak(0, 0);
    WaveformPeak lod0p1 = level0->getPeak(0, 1);
    WaveformPeak lod1p0 = level1->getPeak(0, 0);

    // min/max
    if (!approxEqual(lod1p0.min, std::min(lod0p0.min, lod0p1.min))) return fail("lod1 min");
    if (!approxEqual(lod1p0.max, std::max(lod0p0.max, lod0p1.max))) return fail("lod1 max");

    // count
    if (lod1p0.count != lod0p0.count + lod0p1.count) return fail("lod1 count");

    // weighted RMS
    double sumSq = static_cast<double>(lod0p0.rms) * lod0p0.rms * lod0p0.count
                 + static_cast<double>(lod0p1.rms) * lod0p1.rms * lod0p1.count;
    float expectedRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(lod1p0.count)));
    if (!approxEqual(lod1p0.rms, expectedRms, 1.0e-4f)) return fail("lod1 rms aggregation");

    return true;
}

// 4. LOD selection
bool testLodSelection() {
    // Use a large enough buffer so that levels have distinct peak counts
    std::vector<float> data(65536 * 2, 0.0f); // 2 channels, 65536 frames
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<float>((i % 32) - 16) / 16.0f;

    WaveformCache cache;
    cache.buildFromRaw(data.data(), 65536, 2, 64, 4); // 64, 256, 1024, 4096

    if (cache.selectLevel(1.0) != 0) return fail("spp=1 -> level 0");
    if (cache.selectLevel(63.0) != 0) return fail("spp=63 -> level 0");
    if (cache.selectLevel(64.0) != 0) return fail("spp=64 -> level 0");
    if (cache.selectLevel(200.0) != 0) return fail("spp=200 -> level 0 (coarsest <= 200 is 64)");
    if (cache.selectLevel(300.0) != 1) return fail("spp=300 -> level 1");
    if (cache.selectLevel(5000.0) != 3) return fail("spp=5000 -> level 3");
    if (cache.selectLevel(100000.0) != 3) return fail("spp=100000 -> level 3 (coarsest)");

    return true;
}

// 5. Visible range clipping
bool testVisibleRangeClipping() {
    std::vector<float> data(256, 0.0f);
    for (size_t i = 0; i < 256; ++i) data[i] = static_cast<float>(i % 16) / 16.0f - 0.5f;
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 256, 1, 16, 2);

    std::vector<WaveformPeak> peaks;
    cache.getPeaksForRange(0, 0, 256, 16, peaks);
    if (peaks.size() != 16) return fail("clipping: expected 16 peaks");

    // Request a subrange
    cache.getPeaksForRange(0, 64, 128, 8, peaks);
    if (peaks.size() != 8) return fail("clipping: expected 8 peaks for subrange");

    // Invalid channel returns zeros
    cache.getPeaksForRange(99, 0, 256, 4, peaks);
    for (const auto& p : peaks) {
        if (p.min != 0.0f || p.max != 0.0f) return fail("invalid channel should return zeros");
    }

    // startSample >= endSample returns zeros
    cache.getPeaksForRange(0, 100, 50, 4, peaks);
    for (const auto& p : peaks) {
        if (p.min != 0.0f || p.max != 0.0f) return fail("reverse range should return zeros");
    }

    return true;
}

// 6. Fractional source-frame bins stay source anchored
bool testPreciseRangeMapping() {
    const std::vector<float> data = {0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f};
    WaveformCache cache;
    cache.buildFromRaw(data.data(), static_cast<SampleIndex>(data.size()), 1, 1, 1);

    std::vector<WaveformPeak> peaks;
    cache.getPeaksForRangePrecise(0, 1.25, 5.25, 4, peaks);
    if (peaks.size() != 4) return fail("precise mapping: expected four peaks");
    if (!approxEqual(peaks[0].min, 0.20f) || !approxEqual(peaks[0].max, 0.30f))
        return fail("precise mapping: first pixel reset at fractional start");
    if (!approxEqual(peaks[1].min, 0.30f) || !approxEqual(peaks[1].max, 0.40f))
        return fail("precise mapping: second pixel boundary");
    if (!approxEqual(peaks[3].min, 0.50f) || !approxEqual(peaks[3].max, 0.60f))
        return fail("precise mapping: final pixel boundary");

    return true;
}

// 7. Cache sharing (same source identity -> shared ClipSource cache)
bool testCacheSharing() {
    ClipSource source(ClipSourceID{1}, "Test");
    std::vector<float> data = {0.1f, -0.2f, 0.3f, -0.4f};
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->interleavedData = std::move(data);
    buffer->numChannels = 1;
    buffer->numFrames = 4;
    buffer->sampleRate = 48000;
    source.setBuffer(buffer);

    WaveformCacheBuilder builder;
    auto cache = builder.buildSync(source);
    if (!cache || !cache->isReady()) return fail("sync build should succeed");

    source.setWaveformCache(cache);
    if (source.getWaveformCache() != cache) return fail("cache should be shared on source");

    return true;
}

bool testSourceRevisionTracksBufferContent() {
    ClipSource source(ClipSourceID{1}, "Test");
    auto first = std::make_shared<AudioBufferData>();
    first->interleavedData = {0.1f, -0.2f};
    first->numChannels = 1;
    first->numFrames = 2;
    first->sampleRate = 48000;

    const uint64_t initialRevision = source.getContentRevision();
    source.setBuffer(first);
    if (source.getContentRevision() != initialRevision + 1U) return fail("setBuffer should increment source revision");

    WaveformCacheBuilder builder;
    auto cache = builder.buildSync(source);
    if (!cache || !cache->isReady()) return fail("sync build should produce cache before buffer replacement");
    source.setWaveformCache(cache);
    if (source.getWaveformCache() != cache) return fail("cache should be stored before buffer replacement");

    auto second = std::make_shared<AudioBufferData>();
    second->interleavedData = {0.3f, -0.4f};
    second->numChannels = 1;
    second->numFrames = 2;
    second->sampleRate = 48000;

    source.setBuffer(second);
    if (source.getContentRevision() != initialRevision + 2U) return fail("second setBuffer should increment revision");
    if (source.getWaveformCache()) return fail("buffer replacement should clear stale waveform cache");

    return true;
}

// 7. Failure safety
bool testFailureSafety() {
    WaveformCache cache;
    // Invalid parameters
    cache.buildFromRaw(nullptr, 100, 2, 64, 2);
    if (cache.isReady()) return fail("null data should not become ready");

    cache.buildFromRaw(reinterpret_cast<const float*>(0x1), 0, 2, 64, 2);
    if (cache.isReady()) return fail("zero frames should not become ready");

    std::vector<float> data = {0.1f, 0.2f};
    cache.buildFromRaw(data.data(), 1, 0, 64, 2);
    if (cache.isReady()) return fail("zero channels should not become ready");

    // Invalid buffer in builder
    ClipSource badSource;
    WaveformCacheBuilder builder;
    auto built = builder.buildSync(badSource);
    if (built != nullptr) return fail("invalid source should return nullptr from sync build");

    return true;
}

// 8. NaN/Inf sanitization
bool testNanInfSanitization() {
    std::vector<float> data = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        0.5f
    };
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 4, 1, 2, 1);

    const WaveformMipLevel* level = cache.getLevel(0);
    if (!level) return fail("nan/inf level missing");

    WaveformPeak p0 = level->getPeak(0, 0);
    if (std::isnan(p0.min) || std::isinf(p0.min)) return fail("nan/inf min not sanitized");
    if (std::isnan(p0.max) || std::isinf(p0.max)) return fail("nan/inf max not sanitized");

    return true;
}

// 9. Very short file
bool testVeryShortFile() {
    std::vector<float> data = {0.3f};
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 1, 1, 64, 2); // 64 samples/peak but only 1 frame

    if (!cache.isReady()) return fail("short file should be ready");
    if (cache.getLevel(0)->numPeaks != 1) return fail("short file should have 1 peak");
    if (cache.getLevel(0)->getPeak(0, 0).count != 1) return fail("short file count should be 1");

    return true;
}

// 10. Silent audio
bool testSilentAudio() {
    std::vector<float> data(128, 0.0f);
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 128, 1, 16, 2);

    const WaveformMipLevel* level = cache.getLevel(0);
    for (SampleIndex i = 0; i < level->numPeaks; ++i) {
        WaveformPeak p = level->getPeak(0, i);
        if (p.min != 0.0f || p.max != 0.0f || p.rms != 0.0f) return fail("silent peak should be zero");
    }

    return true;
}

// 11. DC offset / asymmetric waveform
bool testAsymmetricWaveform() {
    // All positive with a DC offset of 0.5
    std::vector<float> data(32, 0.5f);
    data[4] = 0.8f;
    data[5] = 0.2f;
    WaveformCache cache;
    cache.buildFromRaw(data.data(), 32, 1, 8, 1); // 8 samples/peak -> 4 peaks

    const WaveformMipLevel* level = cache.getLevel(0);
    if (!level) return fail("asymmetric level missing");

    WaveformPeak p0 = level->getPeak(0, 0);
    if (!approxEqual(p0.min, 0.2f)) return fail("asymmetric min");
    if (!approxEqual(p0.max, 0.8f)) return fail("asymmetric max");
    if (p0.min == p0.max) return fail("asymmetric min/max should differ");

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok &= testMonoPeakCorrectness();
    ok &= testStereoPeakCorrectness();
    ok &= testLodAggregation();
    ok &= testLodSelection();
    ok &= testVisibleRangeClipping();
    ok &= testPreciseRangeMapping();
    ok &= testCacheSharing();
    ok &= testSourceRevisionTracksBufferContent();
    ok &= testFailureSafety();
    ok &= testNanInfSanitization();
    ok &= testVeryShortFile();
    ok &= testSilentAudio();
    ok &= testAsymmetricWaveform();

    if (ok) {
        std::cout << "AestraWaveformCacheTest: PASS\n";
        return 0;
    }

    return 1;
}
