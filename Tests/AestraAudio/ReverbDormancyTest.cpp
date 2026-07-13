// AestraVerb F8 tail-dormancy regression test.
//
// Verifies the idle-instance optimization is correct and transparent:
//   1. Silence transparency  — silent input yields silent output (dormant path).
//   2. No early truncation    — the audible tail after a burst is not cut short.
//   3. Wake transparency      — after going dormant then receiving new input, the
//                               response matches a fresh instance (the FDN state
//                               decayed to ~0 during sleep, exactly like fresh).
//   4. CPU benefit            — processing a dormant (silent) span is much cheaper
//                               than processing an active (noisy) span.

#include "AestraVerb.h"

#include <algorithm>
#include <chrono>
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

void configure(AestraVerb& v, float sr) {
    v.initialize(sr, 256);
    v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(AestraVerb::Mode::Hall));
    v.setParameter(AestraVerb::kDecay, 0.7f);
    v.setParameter(AestraVerb::kSize, 0.6f);
    v.setParameter(AestraVerb::kDiffusion, 0.8f);
    v.setParameter(AestraVerb::kModRate, 0.5f);
    v.setParameter(AestraVerb::kModDepth, 0.3f);
    v.setParameter(AestraVerb::kMix, 1.0f);
    v.setParameter(AestraVerb::kWidth, 0.7f);
    v.activate();
}

// Process a buffer in 256-frame blocks.
void run(AestraVerb& v, const std::vector<float>& inL, const std::vector<float>& inR,
         std::vector<float>& outL, std::vector<float>& outR) {
    const size_t n = inL.size();
    outL.assign(n, 0.0f);
    outR.assign(n, 0.0f);
    const size_t block = 256;
    for (size_t off = 0; off < n; off += block) {
        const size_t f = std::min(block, n - off);
        const float* in[2] = { inL.data() + off, inR.data() + off };
        float* out[2] = { outL.data() + off, outR.data() + off };
        v.process(in, out, 2, 2, static_cast<uint32_t>(f));
    }
}

double peak(const std::vector<float>& x, size_t a, size_t b) {
    double p = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) p = std::max(p, std::abs((double)x[i]));
    return p;
}

double rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0.0; size_t n = 0;
    for (size_t i = a; i < b && i < x.size(); ++i) { s += (double)x[i] * x[i]; ++n; }
    return n ? std::sqrt(s / n) : 0.0;
}

std::vector<float> noiseBurst(size_t n, size_t burstLen, uint32_t seed) {
    std::vector<float> v(n, 0.0f);
    for (size_t i = 0; i < burstLen && i < n; ++i) {
        seed = seed * 1103515245u + 12345u;
        v[i] = (static_cast<float>((seed >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
    }
    return v;
}

} // namespace

int main() {
    const float sr = 48000.0f;
    std::cout << "AestraVerb F8 Tail-Dormancy Test\n";
    std::cout << "================================\n";

    // --- 1. Silence transparency: silent input -> silent output ---
    {
        AestraVerb v; configure(v, sr);
        const size_t n = static_cast<size_t>(sr * 2.0f);
        std::vector<float> zero(n, 0.0f), oL, oR;
        run(v, zero, zero, oL, oR);
        const double p = std::max(peak(oL, 0, n), peak(oR, 0, n));
        check(p < 1e-9, "silent input produces silent output (peak " + std::to_string(p) + ")");
        std::cout << "  silence transparency: output peak " << p << "\n";
    }

    // --- 2. No early truncation: audible tail survives well past the burst ---
    {
        AestraVerb v; configure(v, sr);
        const size_t n = static_cast<size_t>(sr * 5.0f);
        const size_t burst = static_cast<size_t>(sr * 0.1f);
        auto inL = noiseBurst(n, burst, 12345);
        auto inR = noiseBurst(n, burst, 99991);
        std::vector<float> oL, oR;
        run(v, inL, inR, oL, oR);
        // Tail at 1 s and 2 s must be far above the dormant floor (-140 dBFS).
        const double t1 = rms(oL, size_t(sr * 1.0f), size_t(sr * 1.1f));
        const double t2 = rms(oL, size_t(sr * 2.0f), size_t(sr * 2.1f));
        check(t1 > 1e-4, "tail present at 1 s (rms " + std::to_string(t1) + ")");
        check(t2 > 1e-5, "tail present at 2 s (rms " + std::to_string(t2) + ")");
        std::cout << "  tail rms @1s=" << t1 << " @2s=" << t2 << "\n";
    }

    // --- 3. Wake transparency: dormant-then-woken behaves like a fresh instance ---
    // The FDN decays to ~0 during a long sleep, so a woken instance responds to
    // new input like a fresh one. Two caveats shape the assertions:
    //  * The sub-Hz modulation LFOs advance while the woken instance is still
    //    actively decaying (before it sleeps), so at wake its modulation phase
    //    differs from a fresh instance. That decorrelates the *modulated* late
    //    tail's micro-structure but not its energy. So we check the energy
    //    envelope (phase-insensitive) over the whole response, and sample-level
    //    correlation only in the early, unmodulated reflection window.
    {
        const size_t pre = static_cast<size_t>(sr * 0.1f);
        const size_t gap = static_cast<size_t>(sr * 12.0f);
        const size_t tail = static_cast<size_t>(sr * 2.0f);
        const size_t wake = static_cast<size_t>(sr * 0.1f);

        AestraVerb vw; configure(vw, sr);
        const size_t nW = pre + gap + wake + tail;
        std::vector<float> wInL(nW, 0.0f), wInR(nW, 0.0f);
        auto primeL = noiseBurst(pre, pre, 54321);
        auto wakeL = noiseBurst(wake, wake, 24680);
        auto wakeR = noiseBurst(wake, wake, 13579);
        for (size_t i = 0; i < pre; ++i) { wInL[i] = primeL[i]; wInR[i] = primeL[i]; }
        const size_t wakeStart = pre + gap;
        for (size_t i = 0; i < wake; ++i) { wInL[wakeStart + i] = wakeL[i]; wInR[wakeStart + i] = wakeR[i]; }
        std::vector<float> wOutL, wOutR;
        run(vw, wInL, wInR, wOutL, wOutR);

        AestraVerb vf; configure(vf, sr);
        const size_t nF = wake + tail;
        std::vector<float> fInL(nF, 0.0f), fInR(nF, 0.0f);
        for (size_t i = 0; i < wake; ++i) { fInL[i] = wakeL[i]; fInR[i] = wakeR[i]; }
        std::vector<float> fOutL, fOutR;
        run(vf, fInL, fInR, fOutL, fOutR);

        // Early, unmodulated window (first 30 ms): should match closely.
        const size_t earlyN = static_cast<size_t>(sr * 0.03f);
        double num = 0.0, dw = 0.0, df = 0.0;
        for (size_t i = 0; i < earlyN; ++i) {
            const float a = wOutL[wakeStart + i], b = fOutL[i];
            num += (double)a * b; dw += (double)a * a; df += (double)b * b;
        }
        const double earlyCorr = (dw > 1e-20 && df > 1e-20) ? num / std::sqrt(dw * df) : 0.0;
        check(earlyCorr > 0.98, "woken early reflections match fresh (corr " + std::to_string(earlyCorr) + ")");

        // Energy envelope match over 0-500 ms (phase-insensitive), within 1.5 dB.
        bool envOk = true;
        double worstDb = 0.0;
        for (int w = 0; w < 10; ++w) {
            const size_t a = static_cast<size_t>(sr * 0.05f) * w;
            const size_t b = a + static_cast<size_t>(sr * 0.05f);
            const double rw = rms(wOutL, wakeStart + a, wakeStart + b);
            const double rf = rms(fOutL, a, b);
            if (rf > 1e-6) {
                const double db = std::abs(20.0 * std::log10(std::max(rw, 1e-9) / rf));
                worstDb = std::max(worstDb, db);
                if (db > 1.5) envOk = false;
            }
        }
        check(envOk, "woken energy envelope matches fresh within 1.5 dB (worst " + std::to_string(worstDb) + " dB)");
        std::cout << "  wake transparency: earlyCorr=" << earlyCorr << " worstEnvDiff=" << worstDb << " dB\n";
    }

    // --- 3b. Dormant automation: controls stay current while asleep ---
    // Prime wet, sleep, then automate Mix fully dry during dormancy. On wake the
    // dry path must take effect immediately; if the smoothed Mix were left stale
    // at its wet value, the first wake block would ramp up from ~0 dry gain and
    // the early output would not track the input.
    {
        AestraVerb v; configure(v, sr); // Mix = 1.0 (fully wet)
        const size_t pre = static_cast<size_t>(sr * 0.1f);
        const size_t gap = static_cast<size_t>(sr * 13.0f);
        const size_t wake = static_cast<size_t>(sr * 0.05f);

        auto prime = noiseBurst(pre, pre, 111);
        std::vector<float> pO(pre);
        const float* pin[2] = { prime.data(), prime.data() };
        float* pout[2] = { pO.data(), pO.data() };
        v.process(pin, pout, 2, 2, static_cast<uint32_t>(pre));

        std::vector<float> s(gap, 0.0f), sO(gap);
        const size_t block = 256;
        for (size_t off = 0; off < gap; off += block) {
            const size_t f = std::min(block, gap - off);
            const float* in[2] = { s.data() + off, s.data() + off };
            float* out[2] = { sO.data() + off, sO.data() + off };
            v.process(in, out, 2, 2, static_cast<uint32_t>(f));
        }

        // Automate Mix to fully dry while dormant, then let one more silent
        // (dormant) block run so the snap picks up the new value — this models
        // continuous host automation writing between process blocks while idle.
        v.setParameter(AestraVerb::kMix, 0.0f);
        {
            std::vector<float> s2(block, 0.0f), s2O(block);
            const float* in[2] = { s2.data(), s2.data() };
            float* out[2] = { s2O.data(), s2O.data() };
            v.process(in, out, 2, 2, static_cast<uint32_t>(block));
        }

        auto wk = noiseBurst(wake, wake, 222);
        std::vector<float> wOutL(wake), wOutR(wake);
        const float* win[2] = { wk.data(), wk.data() };
        float* wout[2] = { wOutL.data(), wOutR.data() };
        v.process(win, wout, 2, 2, static_cast<uint32_t>(wake));

        // With Mix snapped to dry, the wake output tracks the dry input closely
        // from the first samples (dry gain ~1, wet gain ~0). Measure early-window
        // correlation of output vs input.
        const size_t earlyN = static_cast<size_t>(sr * 0.01f);
        double num = 0.0, do_ = 0.0, di = 0.0;
        for (size_t i = 0; i < earlyN; ++i) {
            num += (double)wOutL[i] * wk[i]; do_ += (double)wOutL[i] * wOutL[i]; di += (double)wk[i] * wk[i];
        }
        const double dryCorr = (do_ > 1e-20 && di > 1e-20) ? num / std::sqrt(do_ * di) : 0.0;
        check(dryCorr > 0.99, "wake after dormant Mix->dry tracks dry input (corr " + std::to_string(dryCorr) + ")");
        std::cout << "  dormant automation: dry-track corr=" << dryCorr << "\n";
    }

    // --- 4. CPU benefit: a dormant span is much cheaper than an active span ---
    {
        const size_t n = static_cast<size_t>(sr * 4.0f);
        const size_t block = 256;

        // Active: continuous noise.
        AestraVerb va; configure(va, sr);
        auto nz = noiseBurst(n, n, 4242);
        std::vector<float> aOutL(n), aOutR(n);
        auto ta0 = std::chrono::high_resolution_clock::now();
        for (size_t off = 0; off < n; off += block) {
            const size_t f = std::min(block, n - off);
            const float* in[2] = { nz.data() + off, nz.data() + off };
            float* out[2] = { aOutL.data() + off, aOutR.data() + off };
            va.process(in, out, 2, 2, static_cast<uint32_t>(f));
        }
        auto ta1 = std::chrono::high_resolution_clock::now();
        const double activeUs = std::chrono::duration<double, std::micro>(ta1 - ta0).count();

        // Dormant: prime briefly, then time a long silent span (instance asleep).
        AestraVerb vd; configure(vd, sr);
        auto prime = noiseBurst(static_cast<size_t>(sr * 0.1f), static_cast<size_t>(sr * 0.1f), 7);
        std::vector<float> pO(prime.size());
        // Prime + let it fall asleep over ~15 s of silence (not timed).
        {
            const size_t settle = static_cast<size_t>(sr * 15.0f);
            std::vector<float> s(settle, 0.0f), sO(settle);
            const float* pin[2] = { prime.data(), prime.data() };
            float* pout[2] = { pO.data(), pO.data() };
            vd.process(pin, pout, 2, 2, static_cast<uint32_t>(prime.size()));
            for (size_t off = 0; off < settle; off += block) {
                const size_t f = std::min(block, settle - off);
                const float* in[2] = { s.data() + off, s.data() + off };
                float* out[2] = { sO.data() + off, sO.data() + off };
                vd.process(in, out, 2, 2, static_cast<uint32_t>(f));
            }
        }
        std::vector<float> z(n, 0.0f), dOutL(n), dOutR(n);
        auto td0 = std::chrono::high_resolution_clock::now();
        for (size_t off = 0; off < n; off += block) {
            const size_t f = std::min(block, n - off);
            const float* in[2] = { z.data() + off, z.data() + off };
            float* out[2] = { dOutL.data() + off, dOutR.data() + off };
            vd.process(in, out, 2, 2, static_cast<uint32_t>(f));
        }
        auto td1 = std::chrono::high_resolution_clock::now();
        const double dormantUs = std::chrono::duration<double, std::micro>(td1 - td0).count();

        std::cout << "  cpu: active=" << activeUs << "us dormant=" << dormantUs
                  << "us (" << (activeUs / std::max(dormantUs, 1e-9)) << "x cheaper)\n";
        // Generous bound: the real ratio is tens of x; require at least 2x so the
        // check is robust on a loaded CI runner but still fails if dormancy breaks.
        check(dormantUs < 0.5 * activeUs, "dormant span is materially cheaper than active");
    }

    if (g_failures != 0) {
        std::cerr << "\n" << g_failures << " dormancy check(s) FAILED.\n";
        return 1;
    }
    std::cout << "\nAll F8 dormancy checks passed.\n";
    return 0;
}
