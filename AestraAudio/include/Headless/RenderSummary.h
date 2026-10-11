// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

struct AudioAnalysis;

/**
 * @brief What a rendered file sounds like, as numbers a script can check.
 *
 * Pure: computed from interleaved float samples, so it is the same whether the
 * samples came from an export, a test, or a file on disk. The point is that an
 * agent (or CI) can judge a render without listening to it -- the half of the
 * verification loop that otherwise needs a human.
 */
struct RenderSummary {
    uint64_t frames = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    double durationSeconds = 0.0;

    /// Sample peak and RMS over every channel, in dBFS. -200 for digital silence.
    double peakDbfs = -200.0;
    double rmsDbfs = -200.0;
    /// Mean of each channel. A large value is a DC offset somewhere upstream.
    std::vector<double> dcOffset;

    /// Samples at or past full scale (|x| >= 1). A float render can carry
    /// them; a fixed-point file would clip them.
    uint64_t clippedSamples = 0;
    /// NaN or infinity anywhere is a broken render, full stop.
    uint64_t nonFiniteSamples = 0;

    /// Time before the first and after the last sample above the silence
    /// threshold. A whole-file silence sets `silent` and both to the duration.
    double leadingSilenceSeconds = 0.0;
    double trailingSilenceSeconds = 0.0;
    bool silent = true;

    /// FNV-1a over the exact float bit patterns, in order. Two renders with the
    /// same hash are bit-identical; the render is deterministic only if the
    /// same project always gives the same hash.
    uint64_t hash64 = 0;
};

/// Summarise @p interleaved (frames * channels floats). @p silenceDbfs is the
/// level at or below which a sample counts as silence.
RenderSummary summarizeRender(const std::vector<float>& interleaved, uint32_t channels, uint32_t sampleRate,
                              double silenceDbfs = -90.0);

/**
 * @brief The parts of a summary a golden file pins.
 *
 * Frames must match exactly, and the hash too when the golden has one. Peak
 * and RMS within a tolerance, so a reviewer reading a mismatch sees HOW far the
 * render moved, not only that the hash changed.
 */
struct RenderExpectation {
    uint64_t frames = 0;
    /// Bit-exact identity. Only meaningful on the platform and compiler that
    /// wrote it -- float math differs across them -- so a golden checked on
    /// every platform omits it and pins the levels instead.
    bool hasHash = true;
    uint64_t hash64 = 0;
    double peakDbfs = 0.0;
    double rmsDbfs = 0.0;
    /// Loudness pins from the analysis service (V8-S8), within the same
    /// tolerance as the levels. A golden written before them has none, and
    /// is compared exactly as before.
    bool hasLoudness = false;
    double integratedLufs = 0.0;
    double truePeakDbtp = 0.0;
};

RenderExpectation expectationFrom(const RenderSummary& summary);
/// The same, plus the loudness pins.
RenderExpectation expectationFrom(const RenderSummary& summary, const AudioAnalysis& analysis);

/// Human-readable differences; empty means the render matches. Hash is compared
/// exactly; levels within @p toleranceDb.
std::vector<std::string> compareToExpectation(const RenderSummary& actual, const RenderExpectation& expected,
                                              double toleranceDb = 0.01);
/// The same, plus integrated loudness and true peak when the golden pins them.
std::vector<std::string> compareToExpectation(const RenderSummary& actual, const AudioAnalysis& analysis,
                                              const RenderExpectation& expected, double toleranceDb = 0.01);

/// 16 lower-case hex digits, the form a golden file stores (a JSON number
/// cannot hold 64 bits exactly).
std::string hashToHex(uint64_t hash);
bool hashFromHex(const std::string& hex, uint64_t& out);

} // namespace Audio
} // namespace Aestra
