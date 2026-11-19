// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// A reference longer than the mix is cut where the mix ends (owner, 16 Nov 2026), so what is measured is what is
// heard. A reference that is loud only in a tail past the mix's end must measure as the quiet stretch it is
// compared over, and the waveforms must share one time scale.

#include "Core/AuditionCompare.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra;

namespace {
int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("[FAIL] %s\n", m); ++g_failures; } }
constexpr uint32_t kRate = 48000;

std::vector<float> tone(double seconds, float amp, double hz = 220.0) {
    const size_t frames = static_cast<size_t>(seconds * kRate);
    std::vector<float> out(frames * 2);
    for (size_t i = 0; i < frames; ++i)
        out[i * 2] = out[i * 2 + 1] = amp * static_cast<float>(std::sin(6.283185307 * hz * static_cast<double>(i) / kRate));
    return out;
}
} // namespace

int main() {
    // Mix: 4 s at -20 dB. Reference: the same 4 s at -20 dB, then 6 s at -2 dB the mix never reaches.
    auto mixSamples = tone(4.0, 0.1f);
    auto refSamples = tone(4.0, 0.1f);
    const auto loudTail = tone(6.0, 0.8f);
    refSamples.insert(refSamples.end(), loudTail.begin(), loudTail.end());

    const auto mix = AuditionCompare::measure("mix", mixSamples, 2, kRate);
    const auto ref = AuditionCompare::measure("ref", refSamples, 2, kRate);
    check(ref.seconds > 9.9, "the reference is 10 s long to start with");

    const auto result = AuditionCompare::compare(mix, ref);
    check(result.ok, "the two compare");
    check(std::fabs(result.ref.seconds - mix.seconds) < 0.01, "the reference is cut to the mix's length");
    check(result.ref.buffer && result.ref.buffer->numFrames == static_cast<uint64_t>(4.0 * kRate), "its audio is the first 4 s");
    check(std::fabs(result.referenceGainDb) < 0.5, "it matches the mix's level, as the stretch that is heard is the same level (got a gain of off)");

    // Left whole, the loud tail would have dragged the reference's loudness up and its match down.
    AuditionCompare::Side uncut = ref;
    const double wholeGain = -Audio::levelMatchGainDb(mix.analysis.integratedLufs, uncut.analysis.integratedLufs);
    check(wholeGain < -3.0, "(control) measured whole, the loud tail would have turned the reference down hard");

    // A reference no longer than the mix is untouched.
    const auto shortRef = AuditionCompare::measure("short", tone(2.0, 0.1f), 2, kRate);
    const auto same = AuditionCompare::compare(mix, shortRef);
    check(std::fabs(same.ref.seconds - 2.0) < 0.01, "a shorter reference is left as it is");

    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("AuditionCompareTrimTest: all checks passed\n");
    return 0;
}
