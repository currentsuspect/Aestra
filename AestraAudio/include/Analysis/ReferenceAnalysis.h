// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Analysis/AudioAnalysis.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief The measurements Audition's reference tools show beyond AudioAnalysis (FD-26, V81-AU2).
 *
 * Pure functions of interleaved float samples, like analyzeAudio, so a file, a render and a test
 * signal are measured the same way and the results can be cached by the same content key.
 */

/// Peak-to-loudness ratio: true peak minus integrated loudness, in dB. 0 when either is unmeasurable.
double peakToLoudnessRatioDb(const AudioAnalysis& analysis);

/// Width and phase of a stereo signal over time and per band.
struct StereoDetail {
    static constexpr double kWindowSeconds = 1.0;
    /// Side energy as a share of mid + side energy per band, 0 (mono) … 1 (all side). Same bands as AudioAnalysis.
    std::array<double, AudioAnalysis::kBandCount> bandWidth{};
    /// L/R correlation of each window of kWindowSeconds, -1 … +1 (0 where a window is silent).
    std::vector<float> correlationOverTime;
    /// Window starts, in seconds, where correlation fell below kMonoProblemCorrelation: they cancel when summed to mono.
    std::vector<double> monoProblemStartsSec;
    static constexpr double kMonoProblemCorrelation = -0.2;
};
/// Needs two channels; anything else returns an empty detail (a mono signal has no width).
StereoDetail analyzeStereoDetail(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate);

/// One stretch of a song between two detected boundaries.
struct Section {
    double startSec = 0.0;
    double endSec = 0.0;
    double rmsDb = AudioAnalysis::kSilenceDb;   ///< RMS over the section, dBFS.
    double peakDb = AudioAnalysis::kSilenceDb;  ///< Sample peak over the section, dBFS.
    double crestDb = 0.0;                       ///< peak minus RMS.
};
/// Splits a signal where its level and spectral balance change: the map that lets A/B land in the matching section.
/// Always returns at least one section covering the whole signal (or none for an empty signal).
std::vector<Section> detectSections(const float* interleaved, uint64_t frames, uint32_t channels, uint32_t sampleRate);

/// Gain to apply to the mix so it matches the reference, dB (reference minus mix). 0 when either is unmeasurable.
double levelMatchGainDb(double mixLufs, double referenceLufs);
/// The same over a stretch of each signal (a section or a loop), by measuring just those frames.
double levelMatchGainDbForRange(const float* mix, uint64_t mixFrames, uint32_t mixChannels, uint32_t mixRate,
                                double mixStartSec, double mixEndSec, const float* reference, uint64_t referenceFrames,
                                uint32_t referenceChannels, uint32_t referenceRate, double referenceStartSec,
                                double referenceEndSec);

/// Band-by-band difference between a mix and a reference, dB (positive: the mix has more energy there).
std::array<double, AudioAnalysis::kBandCount> tonalBalanceDeltaDb(const AudioAnalysis& mix, const AudioAnalysis& reference);

} // namespace Audio
} // namespace Aestra
