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
#include <vector>

namespace Aestra {
namespace Audio {
namespace ClipPrefilter {

/// Bump when the filter spec changes so stored filtered copies invalidate.
constexpr uint32_t kSpecVersion = 1;

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

} // namespace ClipPrefilter
} // namespace Audio
} // namespace Aestra
