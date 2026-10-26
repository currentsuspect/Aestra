// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// ReverbDecayBalanceTest — the tail decays evenly across the spectrum and doesn't ring.
//
// Owner report (26 Oct): the reverb "sounds metallic and is not smooth". Measured on every mode's impulse response:
//   - The decay peaked hard around 500 Hz. The in-loop damping and low shelf removed a fixed amount per pass, so
//     the lows died in 0.33-0.49x the mid decay time and 2 kHz in 0.31-0.60x: each long tail collapsed into a narrow
//     mid ring. Pinned here: the 125 Hz octave decays over >= 0.8x the 500 Hz octave, 2 kHz over >= 0.7x.
//   - 0.67-1.42% of the late tail's spectrum (200 Hz-8 kHz) stood > 10 dB over its 1/6-octave neighbourhood:
//     isolated resonances ringing on their own (a Dattorro plate, for reference, measures ~0.6%). Pinned: <= 1.0%.
// Both fail on the pre-fix engine.

#include "Plugin/AestraVerb.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace Aestra::Audio::Plugins;

namespace {

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
int g_failed = 0;

void check(bool ok, const char* mode, const char* what, double value) {
    std::printf("  %s: %-11s %s (%.3f)\n", ok ? "PASS" : "FAIL", mode, what, value);
    if (!ok) ++g_failed;
}

std::vector<float> impulseResponse(int mode, double seconds) {
    AestraVerb v;
    v.initialize(kSr, 256);
    v.activate();
    v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
    v.setParameter(AestraVerb::kMix, 1.0f);
    v.setParameter(AestraVerb::kPredelayMs, 0.0f);
    const uint32_t block = 256;
    std::vector<float> inL(block), inR(block), outL(block), outR(block), ir;
    for (int k = 0; k < 200; ++k) { // let parameter smoothing settle on silence
        const float* ip[2] = {inL.data(), inR.data()};
        float* op[2] = {outL.data(), outR.data()};
        v.process(ip, op, 2, 2, block);
    }
    const size_t total = static_cast<size_t>(seconds * kSr);
    for (size_t done = 0; done < total; done += block) {
        std::fill(inL.begin(), inL.end(), 0.0f);
        std::fill(inR.begin(), inR.end(), 0.0f);
        if (done == 0) inL[0] = inR[0] = 1.0f;
        const float* ip[2] = {inL.data(), inR.data()};
        float* op[2] = {outL.data(), outR.data()};
        v.process(ip, op, 2, 2, block);
        ir.insert(ir.end(), outL.begin(), outL.end());
    }
    return ir;
}

// Octave band-pass (RBJ constant 0 dB peak, Q = sqrt(2)), run three times for a steep enough band.
std::vector<double> octave(const std::vector<float>& x, double fc) {
    const double w = 2.0 * kPi * fc / kSr, alpha = std::sin(w) / (2.0 * std::sqrt(2.0)), a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos(w) / a0, a2 = (1.0 - alpha) / a0;
    std::vector<double> y(x.begin(), x.end());
    for (int pass = 0; pass < 3; ++pass) {
        double z1 = 0.0, z2 = 0.0;
        for (double& v : y) {
            const double out = b0 * v + z1;
            z1 = -a1 * out + z2;
            z2 = b2 * v - a2 * out;
            v = out;
        }
    }
    return y;
}

// T30 from the Schroeder curve: least-squares slope between -5 and -35 dB, extended to 60 dB.
double t30(const std::vector<double>& x) {
    std::vector<double> e(x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;) e[i] = (acc += x[i] * x[i]);
    const double e0 = e[0] > 0 ? e[0] : 1.0;
    size_t a = 0, b = 0;
    for (size_t i = 0; i < e.size(); ++i) {
        const double db = 10.0 * std::log10(e[i] / e0 + 1e-30);
        if (!a && db < -5.0) a = i;
        if (db < -35.0) { b = i; break; }
    }
    if (b <= a + 10) return 0.0;
    double st = 0, sd = 0, stt = 0, std_ = 0;
    const double n = static_cast<double>(b - a);
    for (size_t i = a; i < b; ++i) {
        const double t = i / kSr, d = 10.0 * std::log10(e[i] / e0 + 1e-30);
        st += t; sd += d; stt += t * t; std_ += t * d;
    }
    const double slope = (n * std_ - st * sd) / (n * stt - st * st);
    return slope < 0 ? -60.0 / slope : 0.0;
}

void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl(std::cos(-2.0 * kPi / len), std::sin(-2.0 * kPi / len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j, w *= wl) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
            }
        }
    }
}

// Share of 200 Hz-8 kHz bins in the late tail (250 ms until -50 dB) standing > 10 dB over 1/6-octave smoothing.
double ringingShare(const std::vector<float>& x) {
    std::vector<double> e(x.size());
    double acc = 0.0;
    for (size_t i = x.size(); i-- > 0;) e[i] = (acc += double(x[i]) * x[i]);
    size_t end = x.size();
    for (size_t i = 0; i < e.size(); ++i)
        if (10.0 * std::log10(e[i] / e[0] + 1e-30) < -50.0) { end = i; break; }
    const size_t start = static_cast<size_t>(0.25 * kSr);
    if (end <= start + 4096) return 1.0;
    size_t n = 1;
    while (n < end - start) n <<= 1;
    std::vector<std::complex<double>> buf(n);
    const size_t len = end - start;
    for (size_t i = 0; i < len; ++i) buf[i] = x[start + i] * (0.5 - 0.5 * std::cos(2.0 * kPi * i / (len - 1)));
    fft(buf);
    std::vector<double> f, p;
    for (size_t k = 1; k < n / 2; ++k) {
        const double hz = k * kSr / n;
        if (hz > 200.0 && hz < 8000.0) { f.push_back(std::log2(hz)); p.push_back(std::norm(buf[k])); }
    }
    std::vector<double> cs(p.size() + 1, 0.0);
    for (size_t i = 0; i < p.size(); ++i) cs[i + 1] = cs[i] + p[i];
    size_t hot = 0;
    for (size_t i = 0; i < p.size(); ++i) {
        const size_t lo = std::lower_bound(f.begin(), f.end(), f[i] - 1.0 / 12.0) - f.begin();
        const size_t hi = std::lower_bound(f.begin(), f.end(), f[i] + 1.0 / 12.0) - f.begin();
        const double mean = (cs[hi] - cs[lo]) / std::max<size_t>(1, hi - lo);
        if (p[i] > mean * 10.0) ++hot; // 10 dB over the neighbourhood
    }
    return 100.0 * hot / p.size();
}

} // namespace

int main() {
    const char* names[] = {"Room", "Hall", "Plate", "Cathedral", "Chamber", "BrightHall", "Ambience", "Scoring", "SmoothPlate"};
    std::printf("AestraVerb decay balance and ringing (every mode, default settings, 100%% wet)\n");
    for (int m = 0; m < AestraVerb::kModeCount; ++m) {
        const auto ir = impulseResponse(m, 7.0);
        const double low = t30(octave(ir, 125.0)), mid = t30(octave(ir, 500.0)), high = t30(octave(ir, 2000.0));
        check(mid > 0 && low / mid >= 0.8, names[m], "125 Hz decays over >= 0.8x the 500 Hz decay", mid > 0 ? low / mid : 0.0);
        check(mid > 0 && high / mid >= 0.7, names[m], "2 kHz decays over >= 0.7x the 500 Hz decay", mid > 0 ? high / mid : 0.0);
        const double ring = ringingShare(ir);
        check(ring <= 1.0, names[m], "late-tail bins ringing > 10 dB over their neighbourhood, % <= 1.0", ring);
    }
    std::printf("%s\n", g_failed ? "FAILED" : "ALL PASSED");
    return g_failed ? 1 : 0;
}
