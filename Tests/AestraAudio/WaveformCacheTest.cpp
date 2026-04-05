// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WaveformCache.h"

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

std::vector<float> makeStereoFixture() {
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

bool testBuildAndLevels() {
    WaveformCache cache;
    const std::vector<float> data = makeStereoFixture();

    cache.buildFromRaw(data.data(), 8, 2, 2, 3);

    if (!cache.isReady()) {
        return fail("cache should be ready after build");
    }
    if (cache.getNumLevels() != 3) {
        return fail("expected 3 mip levels");
    }
    if (cache.getNumChannels() != 2) {
        return fail("expected 2 channels");
    }
    if (cache.getSourceFrames() != 8) {
        return fail("expected 8 source frames");
    }

    const WaveformMipLevel* level0 = cache.getLevel(0);
    const WaveformMipLevel* level1 = cache.getLevel(1);
    const WaveformMipLevel* level2 = cache.getLevel(2);
    if (!level0 || !level1 || !level2) {
        return fail("expected all mip levels to exist");
    }

    if (level0->samplesPerPeak != 2 || level0->numPeaks != 4) {
        return fail("unexpected level 0 layout");
    }
    if (level1->samplesPerPeak != 8 || level1->numPeaks != 1) {
        return fail("unexpected level 1 layout");
    }
    if (level2->samplesPerPeak != 32 || level2->numPeaks != 1) {
        return fail("unexpected level 2 layout");
    }

    const WaveformPeak ch0Peak0 = level0->getPeak(0, 0);
    if (!approxEqual(ch0Peak0.min, 0.10f) || !approxEqual(ch0Peak0.max, 0.30f)) {
        return fail("unexpected channel 0 peak[0]");
    }

    const WaveformPeak ch1Peak2 = level0->getPeak(1, 2);
    if (!approxEqual(ch1Peak2.min, -0.90f) || !approxEqual(ch1Peak2.max, 0.20f)) {
        return fail("unexpected channel 1 peak[2]");
    }

    const WaveformPeak merged = level1->getPeak(1, 0);
    if (!approxEqual(merged.min, -0.90f) || !approxEqual(merged.max, 0.70f)) {
        return fail("unexpected merged channel 1 peak");
    }

    return true;
}

bool testLevelSelectionAndQueries() {
    WaveformCache cache;
    const std::vector<float> data = makeStereoFixture();
    cache.buildFromRaw(data.data(), 8, 2, 2, 3);

    if (cache.selectLevel(1.0) != 0) {
        return fail("samplesPerPixel=1 should select finest level");
    }
    if (cache.selectLevel(4.0) != 0) {
        return fail("samplesPerPixel=4 should still select level 0");
    }
    if (cache.selectLevel(10.0) != 1) {
        return fail("samplesPerPixel=10 should select level 1");
    }

    std::vector<WaveformPeak> peaks;
    cache.getPeaksForRange(0, 0, 8, 4, peaks);
    if (peaks.size() != 4) {
        return fail("expected 4 output peaks");
    }

    if (!approxEqual(peaks[0].min, 0.10f) || !approxEqual(peaks[0].max, 0.30f)) {
        return fail("unexpected rendered peak 0");
    }
    if (!approxEqual(peaks[1].min, -0.40f) || !approxEqual(peaks[1].max, 0.80f)) {
        return fail("unexpected rendered peak 1");
    }

    const WaveformPeak quick = cache.getQuickPeak(1, 0, 8);
    if (!approxEqual(quick.min, -0.90f) || !approxEqual(quick.max, 0.70f)) {
        return fail("unexpected quick-peak result");
    }

    return true;
}

bool testClearAndInvalidQueries() {
    WaveformCache cache;
    const std::vector<float> data = makeStereoFixture();
    cache.buildFromRaw(data.data(), 8, 2, 2, 2);
    cache.clear();

    if (cache.isReady()) {
        return fail("cache should not be ready after clear");
    }
    if (cache.getNumLevels() != 0) {
        return fail("levels should be empty after clear");
    }

    std::vector<WaveformPeak> peaks;
    cache.getPeaksForRange(3, 0, 8, 4, peaks);
    if (peaks.size() != 4) {
        return fail("output vector should still resize for invalid queries");
    }
    for (const WaveformPeak& peak : peaks) {
        if (!approxEqual(peak.min, 0.0f) || !approxEqual(peak.max, 0.0f)) {
            return fail("invalid query should return zero peaks");
        }
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok &= testBuildAndLevels();
    ok &= testLevelSelectionAndQueries();
    ok &= testClearAndInvalidQueries();

    if (ok) {
        std::cout << "AestraWaveformCacheTest: PASS\n";
        return 0;
    }

    return 1;
}
