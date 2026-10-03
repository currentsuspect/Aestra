// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Headless/RenderSummary.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Aestra {
namespace Audio {

namespace {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

double toDb(double linear) {
    return linear > 0.0 ? 20.0 * std::log10(linear) : -200.0;
}

} // namespace

RenderSummary summarizeRender(const std::vector<float>& interleaved, uint32_t channels, uint32_t sampleRate,
                              double silenceDbfs) {
    RenderSummary s;
    s.channels = channels;
    s.sampleRate = sampleRate;
    s.hash64 = kFnvOffset;
    if (channels == 0) {
        return s;
    }
    s.frames = interleaved.size() / channels;
    s.durationSeconds = sampleRate > 0 ? static_cast<double>(s.frames) / sampleRate : 0.0;
    s.dcOffset.assign(channels, 0.0);

    const double silenceLinear = std::pow(10.0, silenceDbfs / 20.0);
    double peak = 0.0;
    long double sumSquares = 0.0L;
    uint64_t finiteCount = 0;
    std::vector<long double> channelSums(channels, 0.0L);
    bool any = false;
    uint64_t firstLoud = 0;
    uint64_t lastLoud = 0;

    for (uint64_t f = 0; f < s.frames; ++f) {
        bool loud = false;
        for (uint32_t c = 0; c < channels; ++c) {
            const float x = interleaved[static_cast<size_t>(f) * channels + c];
            uint32_t bits = 0;
            std::memcpy(&bits, &x, sizeof(bits));
            for (int b = 0; b < 4; ++b) {
                s.hash64 ^= (bits >> (8 * b)) & 0xFFu;
                s.hash64 *= kFnvPrime;
            }
            if (!std::isfinite(x)) {
                ++s.nonFiniteSamples;
                continue;
            }
            const double a = std::fabs(static_cast<double>(x));
            peak = std::max(peak, a);
            sumSquares += static_cast<long double>(x) * x;
            channelSums[c] += x;
            ++finiteCount;
            if (a >= 1.0) {
                ++s.clippedSamples;
            }
            if (a > silenceLinear) {
                loud = true;
            }
        }
        if (loud) {
            if (!any) {
                firstLoud = f;
                any = true;
            }
            lastLoud = f;
        }
    }

    s.peakDbfs = toDb(peak);
    s.rmsDbfs = finiteCount > 0 ? toDb(std::sqrt(static_cast<double>(sumSquares / finiteCount))) : -200.0;
    for (uint32_t c = 0; c < channels; ++c) {
        s.dcOffset[c] = s.frames > 0 ? static_cast<double>(channelSums[c] / static_cast<long double>(s.frames)) : 0.0;
    }
    s.silent = !any;
    if (any && sampleRate > 0) {
        s.leadingSilenceSeconds = static_cast<double>(firstLoud) / sampleRate;
        s.trailingSilenceSeconds = static_cast<double>(s.frames - 1 - lastLoud) / sampleRate;
    } else {
        s.leadingSilenceSeconds = s.durationSeconds;
        s.trailingSilenceSeconds = s.durationSeconds;
    }
    return s;
}

RenderExpectation expectationFrom(const RenderSummary& summary) {
    RenderExpectation e;
    e.frames = summary.frames;
    e.hash64 = summary.hash64;
    e.peakDbfs = summary.peakDbfs;
    e.rmsDbfs = summary.rmsDbfs;
    return e;
}

std::vector<std::string> compareToExpectation(const RenderSummary& actual, const RenderExpectation& expected,
                                              double toleranceDb) {
    std::vector<std::string> diffs;
    char buf[160];
    if (actual.frames != expected.frames) {
        std::snprintf(buf, sizeof(buf), "frames: %llu, expected %llu",
                      static_cast<unsigned long long>(actual.frames), static_cast<unsigned long long>(expected.frames));
        diffs.emplace_back(buf);
    }
    if (std::fabs(actual.peakDbfs - expected.peakDbfs) > toleranceDb) {
        std::snprintf(buf, sizeof(buf), "peak: %.3f dBFS, expected %.3f (moved %+.3f dB)", actual.peakDbfs,
                      expected.peakDbfs, actual.peakDbfs - expected.peakDbfs);
        diffs.emplace_back(buf);
    }
    if (std::fabs(actual.rmsDbfs - expected.rmsDbfs) > toleranceDb) {
        std::snprintf(buf, sizeof(buf), "rms: %.3f dBFS, expected %.3f (moved %+.3f dB)", actual.rmsDbfs,
                      expected.rmsDbfs, actual.rmsDbfs - expected.rmsDbfs);
        diffs.emplace_back(buf);
    }
    if (expected.hasHash && actual.hash64 != expected.hash64) {
        diffs.emplace_back("hash: " + hashToHex(actual.hash64) + ", expected " + hashToHex(expected.hash64) +
                           " (not bit-identical)");
    }
    return diffs;
}

std::string hashToHex(uint64_t hash) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

bool hashFromHex(const std::string& hex, uint64_t& out) {
    if (hex.size() != 16) {
        return false;
    }
    uint64_t v = 0;
    for (const char ch : hex) {
        v <<= 4;
        if (ch >= '0' && ch <= '9') {
            v |= static_cast<uint64_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            v |= static_cast<uint64_t>(ch - 'a' + 10);
        } else {
            return false;
        }
    }
    out = v;
    return true;
}

} // namespace Audio
} // namespace Aestra
