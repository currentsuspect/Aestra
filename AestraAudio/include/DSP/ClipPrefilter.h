// © 2026 Aestra Studios — All Rights Reserved.
// ClipPrefilter — anti-alias low-pass for DOWNSAMPLED clip playback (Phase 4, F1).
//
// Architecture selected by measurement (Aestra-Internals: aestra-docs/audio-research-bench.md §9,
// "Option B"): a designed linear-phase Kaiser low-pass applied ONCE to the clip at
// its source rate, off the audio thread, feeding the existing interpolation kernels
// unchanged. Spec: passband edge 0.9x destination Nyquist, stopband edge at the
// destination Nyquist, 100 dB attenuation; odd length; output is compensated by the
// filter's integer group delay so clip timing does not move. Measured (lab, §9.3):
// alias rejection -101.5 to -139.1 dBc, passband exact to <0.0001 dB, DC exact.
//
// These are pure, deterministic, allocation-explicit functions. They must NEVER be
// called on the audio thread (ClipPrefilterService owns the worker that runs them).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace Aestra {
namespace Audio {
namespace ClipPrefilter {

/// Bump when the filter spec changes so stored filtered copies invalidate.
constexpr uint32_t kSpecVersion = 2; // 2: whole-number rate ratios are stored decimated (see decimationFactor)

/// A clip needs prefiltering only when it will be DOWNSAMPLED into the session.
inline bool isNeeded(uint32_t sourceRate, uint32_t targetRate) {
    return sourceRate > 0 && targetRate > 0 && sourceRate > targetRate;
}

/// Kaiser low-pass design for sourceRate -> targetRate (see spec above).
/// Returns odd-length coefficients normalized to unity DC gain.
std::vector<double> design(uint32_t sourceRate, uint32_t targetRate);

/// Apply `h` (odd length, from design()) to interleaved audio, compensating the
/// integer group delay (taps-1)/2: output frame i is the filtered signal at input
/// frame i. Same frame count; edges taper as the kernel support leaves the clip.
/// `out` may not alias `in`.
void apply(const float* in, float* out, uint64_t frames, uint32_t channels,
           const std::vector<double>& h);

/// The whole-number factor N when `sourceRate` is exactly N x `targetRate` (96k -> 48k is 2, 192k -> 48k is 4,
/// 88.2k -> 44.1k is 2), else 1. Only then is the filtered signal stored decimated: for a whole-number ratio
/// the playback kernel's resample of the filtered signal lands exactly on every Nth source frame (the
/// sinc is 1 at a whole-number offset and 0 at the others), so keeping just those frames is the same
/// audio at 1/N the memory, played with no resampling at all. Other ratios (88.2k -> 48k) keep the full-rate copy.
inline uint32_t decimationFactor(uint32_t sourceRate, uint32_t targetRate) {
    if (!isNeeded(sourceRate, targetRate) || sourceRate % targetRate != 0) {
        return 1;
    }
    return sourceRate / targetRate;
}

/// Copies `frames` frames (interleaved, `channels` wide) starting at `firstFrame` into `out`.
using ReadFrames = std::function<void(uint64_t firstFrame, uint64_t frames, float* out)>;

/// apply() without the whole input in memory, and optionally keeping every `decimation`-th output frame.
/// The input is read in blocks through `read` (a compact PCM source converts each block as it goes), so
/// the extra memory is one block plus the filter's reach, not a float copy of the clip.
/// `out` holds ceil(frames / decimation) frames; output frame m is exactly apply()'s frame m * decimation
/// (bit for bit; with decimation 1 it IS apply()), computed only where it is kept.
void applyStreaming(const ReadFrames& read, float* out, uint64_t frames, uint32_t channels,
                    const std::vector<double>& h, uint32_t decimation = 1);

} // namespace ClipPrefilter
} // namespace Audio
} // namespace Aestra
