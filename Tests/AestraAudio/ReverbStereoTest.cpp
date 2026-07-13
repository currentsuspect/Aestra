// AestraVerb F6 stereo regression test.
//
// Guards the mono-compatibility fix: the FDN output vectors were re-tuned so the
// reverb tail no longer loses 4.4-5.1 dB when folded to mono. This locks in the
// invariant that the tail stays mono-compatible AND wide, so a future change to
// the output geometry (kOutputL/R), the decorrelation boost, or the width matrix
// can't silently regress it.
//
// Fast by design: a short decorrelated-noise burst through the worst-case modes,
// measuring mono fold-down retention and stereo width on the post-source tail.

#include "AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& msg) {
    if (!cond) { std::cerr << "[FAIL] " << msg << "\n"; ++g_failures; }
}

struct StereoStats {
    float monoRetentionDb = 0.0f; // 20log10(monoRMS / L RMS) on the tail; 0 = no mono loss
    float widthProxy = 0.0f;      // side/mid RMS on the tail
    float lateCorr = 0.0f;        // inter-channel correlation on the tail
};

StereoStats measure(const std::string& /*name*/, AestraVerb::Mode mode, float sr) {
    const size_t frames = static_cast<size_t>(sr * 1.5f);
    const size_t burst = static_cast<size_t>(sr * 0.3f);

    // Decorrelated L/R noise burst, then silence for the tail.
    std::vector<float> inL(frames, 0.0f), inR(frames, 0.0f), outL(frames), outR(frames);
    uint32_t s1 = 22222, s2 = 99991;
    for (size_t i = 0; i < burst; ++i) {
        s1 = s1 * 1103515245u + 12345u;
        s2 = s2 * 1103515245u + 12345u;
        inL[i] = (static_cast<float>((s1 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
        inR[i] = (static_cast<float>((s2 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
    }

    AestraVerb verb;
    verb.initialize(sr, 512);
    verb.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
    verb.setParameter(AestraVerb::kDecay, 0.7f);
    verb.setParameter(AestraVerb::kSize, 0.6f);
    verb.setParameter(AestraVerb::kDiffusion, 0.8f);
    verb.setParameter(AestraVerb::kWidth, 0.7f);
    verb.setParameter(AestraVerb::kMix, 1.0f);
    verb.activate();
    const float* in[2] = { inL.data(), inR.data() };
    float* out[2] = { outL.data(), outR.data() };
    verb.process(in, out, 2, 2, static_cast<uint32_t>(frames));

    const size_t tailStart = std::min(frames - 1, burst + static_cast<size_t>(sr * 0.2f));
    double midSq = 0, sideSq = 0, monoSq = 0, lSq = 0, lr = 0, ll = 0, rr = 0, sl = 0, srr = 0;
    for (size_t i = tailStart; i < frames; ++i) {
        const float mid = (outL[i] + outR[i]) * 0.5f;
        const float side = (outL[i] - outR[i]) * 0.5f;
        midSq += static_cast<double>(mid) * mid;
        sideSq += static_cast<double>(side) * side;
        monoSq += static_cast<double>(outL[i] + outR[i]) * (outL[i] + outR[i]) * 0.25f;
        lSq += static_cast<double>(outL[i]) * outL[i];
        lr += static_cast<double>(outL[i]) * outR[i];
        ll += static_cast<double>(outL[i]) * outL[i];
        rr += static_cast<double>(outR[i]) * outR[i];
        sl += outL[i];
        srr += outR[i];
    }
    const size_t n = frames - tailStart;
    StereoStats s;
    const double midR = std::sqrt(midSq / n), sideR = std::sqrt(sideSq / n);
    const double monoR = std::sqrt(monoSq / n), lR = std::sqrt(lSq / n);
    s.widthProxy = static_cast<float>(midR > 1e-20 ? sideR / midR : 0.0);
    s.monoRetentionDb = static_cast<float>(20.0 * std::log10(std::max(monoR, 1e-12) / std::max(lR, 1e-12)));
    const double ml = sl / n, mr = srr / n;
    const double cov = lr / n - ml * mr;
    const double vl = ll / n - ml * ml, vr = rr / n - mr * mr;
    s.lateCorr = static_cast<float>(cov / std::max(std::sqrt(vl * vr), 1e-20));
    return s;
}

} // namespace

int main() {
    const float sr = 48000.0f;
    std::cout << "AestraVerb F6 Stereo Regression Test\n";
    std::cout << "====================================\n";

    // The modes that were worst on mono fold-down before the fix (all the
    // non-Hall modes clustered around -4.4 to -5.3 dB in this window). After the
    // fix they sit near -3.4 to -4.15 dB. Guard floor -4.5 dB sits between: the
    // fixed engine passes with margin; the pre-fix geometry (worst ~-5.3 dB in
    // this window) fails. Mono retention is an RMS ratio, so it is stable across
    // platforms (no FFT accumulation-order sensitivity).
    struct ModeCase { const char* name; AestraVerb::Mode mode; };
    const ModeCase cases[] = {
        {"room", AestraVerb::Mode::Room},
        {"plate", AestraVerb::Mode::Plate},
        {"chamber", AestraVerb::Mode::Chamber},
        {"ambience", AestraVerb::Mode::Ambience},
        {"cathedral", AestraVerb::Mode::Cathedral},
    };

    for (const auto& c : cases) {
        const StereoStats s = measure(c.name, c.mode, sr);
        std::cout << "  " << c.name << ": monoRet=" << s.monoRetentionDb
                  << "dB width=" << s.widthProxy << " lateCorr=" << s.lateCorr << "\n";
        // Mono compatibility: fold-down must not lose more than 4.5 dB.
        check(s.monoRetentionDb > -4.5f,
              std::string(c.name) + ": mono fold-down retention > -4.5 dB (got " +
                  std::to_string(s.monoRetentionDb) + ")");
        // Still a stereo reverb: the tail must retain real width.
        check(s.widthProxy > 0.9f,
              std::string(c.name) + ": tail width preserved > 0.9 (got " +
                  std::to_string(s.widthProxy) + ")");
        // Not pathologically anti-correlated.
        check(s.lateCorr > -0.35f,
              std::string(c.name) + ": late correlation > -0.35 (got " +
                  std::to_string(s.lateCorr) + ")");
    }

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " stereo check(s) FAILED.\n";
        return 1;
    }
    std::cout << "\nAll F6 stereo checks passed.\n";
    return 0;
}
