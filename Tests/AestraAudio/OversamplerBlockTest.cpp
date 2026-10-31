// © 2026 Aestra Studios — All Rights Reserved.
//
// OversamplerBlockTest — the block oversampler must be the SAME oversampler.
//
// DSP::BlockOversampler4x runs the per-sample DSP::Oversampler's kernels over a
// whole block, so the compiler can vectorise across samples. The claim is that
// only the order of the sums changed. This test holds it to that:
//
//   * the oversampled signal matches the per-sample upsampler,
//   * a nonlinear round trip (up -> tanh -> down) matches the per-sample chain,
//     at every block size, including ones that are not a multiple of anything,
//   * the latency is unchanged (the impulse peak lands on the same sample),
//   * state carries across calls: one big block == the same signal in pieces,
//   * reset() returns it to a clean start.

#include "DSP/OversamplerBlock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using Aestra::Audio::DSP::BlockOversampler4x;
using Aestra::Audio::DSP::Oversampler;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

std::vector<float> makeSignal(size_t n) {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> noise(-0.2f, 0.2f);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(0.5 * std::sin(0.07 * static_cast<double>(i)) +
                                  0.2 * std::sin(1.9 * static_cast<double>(i))) +
               noise(rng);
    }
    return x;
}

float shaper(float v) {
    return std::tanh(3.0f * v);
}

// Per-sample reference: Oversampler 4x with the same memoryless shaper.
std::vector<float> referenceChain(const std::vector<float>& in, bool shaped) {
    Oversampler os;
    os.prepare(4);
    std::vector<float> out(in.size());
    float buf[4];
    for (size_t i = 0; i < in.size(); ++i) {
        os.upsample(in[i], buf);
        if (shaped) {
            for (float& v : buf) {
                v = shaper(v);
            }
        }
        out[i] = os.downsample(buf);
    }
    return out;
}

std::vector<float> blockChain(const std::vector<float>& in, uint32_t blockSize, bool shaped) {
    BlockOversampler4x os;
    os.prepare();
    std::vector<float> out(in.size());
    std::vector<float> scratch(BlockOversampler4x::kFactor * BlockOversampler4x::kMaxFrames);
    for (size_t pos = 0; pos < in.size();) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(blockSize, in.size() - pos));
        os.upsample(&in[pos], scratch.data(), n);
        if (shaped) {
            for (uint32_t i = 0; i < 4u * n; ++i) {
                scratch[i] = shaper(scratch[i]);
            }
        }
        os.downsample(scratch.data(), &out[pos], n);
        pos += n;
    }
    return out;
}

double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i])));
    }
    return worst;
}

// Float32 reassociation only: a 4x chain of three FIRs sits well under 1e-5.
constexpr double kTolerance = 1.0e-5;

void testOversampledSignalMatches() {
    const std::vector<float> in = makeSignal(4096);

    Oversampler ref;
    ref.prepare(4);
    std::vector<float> expected(in.size() * 4);
    for (size_t i = 0; i < in.size(); ++i) {
        ref.upsample(in[i], &expected[i * 4]);
    }

    BlockOversampler4x blk;
    blk.prepare();
    std::vector<float> got(in.size() * 4);
    for (size_t pos = 0; pos < in.size(); pos += 100) {
        const uint32_t n = static_cast<uint32_t>(std::min<size_t>(100, in.size() - pos));
        blk.upsample(&in[pos], &got[pos * 4], n);
    }
    const double diff = maxAbsDiff(expected, got);
    std::printf("  upsampled signal max|diff| = %.3e\n", diff);
    check(diff < kTolerance, "UpsampledSignalMatchesPerSample");
}

void testRoundTripMatchesAtEveryBlockSize() {
    const std::vector<float> in = makeSignal(8192);
    for (const bool shaped : {false, true}) {
        const std::vector<float> expected = referenceChain(in, shaped);
        double worst = 0.0;
        for (const uint32_t bs : {1u, 2u, 7u, 16u, 37u, 64u, 100u, 127u, 128u}) {
            worst = std::max(worst, maxAbsDiff(expected, blockChain(in, bs, shaped)));
        }
        std::printf("  %s round trip, worst over block sizes: %.3e\n", shaped ? "tanh " : "plain", worst);
        check(worst < kTolerance,
              shaped ? "ShapedRoundTripMatchesAtEveryBlockSize" : "PlainRoundTripMatchesAtEveryBlockSize");
    }
}

void testLatencyUnchanged() {
    std::vector<float> impulse(512, 0.0f);
    impulse[100] = 1.0f;
    auto peakAt = [](const std::vector<float>& v) {
        size_t best = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (std::fabs(v[i]) > std::fabs(v[best])) {
                best = i;
            }
        }
        return best;
    };
    const size_t refPeak = peakAt(referenceChain(impulse, false));
    const size_t blkPeak = peakAt(blockChain(impulse, 64, false));
    std::printf("  impulse peak: per-sample %zu, block %zu (reported latency %u)\n", refPeak, blkPeak,
                BlockOversampler4x::kReportedLatency);
    check(refPeak == blkPeak, "ImpulsePeakPositionUnchanged");
    check(blkPeak - 100 == BlockOversampler4x::kReportedLatency, "ImpulsePeakMatchesReportedLatency");
}

void testResetClearsState() {
    const std::vector<float> in = makeSignal(1024);
    BlockOversampler4x os;
    os.prepare();
    std::vector<float> scratch(BlockOversampler4x::kFactor * BlockOversampler4x::kMaxFrames);
    std::vector<float> first(128), second(128);
    os.upsample(in.data(), scratch.data(), 128);
    os.downsample(scratch.data(), first.data(), 128);
    // Dirty the state with more audio, then reset and replay the first block.
    os.upsample(in.data() + 128, scratch.data(), 128);
    os.reset();
    os.upsample(in.data(), scratch.data(), 128);
    os.downsample(scratch.data(), second.data(), 128);
    check(maxAbsDiff(first, second) == 0.0, "ResetRestoresCleanStart");
}

} // namespace

int main() {
    testOversampledSignalMatches();
    testRoundTripMatchesAtEveryBlockSize();
    testLatencyUnchanged();
    testResetClearsState();

    if (g_failures != 0) {
        std::printf("%d OversamplerBlock test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All OversamplerBlock tests passed\n");
    return 0;
}
