// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Analysis/AudioAnalysis.h"

#include "DSP/KWeighting.h"
#include "DSP/TruePeakMeter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Aestra {
namespace Audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAbsoluteGateLufs = -70.0;
constexpr double kRelativeGateLu = -10.0;    // BS.1770-4 integrated
constexpr double kRangeRelativeGateLu = -20.0; // EBU Tech 3342 loudness range

// Transposed direct form II, double precision: the analysis runs offline, so
// accuracy matters more than speed.
struct Filter {
    KWeighting::Biquad c;
    double z1 = 0.0, z2 = 0.0;
    double process(double x) {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

double toDb(double linear) {
    return linear > 0.0 ? 20.0 * std::log10(linear) : AudioAnalysis::kSilenceDb;
}

double powerToLufs(double meanSquare) {
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : AudioAnalysis::kSilenceDb;
}

// BS.1770 channel weight. 5.1 is L R C LFE Ls Rs: LFE excluded, surrounds +1.5 dB.
double channelWeight(uint32_t channel, uint32_t channels) {
    if (channels == 6) {
        if (channel == 3) return 0.0;
        if (channel >= 4) return 1.41;
    }
    return 1.0;
}

double sanitize(float x) {
    return std::isfinite(x) ? static_cast<double>(x) : 0.0;
}

// RBJ cookbook Butterworth sections; two in series make a Linkwitz-Riley 4th order.
KWeighting::Biquad butterworth(double sampleRate, double cutoffHz, bool highPass) {
    const double w0 = 2.0 * kPi * cutoffHz / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * 0.7071067811865476);
    const double a0 = 1.0 + alpha;
    const double b1 = highPass ? -(1.0 + cosw) : (1.0 - cosw);
    const double b0 = highPass ? (1.0 + cosw) / 2.0 : (1.0 - cosw) / 2.0;
    return {b0 / a0, b1 / a0, b0 / a0, -2.0 * cosw / a0, (1.0 - alpha) / a0};
}

struct LinkwitzRiley {
    Filter a, b;
    LinkwitzRiley(double sampleRate, double cutoffHz, bool highPass)
        : a{butterworth(sampleRate, cutoffHz, highPass)}, b{butterworth(sampleRate, cutoffHz, highPass)} {}
    double process(double x) { return b.process(a.process(x)); }
};

// Gated mean of block powers above @p threshold (LUFS), or 0 when none pass.
double gatedMeanPower(const std::vector<double>& powers, double thresholdLufs) {
    double sum = 0.0;
    size_t n = 0;
    for (double p : powers) {
        if (powerToLufs(p) > thresholdLufs) {
            sum += p;
            ++n;
        }
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

double percentile(std::vector<double> sorted, double p) {
    std::sort(sorted.begin(), sorted.end());
    const auto index = static_cast<size_t>(std::lround(p * static_cast<double>(sorted.size() - 1)));
    return sorted[index];
}

} // namespace

AudioAnalysis analyzeAudio(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate) {
    AudioAnalysis out;
    out.frames = frames;
    out.channels = channels;
    out.sampleRate = sampleRate;
    if (!interleaved || frames == 0 || channels == 0 || sampleRate == 0) {
        return out;
    }

    // --- Loudness: K-weighted energy per 100 ms hop, summed over weighted channels.
    const auto hop = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(sampleRate * 0.1)));
    std::vector<Filter> pre(channels, Filter{KWeighting::preFilter(sampleRate)});
    std::vector<Filter> rlb(channels, Filter{KWeighting::rlbHighPass(sampleRate)});
    std::vector<double> hopEnergy;
    hopEnergy.reserve(static_cast<size_t>(frames / hop) + 1);
    double hopSum = 0.0;

    // --- Peaks and correlation.
    double samplePeak = 0.0;
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

    // --- Bands, on the mid signal.
    std::vector<LinkwitzRiley> lowPass, highPass;
    size_t edgesInRange = 0; // edges above Nyquist leave their bands empty
    for (double edge : AudioAnalysis::kBandEdgesHz) {
        if (edge >= 0.49 * sampleRate) break;
        lowPass.emplace_back(sampleRate, edge, false);
        highPass.emplace_back(sampleRate, edge, true);
        ++edgesInRange;
    }
    std::array<double, AudioAnalysis::kBandCount> bandEnergy{};

    for (uint64_t f = 0; f < frames; ++f) {
        const float* frame = interleaved + f * channels;
        for (uint32_t c = 0; c < channels; ++c) {
            const double x = sanitize(frame[c]);
            samplePeak = std::max(samplePeak, std::abs(x));
            const double k = rlb[c].process(pre[c].process(x));
            hopSum += channelWeight(c, channels) * k * k;
        }
        if ((f + 1) % hop == 0) {
            hopEnergy.push_back(hopSum);
            hopSum = 0.0;
        }

        const double l = sanitize(frame[0]);
        const double r = channels > 1 ? sanitize(frame[1]) : l;
        sumLL += l * l;
        sumRR += r * r;
        sumLR += l * r;

        double rest = channels > 1 ? 0.5 * (l + r) : l;
        for (size_t e = 0; e < edgesInRange; ++e) {
            const double below = lowPass[e].process(rest);
            bandEnergy[e] += below * below;
            rest = highPass[e].process(rest);
        }
        bandEnergy[edgesInRange] += rest * rest;
    }

    // Momentary (4 hops) and short-term (30 hops) windows on the 100 ms hop.
    auto windowPowers = [&](size_t hops) {
        std::vector<double> powers;
        if (hopEnergy.size() < hops) return powers;
        double running = 0.0;
        for (size_t i = 0; i < hopEnergy.size(); ++i) {
            running += hopEnergy[i];
            if (i >= hops) running -= hopEnergy[i - hops];
            if (i + 1 >= hops) powers.push_back(std::max(0.0, running) / static_cast<double>(hops * hop));
        }
        return powers;
    };
    const std::vector<double> momentary = windowPowers(4);
    const std::vector<double> shortTerm = windowPowers(30);

    for (double p : momentary) out.maxMomentaryLufs = std::max(out.maxMomentaryLufs, powerToLufs(p));
    out.shortTermLufs.reserve(shortTerm.size());
    for (double p : shortTerm) {
        const double lufs = powerToLufs(p);
        out.maxShortTermLufs = std::max(out.maxShortTermLufs, lufs);
        out.shortTermLufs.push_back(static_cast<float>(lufs));
    }

    // Integrated: absolute gate, then relative gate 10 LU under the absolute-gated mean.
    const double absoluteMean = gatedMeanPower(momentary, kAbsoluteGateLufs);
    if (absoluteMean > 0.0) {
        const double relativeGate = powerToLufs(absoluteMean) + kRelativeGateLu;
        out.integratedLufs = powerToLufs(gatedMeanPower(momentary, std::max(kAbsoluteGateLufs, relativeGate)));
    }

    // Loudness range: short-term values past the absolute gate and 20 LU under their mean, P95 - P10.
    const double rangeMean = gatedMeanPower(shortTerm, kAbsoluteGateLufs);
    if (rangeMean > 0.0) {
        const double gate = std::max(kAbsoluteGateLufs, powerToLufs(rangeMean) + kRangeRelativeGateLu);
        std::vector<double> kept;
        for (double p : shortTerm) {
            const double lufs = powerToLufs(p);
            if (lufs > gate) kept.push_back(lufs);
        }
        if (kept.size() >= 2) out.loudnessRangeLu = percentile(kept, 0.95) - percentile(kept, 0.10);
    }

    // True peak: the same 4x oversampling meter the engine uses, one channel at a time.
    {
        std::vector<float> channel(static_cast<size_t>(frames));
        double truePeak = 0.0;
        for (uint32_t c = 0; c < channels; ++c) {
            for (uint64_t f = 0; f < frames; ++f) channel[f] = static_cast<float>(sanitize(interleaved[f * channels + c]));
            TruePeakMeter meter;
            meter.initialize(sampleRate);
            constexpr uint64_t kChunk = 1u << 20;
            for (uint64_t start = 0; start < frames; start += kChunk) {
                meter.processMono(channel.data() + start, static_cast<uint32_t>(std::min(kChunk, frames - start)));
            }
            truePeak = std::max(truePeak, static_cast<double>(meter.getMaxTruePeak()));
        }
        out.truePeakDbtp = toDb(std::max(truePeak, samplePeak));
    }
    out.samplePeakDbfs = toDb(samplePeak);

    if (channels == 1) {
        out.correlation = samplePeak > 0.0 ? 1.0 : 0.0;
    } else if (sumLL > 0.0 && sumRR > 0.0) {
        out.correlation = std::clamp(sumLR / std::sqrt(sumLL * sumRR), -1.0, 1.0);
    }

    double totalBandEnergy = 0.0;
    for (double e : bandEnergy) totalBandEnergy += e;
    for (size_t b = 0; b < AudioAnalysis::kBandCount; ++b) {
        out.bandRmsDbfs[b] = toDb(std::sqrt(bandEnergy[b] / static_cast<double>(frames)));
        out.bandShare[b] = totalBandEnergy > 0.0 ? bandEnergy[b] / totalBandEnergy : 0.0;
    }
    return out;
}

std::shared_ptr<const AudioAnalysis> AudioAnalysisCache::getOrCompute(const std::string& key, const Compute& compute) {
    if (auto hit = find(key)) return hit;
    auto computed = std::make_shared<const AudioAnalysis>(compute());
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = entries_.find(key); it != entries_.end()) return it->second; // another caller won the race
    while (entries_.size() >= capacity_ && !order_.empty()) {
        entries_.erase(order_.front());
        order_.erase(order_.begin());
    }
    entries_.emplace(key, computed);
    order_.push_back(key);
    return computed;
}

std::shared_ptr<const AudioAnalysis> AudioAnalysisCache::find(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : it->second;
}

void AudioAnalysisCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    order_.clear();
}

size_t AudioAnalysisCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::string AudioAnalysisCache::renderKey(uint64_t hash64, uint32_t sampleRate, uint32_t channels) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "render:%016llx:%u:%u", static_cast<unsigned long long>(hash64), sampleRate,
                  channels);
    return buf;
}

std::string AudioAnalysisCache::fileKey(const std::string& path, uint64_t sizeBytes, int64_t mtimeTicks) {
    return "file:" + path + "|" + std::to_string(sizeBytes) + "|" + std::to_string(mtimeTicks);
}

} // namespace Audio
} // namespace Aestra
