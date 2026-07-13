// AestraVerb Harmonic-Motion Measurement Lab.
//
// The audible "tremolo/ring/sheet" is the FDN's narrow peaks/notches moving
// across sustained source tones: individual frequencies swing in level even when
// the broadband tail loudness barely changes (which is why a tail-envelope
// metric misses it). This lab measures that directly: it holds a comb of steady
// tones through the reverb and tracks how much each tone's level moves over time
// (dB swing), reporting the typical (median) and worst-case swing per config.
// It sweeps modulation depth so a "calmer motion" target can be chosen against a
// budget of ~<=0.5 dB typical / <=2-3 dB worst-harmonic movement.
//
// Output: labs/reverb/tremolo/harmonic_motion.md / .json

#include "AestraVerb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
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

// A log-spaced comb of probe tones spanning the musical range.
std::vector<float> combFreqs() {
    std::vector<float> f;
    for (double hz = 120.0; hz <= 8000.0; hz *= std::pow(2.0, 1.0 / 6.0)) f.push_back(static_cast<float>(hz)); // 1/6-oct
    return f;
}

// Render a steady tone comb through one config; return L output.
std::vector<float> renderComb(AestraVerb::Mode mode, float damping, float modDepth, float modRate,
                              float sr, float seconds, const std::vector<float>& freqs) {
    const size_t n = static_cast<size_t>(sr * seconds);
    std::vector<float> in(n, 0.0f), outL(n), outR(n);
    // Equal-amplitude comb; random start phases so the sum doesn't spike.
    uint32_t s = 777;
    std::vector<float> ph(freqs.size());
    for (auto& p : ph) { s = s * 1103515245u + 12345u; p = (static_cast<float>((s >> 9) & 0x7FFF) / 32768.0f) * 2.0f * kPi; }
    const float amp = 0.4f / std::sqrt(static_cast<float>(freqs.size()));
    for (size_t i = 0; i < n; ++i) {
        const float t = i / sr;
        float v = 0.0f;
        for (size_t k = 0; k < freqs.size(); ++k) v += std::sin(2.0f * kPi * freqs[k] * t + ph[k]);
        in[i] = v * amp;
    }
    AestraVerb v; v.initialize(sr, 256);
    v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
    v.setParameter(AestraVerb::kDecay, 0.6f);
    v.setParameter(AestraVerb::kSize, 0.6f);
    v.setParameter(AestraVerb::kDiffusion, 0.8f);
    v.setParameter(AestraVerb::kDamping, damping);
    v.setParameter(AestraVerb::kModRate, modRate);
    v.setParameter(AestraVerb::kModDepth, modDepth);
    v.setParameter(AestraVerb::kModCharacter, 0.0f);
    v.setParameter(AestraVerb::kWidth, 0.7f);
    v.setParameter(AestraVerb::kMix, 1.0f);
    v.activate();
    const float* ip[2] = { in.data(), in.data() };
    float* op[2] = { outL.data(), outR.data() };
    const size_t block = 256;
    for (size_t off = 0; off < n; off += block) {
        const size_t f = std::min(block, n - off);
        const float* bi[2] = { in.data() + off, in.data() + off };
        float* bo[2] = { outL.data() + off, outR.data() + off };
        v.process(bi, bo, 2, 2, static_cast<uint32_t>(f));
    }
    return outL;
}

struct MotionStat { float typicalDb = 0.0f; float worstDb = 0.0f; float worstHz = 0.0f; };

// For each probe tone, track its level over time via a sliding FFT and report the
// swing (RMS of the dB level around its mean). Typical = median over tones,
// worst = max. Steady state, so the mean is the level and the fluctuation is the
// moving peak/notch.
MotionStat measureMotion(const std::vector<float>& out, float sr, const std::vector<float>& freqs) {
    MotionStat m;
    const size_t win = 2048, hop = 512;
    const size_t startSkip = static_cast<size_t>(sr * 1.0f); // drop buildup
    if (out.size() < startSkip + win + hop) return m;
    // Precompute bin index per tone.
    std::vector<size_t> bin(freqs.size());
    for (size_t k = 0; k < freqs.size(); ++k) bin[k] = std::min(win / 2, static_cast<size_t>(std::round(freqs[k] * win / sr)));
    // Per-tone dB level series.
    std::vector<std::vector<float>> series(freqs.size());
    std::vector<std::complex<float>> buf(win);
    for (size_t off = startSkip; off + win <= out.size(); off += hop) {
        for (size_t i = 0; i < win; ++i) {
            const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (win - 1)));
            buf[i] = std::complex<float>(out[off + i] * w, 0.0f);
        }
        fft(buf);
        for (size_t k = 0; k < freqs.size(); ++k) {
            const double mag = std::abs(buf[bin[k]]) + 1e-12;
            series[k].push_back(static_cast<float>(20.0 * std::log10(mag)));
        }
    }
    std::vector<float> swings;
    for (size_t k = 0; k < freqs.size(); ++k) {
        const auto& s = series[k];
        if (s.size() < 8) continue;
        double mean = 0; for (float v : s) mean += v; mean /= s.size();
        double sq = 0; for (float v : s) sq += (v - mean) * (v - mean);
        const float sw = static_cast<float>(std::sqrt(sq / s.size()));
        swings.push_back(sw);
        if (sw > m.worstDb) { m.worstDb = sw; m.worstHz = freqs[k]; }
    }
    if (!swings.empty()) {
        std::sort(swings.begin(), swings.end());
        m.typicalDb = swings[swings.size() / 2];
    }
    return m;
}

const char* modeName(AestraVerb::Mode m) {
    switch (m) {
        case AestraVerb::Mode::Room: return "room";
        case AestraVerb::Mode::Hall: return "hall";
        case AestraVerb::Mode::Plate: return "plate";
        case AestraVerb::Mode::SmoothPlate: return "smooth_plate";
        default: return "?";
    }
}

} // namespace

int main() {
    const float sr = 48000.0f, seconds = 7.0f, rate = 0.5f;
    const auto freqs = combFreqs();
    const std::string dir = "labs/reverb/tremolo";
    std::error_code ec; std::filesystem::create_directories(dir, ec);

    std::cout << "AestraVerb Harmonic-Motion measurement (" << freqs.size() << " probe tones)\n";
    std::cout << "=========================================================\n\n";

    std::ostringstream md, js;
    md << "# AestraVerb Harmonic-Motion Measurement\n\n"
       << "Steady 1/6-octave tone comb held through the reverb; each tone's level is tracked over "
          "time and its dB swing measured (typical = median over tones, worst = max). This captures "
          "the FDN peaks/notches moving across sustained tones. Budget target: typical <= ~0.5 dB, "
          "worst-harmonic <= ~2-3 dB.\n\n"
       << "## Mod-depth sweep (damping 0.5)\n\n"
       << "| Mode | depth | Typical dB | Worst dB | Worst Hz |\n|------|-------|-----------|----------|----------|\n";
    js << "{\n  \"probeTones\": " << freqs.size() << ",\n  \"sweep\": [\n";

    const std::array<AestraVerb::Mode, 4> modes = { AestraVerb::Mode::Room, AestraVerb::Mode::Hall, AestraVerb::Mode::Plate, AestraVerb::Mode::SmoothPlate };
    const std::array<float, 4> depths = { 0.0f, 0.07f, 0.14f, 0.30f }; // off / default (0.07) / old-default / strong
    bool firstJs = true;
    for (auto mode : modes) {
        for (float d : depths) {
            auto out = renderComb(mode, 0.5f, d, rate, sr, seconds, freqs);
            const MotionStat s = measureMotion(out, sr, freqs);
            md << "| " << modeName(mode) << " | " << std::fixed << std::setprecision(2) << d
               << " | " << std::setprecision(2) << s.typicalDb << " | " << s.worstDb
               << " | " << std::setprecision(0) << s.worstHz << " |\n";
            js << (firstJs ? "    " : ",\n    ") << "{\"mode\":\"" << modeName(mode) << "\",\"depth\":" << d
               << ",\"typicalDb\":" << s.typicalDb << ",\"worstDb\":" << s.worstDb << ",\"worstHz\":" << s.worstHz << "}";
            firstJs = false;
            std::cout << modeName(mode) << " depth " << std::fixed << std::setprecision(2) << d
                      << ": typical " << s.typicalDb << " dB, worst " << s.worstDb << " dB @ "
                      << std::setprecision(0) << s.worstHz << " Hz\n";
        }
    }
    js << "\n  ]\n}\n";

    { std::ofstream f(dir + "/harmonic_motion.md"); if (f) f << md.str(); }
    { std::ofstream f(dir + "/harmonic_motion.json"); if (f) f << js.str(); }
    std::cout << "\nHarmonic-motion report written to " << dir << "/\n";

    // Prototype A/B render: when PROTO_LABEL is set, render a sustained chord
    // through Plate and Room at the current build's voicing (and PROTO_MODDEPTH
    // if given), equal-loudness, tagged by the label — so candidate builds can be
    // rendered and compared by ear. Also measures each so the numbers accompany
    // the audio.
    if (const char* label = std::getenv("PROTO_LABEL")) {
        const std::string protoDir = dir + "/proto";
        std::filesystem::create_directories(protoDir, ec);
        float modDepth = 0.07f; // shipping default
        if (const char* md_ = std::getenv("PROTO_MODDEPTH")) modDepth = std::stof(md_);
        auto writeWav = [](const std::string& p, const std::vector<float>& l, const std::vector<float>& r, float srr) {
            std::ofstream f(p, std::ios::binary); if (!f) return;
            const uint32_t n = static_cast<uint32_t>(l.size()), ds = n * 8, fsz = 36 + ds, br = static_cast<uint32_t>(srr) * 8;
            auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v),4); };
            auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v),2); };
            f.write("RIFF",4); u32(fsz); f.write("WAVE",4); f.write("fmt ",4); u32(16); u16(3); u16(2);
            u32(static_cast<uint32_t>(srr)); u32(br); u16(8); u16(32); f.write("data",4); u32(ds);
            for (uint32_t i=0;i<n;++i){ f.write(reinterpret_cast<const char*>(&l[i]),4); f.write(reinterpret_cast<const char*>(&r[i]),4);} };
        const std::array<AestraVerb::Mode, 2> pm = { AestraVerb::Mode::Plate, AestraVerb::Mode::Room };
        const float dur = 4.0f; const size_t src = static_cast<size_t>(sr * dur), nn = src + static_cast<size_t>(sr * 4.0f);
        const float ch[] = { 261.63f, 329.63f, 392.00f };
        std::vector<float> dry(nn, 0.0f);
        for (size_t i = 0; i < src; ++i) { const float t = i / sr; float e = t < 0.01f ? t/0.01f : (t > dur-0.5f ? (dur-t)/0.5f : 1.0f); float v=0; for (float f: ch) v += std::sin(2*kPi*f*t)+0.3f*std::sin(2*kPi*2*f*t); dry[i]=v*e*0.12f; }
        auto rms=[](const std::vector<float>&x){ double a=0; for(float v:x)a+=(double)v*v; return std::sqrt(a/std::max<size_t>(x.size(),1)); };
        for (auto mode : pm) {
            AestraVerb v; v.initialize(sr, 256);
            v.setParameter(AestraVerb::kMode, AestraVerb::modeParam(mode));
            v.setParameter(AestraVerb::kDecay,0.7f); v.setParameter(AestraVerb::kSize,0.6f); v.setParameter(AestraVerb::kDiffusion,0.8f);
            v.setParameter(AestraVerb::kDamping,0.5f); v.setParameter(AestraVerb::kModRate,rate); v.setParameter(AestraVerb::kModDepth,modDepth);
            v.setParameter(AestraVerb::kWidth,0.7f); v.setParameter(AestraVerb::kMix,0.4f); v.activate();
            std::vector<float> oL(nn), oR(nn);
            const size_t block=256;
            for (size_t off=0; off<nn; off+=block){ const size_t f=std::min(block,nn-off); const float* bi[2]={dry.data()+off,dry.data()+off}; float* bo[2]={oL.data()+off,oR.data()+off}; v.process(bi,bo,2,2,static_cast<uint32_t>(f)); }
            const double g=0.1/std::max(rms(oL),1e-9); for(auto&s:oL)s=float(s*g); for(auto&s:oR)s=float(s*g);
            writeWav(protoDir + "/" + modeName(mode) + "_" + label + ".wav", oL, oR, sr);
            std::cout << "proto " << modeName(mode) << " [" << label << " depth " << modDepth << "] written\n";
        }
    }
    return 0;
}
