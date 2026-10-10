// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief What a piece of audio measures as: loudness, peaks, stereo image, spectrum balance.
 *
 * The analysis service of V8-S8 (FD-29). Pure: computed from interleaved float
 * samples, so a file, a render and a test signal are measured the same way.
 * Consumers (Audition's reference tools, the mixer master readout, headless-check
 * goldens) read these numbers instead of each measuring for itself.
 *
 * Loudness follows ITU-R BS.1770-4 / EBU R128: K-weighting (DSP/KWeighting.h),
 * 400 ms momentary and 3 s short-term windows on a 100 ms hop, integrated with
 * the -70 LUFS absolute and -10 LU relative gates, loudness range per EBU Tech
 * 3342. Channel weights follow BS.1770 for 1, 2 and 5.1 (L R C LFE Ls Rs: LFE
 * excluded, surrounds +1.5 dB); any other layout weighs every channel 1.
 * A mono file is measured as one channel, as BS.1770 says, not as dual mono.
 *
 * Levels with nothing to measure (silence, or audio shorter than the window)
 * read kSilenceDb.
 */
struct AudioAnalysis {
    static constexpr double kSilenceDb = -200.0;

    uint64_t frames = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;

    /// Gated integrated loudness over the whole signal, LUFS.
    double integratedLufs = kSilenceDb;
    /// Loudest 400 ms and 3 s windows, LUFS.
    double maxMomentaryLufs = kSilenceDb;
    double maxShortTermLufs = kSilenceDb;
    /// EBU Tech 3342 loudness range, LU. 0 when too little passes the gates.
    double loudnessRangeLu = 0.0;
    /// Short-term loudness every 100 ms (the first value covers the first 3 s),
    /// for a loudness-over-time graph. kSilenceDb where nothing is measurable.
    std::vector<float> shortTermLufs;

    /// Highest sample and highest reconstructed (4x oversampled) peak, any channel.
    double samplePeakDbfs = kSilenceDb;
    double truePeakDbtp = kSilenceDb;

    /// L/R correlation over the whole signal, -1 … +1 (sum LR / sqrt(sum L² · sum R²)).
    /// 1 for mono with signal (one channel is perfectly correlated with itself);
    /// 0 when either side is silent.
    double correlation = 0.0;

    /// Energy per band of the mid signal ((L+R)/2, or the one channel): RMS in
    /// dBFS and the share of the total. Bands split at kBandEdgesHz with
    /// Linkwitz-Riley crossovers; shares are of the bands' summed energy, so they
    /// sum to 1. A band above Nyquist stays silent.
    enum Band : size_t { Low, LowMid, Mid, HighMid, High, kBandCount };
    static constexpr std::array<double, kBandCount - 1> kBandEdgesHz{120.0, 500.0, 2000.0, 6000.0};
    std::array<double, kBandCount> bandRmsDbfs{kSilenceDb, kSilenceDb, kSilenceDb, kSilenceDb, kSilenceDb};
    std::array<double, kBandCount> bandShare{};
};

/// Measure @p interleaved (frames × channels floats) at @p sampleRate.
AudioAnalysis analyzeAudio(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate);

inline AudioAnalysis analyzeAudio(const std::vector<float>& interleaved, uint32_t channels, uint32_t sampleRate) {
    return analyzeAudio(interleaved.data(), channels ? interleaved.size() / channels : 0, channels, sampleRate);
}

/**
 * @brief Each analysis computed once: results kept by content key.
 *
 * The key names the audio, not where it lives: a render's RenderSummary hash
 * (plus rate and channel count), or a file's path with its size and mtime, so
 * an edited file is measured again. Thread-safe; the analysis itself runs
 * outside the lock, and two callers racing on one key both get a valid result.
 */
class AudioAnalysisCache {
public:
    using Compute = std::function<AudioAnalysis()>;

    explicit AudioAnalysisCache(size_t capacity = 256) : capacity_(capacity ? capacity : 1) {}

    /// The cached analysis for @p key, or the result of @p compute, stored.
    std::shared_ptr<const AudioAnalysis> getOrCompute(const std::string& key, const Compute& compute);
    /// The cached analysis for @p key, or null.
    std::shared_ptr<const AudioAnalysis> find(const std::string& key) const;

    void clear();
    size_t size() const;

    /// Key for a render: its bit-exact hash, rate and channels.
    static std::string renderKey(uint64_t hash64, uint32_t sampleRate, uint32_t channels);
    /// Key for a file: path, byte size and modification time.
    static std::string fileKey(const std::string& path, uint64_t sizeBytes, int64_t mtimeTicks);

private:
    size_t capacity_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<const AudioAnalysis>> entries_;
    std::vector<std::string> order_; // insertion order; the oldest goes first when full
};

} // namespace Audio
} // namespace Aestra
