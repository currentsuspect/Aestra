// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// RenderSummary is what AestraHeadless --check judges a render by, so each
// number it reports is checked here against a signal whose answer is known:
// a full-scale sine, a DC offset, a clipped sample, a NaN, a padded tone.

#include "Headless/RenderSummary.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::printf("[FAIL] %s\n", message);
        ++g_failures;
    }
}

bool near(double a, double b, double tol) {
    return std::fabs(a - b) <= tol;
}

constexpr uint32_t kRate = 48000;

// Stereo, both channels the same sine at the given peak.
std::vector<float> sine(double seconds, double peak) {
    const auto frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> out(frames * 2);
    for (size_t f = 0; f < frames; ++f) {
        const float x = static_cast<float>(peak * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * f / kRate));
        out[f * 2] = x;
        out[f * 2 + 1] = x;
    }
    return out;
}

void testSineLevels() {
    const RenderSummary s = summarizeRender(sine(1.0, 0.5), 2, kRate);
    check(s.frames == kRate && s.channels == 2 && near(s.durationSeconds, 1.0, 1e-9), "shape of a 1 s stereo render");
    check(near(s.peakDbfs, -6.0206, 0.01), "peak of a 0.5 sine is -6.02 dBFS");
    check(near(s.rmsDbfs, -9.0309, 0.01), "RMS of a 0.5 sine is -9.03 dBFS (peak - 3.01)");
    check(near(s.dcOffset[0], 0.0, 1e-4) && near(s.dcOffset[1], 0.0, 1e-4), "a whole number of cycles has no DC");
    check(s.clippedSamples == 0 && s.nonFiniteSamples == 0, "nothing clipped, nothing non-finite");
    check(!s.silent && s.leadingSilenceSeconds < 0.001, "a sine is not silent and starts at once");
}

void testDcOffset() {
    std::vector<float> v(2 * 4800, 0.25f);
    for (size_t i = 1; i < v.size(); i += 2)
        v[i] = -0.1f;
    const RenderSummary s = summarizeRender(v, 2, kRate);
    check(near(s.dcOffset[0], 0.25, 1e-6) && near(s.dcOffset[1], -0.1, 1e-6), "per-channel DC offset");
}

void testClippedAndNonFinite() {
    std::vector<float> v = sine(0.1, 0.5);
    v[10] = 1.0f;
    v[11] = -1.5f;
    v[20] = std::numeric_limits<float>::quiet_NaN();
    v[21] = std::numeric_limits<float>::infinity();
    const RenderSummary s = summarizeRender(v, 2, kRate);
    check(s.clippedSamples == 2, "samples at or past full scale are counted as clipped");
    check(s.nonFiniteSamples == 2, "NaN and Inf are counted");
    check(std::isfinite(s.peakDbfs) && std::isfinite(s.rmsDbfs), "non-finite samples do not poison the levels");
    check(near(s.peakDbfs, 20.0 * std::log10(1.5), 0.01), "peak is the largest finite magnitude");
}

void testSilencePadding() {
    // 0.5 s silence, 1 s tone, 0.25 s silence.
    std::vector<float> v(static_cast<size_t>(0.5 * kRate) * 2, 0.0f);
    const auto tone = sine(1.0, 0.5);
    v.insert(v.end(), tone.begin(), tone.end());
    v.insert(v.end(), static_cast<size_t>(0.25 * kRate) * 2, 0.0f);
    const RenderSummary s = summarizeRender(v, 2, kRate);
    check(near(s.leadingSilenceSeconds, 0.5, 0.001), "leading silence measured");
    check(near(s.trailingSilenceSeconds, 0.25, 0.001), "trailing silence measured");

    const RenderSummary quiet = summarizeRender(std::vector<float>(2 * 4800, 0.0f), 2, kRate);
    check(quiet.silent && quiet.peakDbfs <= -199.0, "all zeros is silent, at the floor");
}

void testHashAndExpectation() {
    const auto a = sine(0.2, 0.5);
    auto b = a;
    const RenderSummary sa = summarizeRender(a, 2, kRate);
    check(summarizeRender(b, 2, kRate).hash64 == sa.hash64, "identical samples hash identically");
    b[1234] = std::nextafter(b[1234], 1.0f); // one ULP
    const RenderSummary sb = summarizeRender(b, 2, kRate);
    check(sb.hash64 != sa.hash64, "one ULP anywhere changes the hash");

    check(compareToExpectation(sa, expectationFrom(sa)).empty(), "a render matches its own golden");
    const auto diffs = compareToExpectation(sb, expectationFrom(sa));
    check(diffs.size() == 1 && diffs[0].find("hash") == 0, "a 1-ULP change fails only on the hash");

    RenderExpectation louder = expectationFrom(sa);
    louder.peakDbfs += 0.5;
    const auto levelDiffs = compareToExpectation(sa, louder);
    check(levelDiffs.size() == 1 && levelDiffs[0].find("peak") == 0, "a level change reports how far it moved");

    RenderExpectation levelsOnly = expectationFrom(sa);
    levelsOnly.hasHash = false;
    check(compareToExpectation(sb, levelsOnly).empty(),
          "a cross-platform golden (no hash) ignores a 1-ULP difference");

    uint64_t back = 0;
    check(hashFromHex(hashToHex(sa.hash64), back) && back == sa.hash64, "the golden hash round-trips through hex");
    check(!hashFromHex("XYZ", back) && !hashFromHex("0123456789ABCDEF", back), "malformed hex is refused");
}

} // namespace

int main() {
    testSineLevels();
    testDcOffset();
    testClippedAndNonFinite();
    testSilencePadding();
    testHashAndExpectation();
    if (g_failures == 0) {
        std::printf("RenderSummaryTest: all checks passed\n");
        return 0;
    }
    std::printf("RenderSummaryTest: %d check(s) failed\n", g_failures);
    return 1;
}
