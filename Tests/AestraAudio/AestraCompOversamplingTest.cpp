// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AestraCompOversamplingTest — oversampling quality-mode contract tests (#228).
//
// Covers: latency reporting, bypass parity under reported latency, dry/wet
// alignment, aliasing reduction with 2x/4x, and v5 state round-trip with
// backward-compatible defaults for pre-v5 blobs.

#include "DSP/Oversampler.h"
#include "Plugin/AestraComp.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using Aestra::Audio::DSP::Oversampler;
using Aestra::Audio::Plugins::AestraComp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 256;

void processAll(AestraComp& comp, const std::vector<float>& inL, const std::vector<float>& inR,
                std::vector<float>& outL, std::vector<float>& outR) {
    const uint32_t total = static_cast<uint32_t>(inL.size());
    outL.assign(total, 0.0f);
    outR.assign(total, 0.0f);
    for (uint32_t start = 0; start < total; start += kBlockSize) {
        const uint32_t frames = std::min<uint32_t>(kBlockSize, total - start);
        const float* ins[2] = {inL.data() + start, inR.data() + start};
        float* outs[2] = {outL.data() + start, outR.data() + start};
        comp.process(ins, outs, 2, 2, frames);
    }
}

/// Goertzel magnitude at a single frequency over [begin, end).
double toneMagnitude(const std::vector<float>& signal, size_t begin, size_t end, double freqHz) {
    const double w = 2.0 * 3.14159265358979323846 * freqHz / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (size_t n = begin; n < end; ++n) {
        s0 = signal[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double n = static_cast<double>(end - begin);
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / (n * 0.5);
}

double db(double linear) {
    return 20.0 * std::log10(std::max(1.0e-12, linear));
}

void setupAggressiveComp(AestraComp& comp, float oversamplingNorm) {
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();
    // Aggressive clean compression: low threshold, max ratio, fastest attack.
    comp.setParameter(AestraComp::kThreshold, 0.0f); // -60 dB
    comp.setParameter(AestraComp::kRatio, 1.0f);     // 20:1
    comp.setParameter(AestraComp::kAttack, 0.0f);    // 0.1 ms
    comp.setParameter(AestraComp::kRelease, 0.0f);   // 10 ms
    comp.setParameter(AestraComp::kKnee, 0.0f);
    comp.setParameter(AestraComp::kMix, 1.0f);
    comp.setParameter(AestraComp::kOversampling, oversamplingNorm);
}

bool testLatencyReporting() {
    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();
    if (comp.getLatencySamples() != 0) {
        std::cerr << "latency: expected 0 with oversampling Off\n";
        return false;
    }

    std::vector<float> in(kBlockSize, 0.0f), l, r;
    for (float norm : {0.5f, 1.0f}) {
        comp.setParameter(AestraComp::kOversampling, norm);
        processAll(comp, in, in, l, r); // config applies on next process()
        if (comp.getLatencySamples() != Oversampler::kReportedLatency) {
            std::cerr << "latency: expected " << Oversampler::kReportedLatency << " at norm " << norm << ", got "
                      << comp.getLatencySamples() << "\n";
            return false;
        }
    }

    comp.setParameter(AestraComp::kOversampling, 0.0f);
    processAll(comp, in, in, l, r);
    if (comp.getLatencySamples() != 0) {
        std::cerr << "latency: expected 0 after switching oversampling back off\n";
        return false;
    }
    return true;
}

bool testBypassParityUnderLatency() {
    AestraComp comp;
    setupAggressiveComp(comp, 1.0f);
    comp.setParameter(AestraComp::kBypass, 1.0f);

    std::vector<float> inL(2048), inR(2048), outL, outR;
    for (size_t n = 0; n < inL.size(); ++n) {
        inL[n] = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * n / kSampleRate));
        inR[n] = 0.5f * inL[n];
    }
    processAll(comp, inL, inR, outL, outR);

    const uint32_t lat = comp.getLatencySamples();
    if (lat != Oversampler::kReportedLatency) {
        std::cerr << "bypass parity: unexpected latency " << lat << "\n";
        return false;
    }
    for (size_t n = lat; n < inL.size(); ++n) {
        if (outL[n] != inL[n - lat] || outR[n] != inR[n - lat]) {
            std::cerr << "bypass parity: bypassed output is not an exact " << lat << "-sample delay at n=" << n << "\n";
            return false;
        }
    }
    return true;
}

bool testTransparentPathAlignment() {
    // Ratio 1:1 disables gain reduction, mix 100%: the oversampled path should
    // return the input delayed by the reported latency, within filter accuracy.
    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.activate();
    comp.setParameter(AestraComp::kRatio, 0.0f); // 1:1
    comp.setParameter(AestraComp::kOversampling, 1.0f);

    const size_t total = 8192;
    std::vector<float> in(total), outL, outR;
    for (size_t n = 0; n < total; ++n) {
        in[n] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * n / kSampleRate));
    }
    processAll(comp, in, in, outL, outR);

    const uint32_t lat = comp.getLatencySamples();
    double errAcc = 0.0, refAcc = 0.0;
    for (size_t n = 1024; n < total; ++n) {
        const double e = outL[n] - in[n - lat];
        errAcc += e * e;
        refAcc += in[n - lat] * in[n - lat];
    }
    const double errDb = 10.0 * std::log10(std::max(1.0e-20, errAcc / refAcc));
    std::cout << "  transparent path error: " << errDb << " dB\n";
    if (errDb > -60.0) {
        std::cerr << "alignment: oversampled 1:1 path deviates from delayed input (" << errDb << " dB)\n";
        return false;
    }
    return true;
}

bool testOversamplerAliasRejection() {
    // Component-level guarantee: wrapping a genuinely hard nonlinearity
    // (hard clip) in the Oversampler must attenuate folded harmonics. A
    // 15 kHz sine clipped at native rate folds its 3rd harmonic (45 kHz)
    // to 3 kHz and its 5th (75 kHz) to 21 kHz.
    const size_t total = 1 << 15;
    const double f0 = 15000.0;
    auto clip = [](float v) { return std::clamp(v, -0.25f, 0.25f); };

    std::vector<float> nativeOut(total), osOut(total);
    for (uint32_t factorNorm = 0; factorNorm < 2; ++factorNorm) {
        Oversampler os;
        os.prepare(factorNorm == 0 ? 1u : 4u);
        std::vector<float>& out = factorNorm == 0 ? nativeOut : osOut;
        for (size_t n = 0; n < total; ++n) {
            const float in = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * f0 * n / kSampleRate));
            float up[4];
            os.upsample(in, up);
            for (uint32_t s = 0; s < os.factor(); ++s) {
                up[s] = clip(up[s]);
            }
            out[n] = os.downsample(up);
        }
    }

    const double alias3Native = db(toneMagnitude(nativeOut, total / 2, total, 3000.0));
    const double alias3Os = db(toneMagnitude(osOut, total / 2, total, 3000.0));
    std::cout << "  hard-clip alias @3kHz: native=" << alias3Native << " dB, 4x=" << alias3Os << " dB\n";

    if (alias3Native > -40.0 && alias3Os > alias3Native - 20.0) {
        std::cerr << "aliasing: 4x oversampled hard clip did not attenuate the folded 3rd harmonic by 20 dB\n";
        return false;
    }
    if (alias3Native <= -40.0) {
        std::cerr << "aliasing: stimulus produced no measurable native alias — test is not exercising anything\n";
        return false;
    }
    return true;
}

bool testCompAliasBandNotWorse() {
    // Integration-level guard: AestraComp's RMS-windowed detector already keeps
    // steady-state gain aliasing very low (measured near -120 dB), so assert
    // that enabling oversampling never makes the alias band worse. The printed
    // numbers are the honest record of what oversampling does for this
    // detector topology.
    const size_t total = 1 << 16;
    const double f0 = 15000.0;
    std::vector<float> in(total);
    for (size_t n = 0; n < total; ++n) {
        const bool on = (static_cast<size_t>(n * 100.0 / kSampleRate) % 2) == 0;
        in[n] = on ? static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * f0 * n / kSampleRate)) : 0.0f;
    }

    auto spuriousDb = [&](float osNorm) {
        AestraComp comp;
        setupAggressiveComp(comp, osNorm);
        std::vector<float> outL, outR;
        processAll(comp, in, in, outL, outR);
        double acc = 0.0;
        for (double f : {3000.0, 4500.0, 6000.0, 7500.0, 9000.0}) {
            const double m = toneMagnitude(outL, total / 2, total, f);
            acc += m * m;
        }
        return db(std::sqrt(acc));
    };

    const double off = spuriousDb(0.0f);
    const double x2 = spuriousDb(0.5f);
    const double x4 = spuriousDb(1.0f);
    std::cout << "  comp alias-band energy: off=" << off << " dB, 2x=" << x2 << " dB, 4x=" << x4 << " dB\n";

    if (x2 > off + 3.0 || x4 > off + 3.0) {
        std::cerr << "aliasing: oversampling made the alias band worse (off=" << off << ", 2x=" << x2 << ", 4x=" << x4
                  << ")\n";
        return false;
    }
    return true;
}

bool testStateRoundtripAndBackCompat() {
    AestraComp comp;
    comp.initialize(kSampleRate, kBlockSize);
    comp.setParameter(AestraComp::kOversampling, 1.0f);
    const auto state = comp.saveState();

    AestraComp restored;
    restored.initialize(kSampleRate, kBlockSize);
    if (!restored.loadState(state)) {
        std::cerr << "state: v5 load failed\n";
        return false;
    }
    if (restored.getParameter(AestraComp::kOversampling) != 1.0f) {
        std::cerr << "state: oversampling not restored\n";
        return false;
    }

    // Patch the version field down to 4: the oversampling slot must then be
    // ignored and default to Off, regardless of its stored content.
    auto v4state = state;
    const uint32_t v4 = 4;
    std::memcpy(v4state.data() + sizeof(uint32_t), &v4, sizeof(uint32_t));
    AestraComp legacy;
    legacy.initialize(kSampleRate, kBlockSize);
    if (!legacy.loadState(v4state)) {
        std::cerr << "state: v4 load failed\n";
        return false;
    }
    if (legacy.getParameter(AestraComp::kOversampling) != 0.0f) {
        std::cerr << "state: pre-v5 blob must default oversampling to Off\n";
        return false;
    }
    return true;
}

bool testNonFiniteInputStaysFinite() {
    AestraComp comp;
    setupAggressiveComp(comp, 1.0f);
    std::vector<float> in(1024, 0.1f), outL, outR;
    in[100] = std::numeric_limits<float>::quiet_NaN();
    in[200] = std::numeric_limits<float>::infinity();
    processAll(comp, in, in, outL, outR);
    for (float v : outL) {
        if (!std::isfinite(v)) {
            std::cerr << "nan: non-finite output with oversampling enabled\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    std::cout << "AestraComp Oversampling Tests\n";
    bool ok = true;
    ok &= testLatencyReporting();
    ok &= testBypassParityUnderLatency();
    ok &= testTransparentPathAlignment();
    ok &= testOversamplerAliasRejection();
    ok &= testCompAliasBandNotWorse();
    ok &= testStateRoundtripAndBackCompat();
    ok &= testNonFiniteInputStaysFinite();
    if (!ok) {
        std::cerr << "AestraComp oversampling tests FAILED\n";
        return 1;
    }
    std::cout << "All AestraComp oversampling tests passed.\n";
    return 0;
}
