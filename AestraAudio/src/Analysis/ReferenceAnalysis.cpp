// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Analysis/ReferenceAnalysis.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {

namespace {

constexpr double kPi = 3.14159265358979323846;

double toDb(double linear) {
    return linear > 0.0 ? 20.0 * std::log10(linear) : AudioAnalysis::kSilenceDb;
}
double sanitize(float x) {
    return std::isfinite(x) ? static_cast<double>(x) : 0.0;
}

// One 2nd-order Butterworth section (RBJ), applied twice for a 4th-order slope.
struct Section2 {
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    Section2(double rate, double hz, bool high) {
        const double w0 = 2.0 * kPi * hz / rate, c = std::cos(w0), alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + alpha;
        b1 = (high ? -(1.0 + c) : (1.0 - c)) / a0;
        b0 = (high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0) / a0;
        b2 = b0;
        a1 = -2.0 * c / a0;
        a2 = (1.0 - alpha) / a0;
    }
    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
struct Cross {
    Section2 a, b;
    Cross(double rate, double hz, bool high) : a(rate, hz, high), b(rate, hz, high) {}
    double process(double x) { return b.process(a.process(x)); }
};

// Splits one signal into the five analysis bands, one sample at a time.
struct BandSplitter {
    std::vector<Cross> lows, highs;
    size_t edges = 0;
    explicit BandSplitter(double rate) {
        for (double edge : AudioAnalysis::kBandEdgesHz) {
            if (edge >= rate * 0.5) break;
            lows.emplace_back(rate, edge, false);
            highs.emplace_back(rate, edge, true);
            ++edges;
        }
    }
    void split(double x, double (&out)[AudioAnalysis::kBandCount]) {
        for (double& v : out) v = 0.0;
        double rest = x;
        for (size_t e = 0; e < edges; ++e) {
            out[e] = lows[e].process(rest);
            rest = highs[e].process(rest);
        }
        out[edges] = rest;
    }
};

} // namespace

double peakToLoudnessRatioDb(const AudioAnalysis& a) {
    if (a.truePeakDbtp <= AudioAnalysis::kSilenceDb || a.integratedLufs <= AudioAnalysis::kSilenceDb) return 0.0;
    return a.truePeakDbtp - a.integratedLufs;
}

StereoDetail analyzeStereoDetail(const float* x, uint64_t frames, uint32_t channels, uint32_t rate) {
    StereoDetail out;
    if (!x || channels != 2 || frames == 0 || rate == 0) return out;

    BandSplitter mid(rate), side(rate);
    double midE[AudioAnalysis::kBandCount] = {}, sideE[AudioAnalysis::kBandCount] = {};
    double m[AudioAnalysis::kBandCount], s[AudioAnalysis::kBandCount];

    const uint64_t window = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(rate * StereoDetail::kWindowSeconds)));
    double ll = 0, rr = 0, lr = 0;
    for (uint64_t f = 0; f < frames; ++f) {
        const double l = sanitize(x[f * 2]), r = sanitize(x[f * 2 + 1]);
        mid.split((l + r) * 0.5, m);
        side.split((l - r) * 0.5, s);
        for (size_t b = 0; b < AudioAnalysis::kBandCount; ++b) {
            midE[b] += m[b] * m[b];
            sideE[b] += s[b] * s[b];
        }
        ll += l * l; rr += r * r; lr += l * r;
        if ((f + 1) % window == 0 || f + 1 == frames) {
            const bool full = (f + 1) % window == 0;
            if (full || ll > 0.0) {
                const double c = (ll > 0.0 && rr > 0.0) ? std::clamp(lr / std::sqrt(ll * rr), -1.0, 1.0) : 0.0;
                if (full) {
                    const double startSec = static_cast<double>(out.correlationOverTime.size()) * StereoDetail::kWindowSeconds;
                    out.correlationOverTime.push_back(static_cast<float>(c));
                    if (ll > 0.0 && rr > 0.0 && c < StereoDetail::kMonoProblemCorrelation) out.monoProblemStartsSec.push_back(startSec);
                }
            }
            ll = rr = lr = 0.0;
        }
    }
    for (size_t b = 0; b < AudioAnalysis::kBandCount; ++b) {
        const double total = midE[b] + sideE[b];
        out.bandWidth[b] = total > 0.0 ? sideE[b] / total : 0.0;
    }
    return out;
}

std::vector<Section> detectSections(const float* x, uint64_t frames, uint32_t channels, uint32_t rate) {
    std::vector<Section> sections;
    if (!x || frames == 0 || channels == 0 || rate == 0) return sections;

    // Features per half-second block: level and the five band shares of the mono mix.
    const uint64_t block = std::max<uint64_t>(1, rate / 2);
    const size_t blocks = static_cast<size_t>((frames + block - 1) / block);
    struct Feature { double v[1 + AudioAnalysis::kBandCount]; };
    std::vector<Feature> features(blocks);
    std::vector<double> blockPeak(blocks, 0.0), blockEnergy(blocks, 0.0);

    BandSplitter splitter(rate);
    double bands[AudioAnalysis::kBandCount];
    double bandE[AudioAnalysis::kBandCount] = {};
    for (uint64_t f = 0; f < frames; ++f) {
        double mono = 0.0, peak = 0.0;
        for (uint32_t c = 0; c < channels; ++c) {
            const double v = sanitize(x[f * channels + c]);
            mono += v; peak = std::max(peak, std::fabs(v));
        }
        mono /= channels;
        const size_t b = static_cast<size_t>(f / block);
        blockPeak[b] = std::max(blockPeak[b], peak);
        blockEnergy[b] += mono * mono;
        splitter.split(mono, bands);
        for (size_t k = 0; k < AudioAnalysis::kBandCount; ++k) bandE[k] += bands[k] * bands[k];
        if ((f + 1) % block == 0 || f + 1 == frames) {
            const uint64_t n = (f % block) + 1;
            double total = 0.0;
            for (double e : bandE) total += e;
            features[b].v[0] = toDb(std::sqrt(blockEnergy[b] / static_cast<double>(n))) / 20.0; // ~ scaled so 20 dB = 1
            for (size_t k = 0; k < AudioAnalysis::kBandCount; ++k) features[b].v[1 + k] = total > 0.0 ? bandE[k] / total : 0.0;
            for (double& e : bandE) e = 0.0;
        }
    }

    // Novelty: how different the four blocks after a point are from the four before it.
    constexpr size_t kSide = 4;
    constexpr double kThreshold = 0.35;
    constexpr size_t kMinSpacing = 8; // blocks (4 s)
    std::vector<double> novelty(blocks, 0.0);
    for (size_t i = kSide; i + kSide <= blocks; ++i) {
        double dist = 0.0;
        for (size_t k = 0; k < 1 + AudioAnalysis::kBandCount; ++k) {
            double before = 0.0, after = 0.0;
            for (size_t j = 0; j < kSide; ++j) { before += features[i - 1 - j].v[k]; after += features[i + j].v[k]; }
            const double d = (after - before) / kSide;
            dist += d * d;
        }
        novelty[i] = std::sqrt(dist);
    }
    std::vector<size_t> cuts;
    for (size_t i = kSide; i + kSide <= blocks; ++i) {
        if (novelty[i] < kThreshold) continue;
        const size_t lo = i >= kSide ? i - kSide : 0, hi = std::min(blocks, i + kSide + 1);
        if (novelty[i] < *std::max_element(novelty.begin() + static_cast<long>(lo), novelty.begin() + static_cast<long>(hi))) continue;
        if (!cuts.empty() && i - cuts.back() < kMinSpacing) continue;
        cuts.push_back(i);
    }

    const double blockSec = static_cast<double>(block) / rate;
    const double totalSec = static_cast<double>(frames) / rate;
    std::vector<size_t> edges{0};
    edges.insert(edges.end(), cuts.begin(), cuts.end());
    edges.push_back(blocks);
    for (size_t e = 0; e + 1 < edges.size(); ++e) {
        Section s;
        s.startSec = static_cast<double>(edges[e]) * blockSec;
        s.endSec = e + 2 == edges.size() ? totalSec : static_cast<double>(edges[e + 1]) * blockSec;
        double energy = 0.0, peak = 0.0;
        uint64_t n = 0;
        for (size_t b = edges[e]; b < edges[e + 1]; ++b) {
            energy += blockEnergy[b];
            peak = std::max(peak, blockPeak[b]);
            n += std::min<uint64_t>(block, frames - static_cast<uint64_t>(b) * block);
        }
        s.rmsDb = n ? toDb(std::sqrt(energy / static_cast<double>(n))) : AudioAnalysis::kSilenceDb;
        s.peakDb = toDb(peak);
        s.crestDb = (s.peakDb > AudioAnalysis::kSilenceDb && s.rmsDb > AudioAnalysis::kSilenceDb) ? s.peakDb - s.rmsDb : 0.0;
        sections.push_back(s);
    }
    return sections;
}

double levelMatchGainDb(double mixLufs, double referenceLufs) {
    if (mixLufs <= AudioAnalysis::kSilenceDb || referenceLufs <= AudioAnalysis::kSilenceDb) return 0.0;
    return referenceLufs - mixLufs;
}

double levelMatchGainDbForRange(const float* mix, uint64_t mixFrames, uint32_t mixChannels, uint32_t mixRate,
                                double mixStartSec, double mixEndSec, const float* reference, uint64_t referenceFrames,
                                uint32_t referenceChannels, uint32_t referenceRate, double referenceStartSec,
                                double referenceEndSec) {
    auto measure = [](const float* data, uint64_t frames, uint32_t channels, uint32_t rate, double startSec, double endSec) {
        if (!data || channels == 0 || rate == 0) return AudioAnalysis::kSilenceDb;
        const uint64_t a = std::min<uint64_t>(frames, static_cast<uint64_t>(std::max(0.0, startSec) * rate));
        const uint64_t b = std::min<uint64_t>(frames, static_cast<uint64_t>(std::max(0.0, endSec) * rate));
        if (b <= a) return AudioAnalysis::kSilenceDb;
        return analyzeAudio(data + a * channels, b - a, channels, rate).integratedLufs;
    };
    return levelMatchGainDb(measure(mix, mixFrames, mixChannels, mixRate, mixStartSec, mixEndSec),
                            measure(reference, referenceFrames, referenceChannels, referenceRate, referenceStartSec, referenceEndSec));
}

std::array<double, AudioAnalysis::kBandCount> tonalBalanceDeltaDb(const AudioAnalysis& mix, const AudioAnalysis& reference) {
    std::array<double, AudioAnalysis::kBandCount> out{};
    for (size_t b = 0; b < AudioAnalysis::kBandCount; ++b) {
        if (mix.bandShare[b] > 0.0 && reference.bandShare[b] > 0.0) out[b] = 10.0 * std::log10(mix.bandShare[b] / reference.bandShare[b]);
    }
    return out;
}

} // namespace Audio
} // namespace Aestra
