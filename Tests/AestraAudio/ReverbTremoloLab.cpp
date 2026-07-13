// AestraVerb Tremolo / Amplitude-Modulation Measurement Lab.
//
// Quantifies the audible "tremolo" (tail amplitude pumping) directly from the
// OUTPUT: fires a 50 ms broadband burst, then measures the ripple of the decaying
// tail's dB envelope around a quadratic decay fit — its depth (RMS dB), dominant
// rate, and coherent-peak prominence — per mode. Includes a modulation-OFF
// control and a modulation-depth sweep to test whether the LFO is the cause, a
// low/mid/high band split, a damping comparison, and A/B WAV renders (mod off vs
// max) for ear confirmation. This broadband-envelope metric does not by itself
// measure narrow spectral peaks/notches moving across sustained source tones.
//
// Output: labs/reverb/tremolo/tremolo_report.md / .json

#include "AestraVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Aestra::Audio::Plugins::AestraVerb;

namespace {
constexpr float kPi = 3.14159265358979323846f;

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    if (n <= 1) return;
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * kPi / static_cast<float>(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
size_t floorPow2(size_t n) { size_t p = 1; while ((p << 1) <= n) p <<= 1; return p; }

bool writeWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, float sr) {
    if (l.size() != r.size() || l.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t n = static_cast<uint32_t>(l.size());
    const uint32_t dataSize = n * 8, fileSize = 36 + dataSize, byteRate = static_cast<uint32_t>(sr) * 8;
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(fileSize); f.write("WAVE", 4); f.write("fmt ", 4);
    u32(16); u16(3); u16(2); u32(static_cast<uint32_t>(sr)); u32(byteRate); u16(8); u16(32);
    f.write("data", 4); u32(dataSize);
    for (uint32_t i = 0; i < n; ++i) { f.write(reinterpret_cast<const char*>(&l[i]), 4); f.write(reinterpret_cast<const char*>(&r[i]), 4); }
    return f.good();
}

// One-pole band split into low (<500), mid (500-4k), high (>4k).
struct Bands { std::vector<float> lo, mid, hi; };
Bands split3(const std::vector<float>& x, float sr) {
    Bands b; b.lo.resize(x.size()); b.mid.resize(x.size()); b.hi.resize(x.size());
    const float aLo = 1.0f - std::exp(-2.0f * kPi * 500.0f / sr);
    const float aHi = 1.0f - std::exp(-2.0f * kPi * 4000.0f / sr);
    float lp500 = 0.0f, lp4k = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        lp500 += aLo * (x[i] - lp500);
        lp4k  += aHi * (x[i] - lp4k);
        b.lo[i]  = lp500;          // < 500
        b.mid[i] = lp4k - lp500;   // 500..4k
        b.hi[i]  = x[i] - lp4k;    // > 4k
    }
    return b;
}

// Amplitude envelope via block-RMS (block ~5.3 ms @48k -> ~187 Hz envelope rate).
std::vector<float> envelope(const std::vector<float>& x, size_t block, float& envRate, float sr) {
    std::vector<float> e;
    for (size_t off = 0; off + block <= x.size(); off += block) {
        double s = 0; for (size_t i = 0; i < block; ++i) s += (double)x[off + i] * x[off + i];
        e.push_back(static_cast<float>(std::sqrt(s / block)));
    }
    envRate = sr / static_cast<float>(block);
    return e;
}

struct TremStat { float rateHz = 0.0f; float rippleDb = 0.0f; float peakProminence = 0.0f; };

// Measure the tremolo as ripple on the DECAYING tail: a clean reverb tail decays
// smoothly (a straight line in dB), so any pumping shows up as fluctuation of the
// dB-envelope around that smooth decay. We take the dB envelope, remove the slow
// decay trend with a long moving average, and report the residual ripple depth
// (RMS, in dB) and its dominant rate in 0.3-8 Hz. The modulation-off render is
// the baseline: an unmodulated sparse FDN can still have substantial envelope
// ripple, so this metric must not be read as a complete modulation audibility
// test.
TremStat measureRipple(const std::vector<float>& env, float envRate) {
    TremStat t;
    const size_t n = env.size();
    if (n < 128) return t;
    // dB envelope.
    std::vector<float> db(n);
    float peak = 1e-12f; for (float v : env) peak = std::max(peak, v);
    for (size_t i = 0; i < n; ++i) db[i] = 20.0f * std::log10(std::max(env[i], peak * 1e-6f));
    // Analyze the sustained tail: from ~150 ms (past the burst and early
    // reflections) down to ~45 dB below the TAIL-START level. Referencing the
    // tail start rather than the global peak keeps the window from collapsing on
    // the loud initial burst response.
    const size_t lo = std::min(n, static_cast<size_t>(envRate * 0.15f));
    if (lo + 64 >= n) return t;
    const float dbRef = db[lo];
    size_t hi = n;
    for (size_t i = lo; i < n; ++i) { if (db[i] < dbRef - 45.0f) { hi = i; break; } }
    if (hi <= lo + 64) return t;
    const size_t len = hi - lo;
    // Detrend with a quadratic (order-2) least-squares fit of the dB envelope.
    // This removes the decay AND its curvature (the early FDN knee) while keeping
    // all oscillatory fluctuation, including the sub-Hz tremolo that a moving
    // average would eat and that a straight line leaves as curvature error.
    // Normal equations for y = a + b*x + c*x^2, x normalized to [0,1].
    double S0 = len, S1 = 0, S2 = 0, S3 = 0, S4 = 0, T0 = 0, T1 = 0, T2 = 0;
    for (size_t i = 0; i < len; ++i) {
        const double x = static_cast<double>(i) / (len - 1), y = db[lo + i];
        const double x2 = x * x;
        S1 += x; S2 += x2; S3 += x2 * x; S4 += x2 * x2;
        T0 += y; T1 += x * y; T2 += x2 * y;
    }
    // Solve the 3x3 system by Cramer's rule.
    auto det3 = [](double a,double b,double c,double d,double e,double f,double g,double h,double i){
        return a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g); };
    const double D = det3(S0,S1,S2, S1,S2,S3, S2,S3,S4);
    double a = 0, b = 0, c = 0;
    if (std::abs(D) > 1e-12) {
        a = det3(T0,S1,S2, T1,S2,S3, T2,S3,S4) / D;
        b = det3(S0,T0,S2, S1,T1,S3, S2,T2,S4) / D;
        c = det3(S0,S1,T0, S1,S2,T1, S2,S3,T2) / D;
    } else { a = T0 / len; }
    std::vector<float> resid(len);
    for (size_t i = 0; i < len; ++i) {
        const double x = static_cast<double>(i) / (len - 1);
        resid[i] = static_cast<float>(db[lo + i] - (a + b * x + c * x * x));
    }

    double sumSq = 0; for (float v : resid) sumSq += (double)v * v;
    t.rippleDb = static_cast<float>(std::sqrt(sumSq / resid.size()));

    // Envelope spectrum: separate a coherent tremolo LINE (peak) from the
    // broadband beating floor (median). Band 0.2-25 Hz covers slow LFO wobble
    // through fast comb-beat flutter.
    size_t N = floorPow2(resid.size());
    if (N >= 64) {
        std::vector<std::complex<float>> buf(N);
        for (size_t i = 0; i < N; ++i) {
            const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (N - 1)));
            buf[i] = std::complex<float>(resid[i] * w, 0.0f);
        }
        fft(buf);
        const float binHz = envRate / static_cast<float>(N);
        double pk = 0; float pkHz = 0;
        std::vector<double> mags;
        for (size_t i = 1; i <= N / 2; ++i) {
            const float hz = i * binHz;
            if (hz < 0.1f || hz > 25.0f) continue;
            const double m = std::abs(buf[i]);
            mags.push_back(m);
            if (m > pk) { pk = m; pkHz = hz; }
        }
        t.rateHz = pkHz;
        if (!mags.empty()) {
            std::vector<double> s = mags; std::sort(s.begin(), s.end());
            const double med = s[s.size() / 2];
            t.peakProminence = med > 1e-12 ? static_cast<float>(pk / med) : 0.0f;
        }
        if (std::getenv("TREM_DEBUG")) {
            std::fprintf(stderr, "[dbg] len=%zu N=%zu binHz=%.4f mags=%zu pk=%.4g pkHz=%.4f rippleDb=%.2f\n",
                         len, N, binHz, mags.size(), pk, pkHz, t.rippleDb);
        }
    }
    return t;
}

// Render a short decorrelated noise burst then silence; return the L tail so its
// decaying envelope can be analyzed for pump/ripple.
std::vector<float> renderBurst(AestraVerb::Mode mode, float damping, float modDepth,
                               float sr, float seconds) {
    const size_t n = static_cast<size_t>(sr * seconds);
    const size_t burst = static_cast<size_t>(sr * 0.05f); // 50 ms broadband burst
    std::vector<float> inL(n, 0.0f), inR(n, 0.0f), outL(n), outR(n);
    uint32_t s1 = 12345, s2 = 67890;
    for (size_t i = 0; i < burst; ++i) {
        s1 = s1 * 1103515245u + 12345u; s2 = s2 * 1103515245u + 12345u;
        inL[i] = (static_cast<float>((s1 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
        inR[i] = (static_cast<float>((s2 >> 16) & 0x7FFF) / 16384.0f - 1.0f) * 0.5f;
    }
    AestraVerb v; v.initialize(sr, 256);
    v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
    v.setParameter(AestraVerb::kDecay, 0.6f);
    v.setParameter(AestraVerb::kSize, 0.6f);
    v.setParameter(AestraVerb::kDiffusion, 0.8f);
    v.setParameter(AestraVerb::kDamping, damping);
    v.setParameter(AestraVerb::kModRate, 0.5f);
    v.setParameter(AestraVerb::kModDepth, modDepth);
    v.setParameter(AestraVerb::kModCharacter, 0.0f); // Random (default)
    v.setParameter(AestraVerb::kWidth, 0.7f);
    v.setParameter(AestraVerb::kMix, 1.0f);
    v.activate();
    const float* in[2] = { inL.data(), inR.data() };
    float* out[2] = { outL.data(), outR.data() };
    const size_t block = 256;
    for (size_t off = 0; off < n; off += block) {
        const size_t f = std::min(block, n - off);
        const float* bi[2] = { inL.data() + off, inR.data() + off };
        float* bo[2] = { outL.data() + off, outR.data() + off };
        v.process(bi, bo, 2, 2, static_cast<uint32_t>(f));
    }
    return outL;
}

const char* modeName(AestraVerb::Mode m) {
    switch (m) {
        case AestraVerb::Mode::Room: return "room";
        case AestraVerb::Mode::Hall: return "hall";
        case AestraVerb::Mode::Plate: return "plate";
        case AestraVerb::Mode::Cathedral: return "cathedral";
        case AestraVerb::Mode::Chamber: return "chamber";
        case AestraVerb::Mode::BrightHall: return "bright_hall";
        case AestraVerb::Mode::Ambience: return "ambience";
        case AestraVerb::Mode::Scoring: return "scoring";
        case AestraVerb::Mode::SmoothPlate: return "smooth_plate";
    }
    return "?";
}

} // namespace

int main() {
    const float sr = 48000.0f;
    const float seconds = 8.0f; // burst + long decaying tail to analyze the pump
    const size_t envBlock = 256;
    const std::string dir = "labs/reverb/tremolo";
    std::error_code ec; std::filesystem::create_directories(dir, ec);

    const std::array<AestraVerb::Mode, 9> modes = {
        AestraVerb::Mode::Room, AestraVerb::Mode::Hall, AestraVerb::Mode::Plate,
        AestraVerb::Mode::Cathedral, AestraVerb::Mode::Chamber, AestraVerb::Mode::BrightHall,
        AestraVerb::Mode::Ambience, AestraVerb::Mode::Scoring, AestraVerb::Mode::SmoothPlate };

    std::cout << "AestraVerb Tremolo (amplitude-modulation) measurement\n";
    std::cout << "=====================================================\n\n";

    std::ostringstream md, js;
    md << "# AestraVerb Tremolo / Tail-Pump Measurement\n\n"
       << "50 ms broadband burst then silence; the decaying tail's dB envelope is detrended and "
          "the residual ripple measured. Default settings (Random mod, depth 0.3, damping 0.5, "
          "Mix 100%). RippleDb = RMS pump of the tail around its smooth decay (dB); WobbleHz = its "
          "dominant rate; the mod-OFF column is the unmodulated FDN baseline; "
          "the band columns split the output into low/mid/high.\n\n"
       << "| Mode | WobbleHz | RippleDb (mod on) | RippleDb (mod OFF) | Low | Mid | High |\n"
       << "|------|----------|-------------------|--------------------|-----|-----|------|\n";
    js << "{\n  \"schemaVersion\": \"1.0.0\",\n  \"config\": {\"modChar\":\"Random\",\"modDepth\":0.3,\"damping\":0.5,\"metric\":\"tail dB ripple RMS\"},\n  \"modes\": [\n";

    for (size_t mi = 0; mi < modes.size(); ++mi) {
        const auto mode = modes[mi];
        auto tail = renderBurst(mode, 0.5f, 0.3f, sr, seconds);
        auto tailNoMod = renderBurst(mode, 0.5f, 0.0f, sr, seconds); // control
        // NB: compute each envelope into a variable BEFORE measureRipple — passing
        // the by-ref envRate out-param and the same envRate as a second arg in one
        // call is unspecified evaluation order (bit us: envRate read as 0).
        float envRate = 0.0f;
        auto eOn = envelope(tail, envBlock, envRate, sr);       const TremStat on = measureRipple(eOn, envRate);
        auto eOff = envelope(tailNoMod, envBlock, envRate, sr);  const TremStat off = measureRipple(eOff, envRate);

        auto bands = split3(tail, sr);
        float er = 0.0f;
        auto eLo = envelope(bands.lo, envBlock, er, sr);   const TremStat lo = measureRipple(eLo, er);
        auto eMid = envelope(bands.mid, envBlock, er, sr); const TremStat mid = measureRipple(eMid, er);
        auto eHi = envelope(bands.hi, envBlock, er, sr);   const TremStat hi = measureRipple(eHi, er);

        md << "| " << modeName(mode)
           << " | " << std::fixed << std::setprecision(2) << on.rateHz
           << " | " << std::setprecision(2) << on.rippleDb
           << " | " << off.rippleDb
           << " | " << lo.rippleDb << " | " << mid.rippleDb << " | " << hi.rippleDb
           << " |\n";
        js << (mi ? ",\n" : "") << "    {\"mode\":\"" << modeName(mode) << "\",\"wobbleHz\":" << on.rateHz
           << ",\"rippleOnDb\":" << on.rippleDb << ",\"rippleOffDb\":" << off.rippleDb
           << ",\"lowDb\":" << lo.rippleDb << ",\"midDb\":" << mid.rippleDb
           << ",\"highDb\":" << hi.rippleDb << "}";
        std::cout << modeName(mode) << ": wobble " << std::fixed << std::setprecision(2) << on.rateHz
                  << " Hz, ripple " << std::setprecision(2) << on.rippleDb << " dB (off "
                  << off.rippleDb << ") prom " << std::setprecision(1) << on.peakProminence << "x (off "
                  << off.peakProminence << "x) | L/M/H " << std::setprecision(2) << lo.rippleDb << "/" << mid.rippleDb
                  << "/" << hi.rippleDb << " dB\n";
    }
    js << "\n  ],\n";

    // Does MORE modulation smear the FDN beating? Sweep depth 0 / 0.3 / 1.0.
    md << "\n## Modulation-depth effect on tail ripple (does the LFO smear the beating?)\n\n"
       << "| Mode | Ripple @depth 0 | @depth 0.3 | @depth 1.0 |\n|------|-----------------|------------|------------|\n";
    js << "  \"modDepthSweep\": [\n";
    const std::array<AestraVerb::Mode, 4> depthModes = { AestraVerb::Mode::Hall, AestraVerb::Mode::Plate, AestraVerb::Mode::Room, AestraVerb::Mode::SmoothPlate };
    for (size_t i = 0; i < depthModes.size(); ++i) {
        float er = 0.0f;
        auto e0 = envelope(renderBurst(depthModes[i], 0.5f, 0.0f, sr, seconds), envBlock, er, sr); const float r0 = measureRipple(e0, er).rippleDb;
        auto e3 = envelope(renderBurst(depthModes[i], 0.5f, 0.3f, sr, seconds), envBlock, er, sr); const float r3 = measureRipple(e3, er).rippleDb;
        auto e1 = envelope(renderBurst(depthModes[i], 0.5f, 1.0f, sr, seconds), envBlock, er, sr); const float r1 = measureRipple(e1, er).rippleDb;
        md << "| " << modeName(depthModes[i]) << " | " << std::setprecision(2) << r0 << " | " << r3 << " | " << r1 << " |\n";
        js << (i ? ",\n" : "") << "    {\"mode\":\"" << modeName(depthModes[i]) << "\",\"depth0\":" << r0 << ",\"depth03\":" << r3 << ",\"depth1\":" << r1 << "}";
        std::cout << "modDepth " << modeName(depthModes[i]) << ": " << std::setprecision(2) << r0 << " (0) / " << r3 << " (0.3) / " << r1 << " (1.0) dB\n";
    }
    js << "\n  ],\n";

    md << "\n## Damping exposure (tail ripple dB at default 0.5 vs low 0.1 damping)\n\n"
       << "| Mode | Ripple @damp 0.5 | Ripple @damp 0.1 |\n|------|------------------|------------------|\n";
    js << "  \"dampingExposure\": [\n";
    const std::array<AestraVerb::Mode, 3> dampModes = { AestraVerb::Mode::Plate, AestraVerb::Mode::Hall, AestraVerb::Mode::Room };
    for (size_t i = 0; i < dampModes.size(); ++i) {
        float er = 0.0f;
        auto e05 = envelope(renderBurst(dampModes[i], 0.5f, 0.3f, sr, seconds), envBlock, er, sr);
        const float d05 = measureRipple(e05, er).rippleDb;
        auto e01 = envelope(renderBurst(dampModes[i], 0.1f, 0.3f, sr, seconds), envBlock, er, sr);
        const float d01 = measureRipple(e01, er).rippleDb;
        md << "| " << modeName(dampModes[i]) << " | " << std::setprecision(2) << d05 << " | " << d01 << " |\n";
        js << (i ? ",\n" : "") << "    {\"mode\":\"" << modeName(dampModes[i]) << "\",\"rippleDamp05\":" << d05 << ",\"rippleDamp01\":" << d01 << "}";
        std::cout << "damping " << modeName(dampModes[i]) << ": " << std::setprecision(2) << d05 << " dB (0.5) -> " << d01 << " dB (0.1)\n";
    }
    js << "\n  ]\n}\n";

    // Ear-confirmation A/B: a sustained chord stab (exposes the tail pump)
    // through Plate and Hall at modulation depth 0.0 vs MAX. If these sound the
    // same, the tremolo is not the modulation (confirms the measurement by ear).
    {
        const std::string abDir = dir + "/ab";
        std::filesystem::create_directories(abDir, ec);
        if (ec || !std::filesystem::is_directory(abDir)) {
            std::cerr << "ERROR: could not create " << abDir << ": " << ec.message() << "\n";
            return 1;
        }
        const float chordDur = 4.0f;
        const size_t src = static_cast<size_t>(sr * chordDur);
        const size_t n = src + static_cast<size_t>(sr * 4.0f);
        const float freqs[] = { 261.63f, 329.63f, 392.00f };
        std::vector<float> dryL(n, 0.0f), dryR(n, 0.0f);
        for (size_t i = 0; i < src; ++i) {
            const float t = i / sr;
            float env = t < 0.01f ? t / 0.01f : (t > chordDur - 0.5f ? (chordDur - t) / 0.5f : 1.0f);
            float v = 0; for (float f : freqs) v += std::sin(2 * kPi * f * t) + 0.3f * std::sin(2 * kPi * 2 * f * t);
            dryL[i] = dryR[i] = v * env * 0.12f;
        }
        auto rmsv = [](const std::vector<float>& x) { double a = 0; for (float v : x) a += (double)v * v; return std::sqrt(a / std::max<size_t>(x.size(), 1)); };
        struct AB {
            const char* name;
            AestraVerb::Mode mode;
            float depth;
            float damping;
        };
        const AB abs_[] = {
            {"plate_modOFF", AestraVerb::Mode::Plate, 0.0f, 0.5f},
            {"plate_modDEFAULT", AestraVerb::Mode::Plate, 0.07f, 0.5f},
            {"plate_modLEGACY", AestraVerb::Mode::Plate, 0.14f, 0.5f},
            {"plate_modSTRONG", AestraVerb::Mode::Plate, 0.3f, 0.5f},
            {"plate_modMAX", AestraVerb::Mode::Plate, 1.0f, 0.5f},
            {"plate_lowDamp_modDEFAULT", AestraVerb::Mode::Plate, 0.07f, 0.1f},
            {"hall_modOFF", AestraVerb::Mode::Hall, 0.0f, 0.5f},
            {"hall_modDEFAULT", AestraVerb::Mode::Hall, 0.07f, 0.5f},
            {"hall_modLEGACY", AestraVerb::Mode::Hall, 0.14f, 0.5f},
            {"hall_modSTRONG", AestraVerb::Mode::Hall, 0.3f, 0.5f},
            {"hall_modMAX", AestraVerb::Mode::Hall, 1.0f, 0.5f},
            {"hall_lowDamp_modDEFAULT", AestraVerb::Mode::Hall, 0.07f, 0.1f},
            {"room_modOFF", AestraVerb::Mode::Room, 0.0f, 0.5f},
            {"room_modDEFAULT", AestraVerb::Mode::Room, 0.07f, 0.5f},
            {"room_modLEGACY", AestraVerb::Mode::Room, 0.14f, 0.5f},
            {"room_modSTRONG", AestraVerb::Mode::Room, 0.3f, 0.5f},
            {"room_lowDamp_modDEFAULT", AestraVerb::Mode::Room, 0.07f, 0.1f},
        };
        std::ofstream key(abDir + "/KEY.md");
        key << "# Tremolo A/B — modulation and damping sweep\n\nSame chord stab, equal loudness. Compare "
               "modOFF through modMAX to hear how moving FDN peaks/notches change even when the "
               "broadband tail-envelope ripple remains similar. DEFAULT is the shipped 0.07 depth; LEGACY is the "
               "previous 0.14 default. The lab holds Mod Rate at 0.5; STRONG is depth 0.30.\n\n"
               "| File | Mode | Mod depth | Damping |\n"
               "|---|---|---:|---:|\n";
        for (const auto& a : abs_) {
            AestraVerb v; v.initialize(sr, 256);
            v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(a.mode));
            v.setParameter(AestraVerb::kDecay, 0.7f); v.setParameter(AestraVerb::kSize, 0.6f);
            v.setParameter(AestraVerb::kDiffusion, 0.8f); v.setParameter(AestraVerb::kDamping, a.damping);
            v.setParameter(AestraVerb::kModRate, 0.5f); v.setParameter(AestraVerb::kModDepth, a.depth);
            v.setParameter(AestraVerb::kWidth, 0.7f); v.setParameter(AestraVerb::kMix, 0.4f);
            v.activate();
            std::vector<float> oL(n), oR(n);
            const float* in[2] = { dryL.data(), dryR.data() }; float* out[2] = { oL.data(), oR.data() };
            const size_t block = 256;
            for (size_t off = 0; off < n; off += block) {
                const size_t f = std::min(block, n - off);
                const float* bi[2] = { dryL.data() + off, dryR.data() + off }; float* bo[2] = { oL.data() + off, oR.data() + off };
                v.process(bi, bo, 2, 2, static_cast<uint32_t>(f));
            }
            const double g = 0.1 / std::max(std::sqrt((rmsv(oL)*rmsv(oL) + rmsv(oR)*rmsv(oR)) * 0.5), 1e-9); // combined stereo RMS
            for (auto& s : oL) s = float(s * g);
            for (auto& s : oR) s = float(s * g);
            if (!writeWav(abDir + "/" + a.name + ".wav", oL, oR, sr)) {
                std::cerr << "ERROR: failed to write " << abDir << "/" << a.name << ".wav\n";
                return 1;
            }
            key << "| " << a.name << ".wav | " << modeName(a.mode) << " | " << a.depth
                << " | " << a.damping << " |\n";
        }
        if (!key) { std::cerr << "ERROR: failed to write A/B KEY\n"; return 1; }
        std::cout << "A/B ear-confirmation renders written to " << abDir << "/\n";
    }

    bool wroteOk = true;
    { std::ofstream f(dir + "/tremolo_report.md"); f << md.str(); wroteOk &= static_cast<bool>(f); }
    { std::ofstream f(dir + "/tremolo_report.json"); f << js.str(); wroteOk &= static_cast<bool>(f); }
    if (!wroteOk) { std::cerr << "ERROR: failed to write tremolo report\n"; return 1; }
    std::cout << "\nTremolo report written to " << dir << "/\n";
    return 0;
}
