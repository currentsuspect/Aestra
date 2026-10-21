// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// TrackMixKernel — the strip's fader, pan, mix and meter stage.
//
// The strip's samples are interleaved L/R doubles, so one 2-wide vector
// carries both channels through the gain, the store and the mix. The peak
// tracking becomes one vector maximum instead of two scalar branches.
//
// SSE2 is part of the x86-64 baseline ABI: no compiler flag, no runtime
// dispatch, and it is available on the 4 GB Folio target as-is. A scalar
// fallback covers every other architecture.
//
// The output is BIT-IDENTICAL to the scalar loop this replaced:
//
//   * the gain application computes the same products in the same order,
//   * the mix into the destination adds the same values in the same order,
//   * the magnitude clears the sign bit, so -0.0 becomes +0.0 -- which never
//     wins a strict `>` against the 0.0 the peak starts at, exactly as the
//     old ternary's -0.0 did not,
//   * the maximum is taken with the NEW magnitude first, so a NaN leaves the
//     running peak alone exactly as `if (mag > peak)` did.

#include "Core/AudioGraphState.h"

#include <cmath>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define AESTRA_TRACKMIX_HAS_SSE2 1
#include <emmintrin.h>
#endif

namespace Aestra {
namespace Audio {

/**
 * @brief The running meter figures for one track strip, accumulated over a block.
 */
struct TrackMeterAccumulators {
    double rmsL{0.0};
    double rmsR{0.0};
    double lowL{0.0};
    double lowR{0.0};
    double correlation{0.0};
};

/**
 * @brief Apply the smoothed fader/pan gains, write the strip's output back,
 *        mix it into its destination, and update the track's meter figures.
 *
 * @param trackData    The strip's pre-fader samples, interleaved L/R.
 * @param buffer       The strip's buffer; the post-gain samples are written here.
 * @param destBuffer   Where the post-fader output goes, or nullptr to drop it.
 * @param numFrames    Frames to process.
 * @param gainL        The left channel's smoothed gain ramp; advanced in place.
 * @param gainR        The right channel's smoothed gain ramp; advanced in place.
 * @param meters       The track's meter accumulators; updated in place.
 * @param lfStateL     Low-band meter filter state, left channel.
 * @param lfStateR     Low-band meter filter state, right channel.
 * @param lfCoeff      Low-band meter filter coefficient.
 * @param publishMeter Whether the meter figures are wanted at all.
 * @param outPeakL     Receives the block's peak magnitude, left channel.
 * @param outPeakR     Receives the block's peak magnitude, right channel.
 */
inline void mixTrackStereo(const double* trackData,
                           double* buffer,
                           double* destBuffer,
                           uint32_t numFrames,
                           SmoothedParamD& gainL,
                           SmoothedParamD& gainR,
                           TrackMeterAccumulators& meters,
                           double* lfStateL,
                           double* lfStateR,
                           double lfCoeff,
                           bool publishMeter,
                           double& outPeakL,
                           double& outPeakR) noexcept {
#if defined(AESTRA_TRACKMIX_HAS_SSE2)
    __m128d peakVec = _mm_setzero_pd();
    const __m128d signMask = _mm_set1_pd(-0.0);
    for (uint32_t i = 0; i < numFrames; ++i) {
        const double leftGain = gainL.next();
        const double rightGain = gainR.next();

        const __m128d gain = _mm_set_pd(rightGain, leftGain);
        const __m128d out = _mm_mul_pd(_mm_loadu_pd(&trackData[i * 2]), gain);
        _mm_storeu_pd(&buffer[i * 2], out);

        if (destBuffer) {
            _mm_storeu_pd(&destBuffer[i * 2],
                           _mm_add_pd(_mm_loadu_pd(&destBuffer[i * 2]), out));
        }

        const __m128d mag = _mm_andnot_pd(signMask, out);
        peakVec = _mm_max_pd(mag, peakVec);

        if (publishMeter) {
            const double outL = _mm_cvtsd_f64(out);
            const double outR = _mm_cvtsd_f64(_mm_unpackhi_pd(out, out));

            meters.rmsL += outL * outL;
            meters.rmsR += outR * outR;

            const double lpL = *lfStateL + lfCoeff * (outL - *lfStateL);
            const double lpR = *lfStateR + lfCoeff * (outR - *lfStateR);
            *lfStateL = lpL;
            *lfStateR = lpR;
            meters.lowL += lpL * lpL;
            meters.lowR += lpR * lpR;
            meters.correlation += outL * outR;
        }
    }
    outPeakL = _mm_cvtsd_f64(peakVec);
    outPeakR = _mm_cvtsd_f64(_mm_unpackhi_pd(peakVec, peakVec));
#else
    double peakL = 0.0;
    double peakR = 0.0;
    for (uint32_t i = 0; i < numFrames; ++i) {
        const double leftGain = gainL.next();
        const double rightGain = gainR.next();

        const double outL = trackData[i * 2] * leftGain;
        const double outR = trackData[i * 2 + 1] * rightGain;

        buffer[i * 2] = outL;
        buffer[i * 2 + 1] = outR;

        if (destBuffer) {
            destBuffer[i * 2] += outL;
            destBuffer[i * 2 + 1] += outR;
        }

        const double absL = std::fabs(outL);
        const double absR = std::fabs(outR);
        if (absL > peakL)
            peakL = absL;
        if (absR > peakR)
            peakR = absR;

        if (publishMeter) {
            meters.rmsL += outL * outL;
            meters.rmsR += outR * outR;

            const double lpL = *lfStateL + lfCoeff * (outL - *lfStateL);
            const double lpR = *lfStateR + lfCoeff * (outR - *lfStateR);
            *lfStateL = lpL;
            *lfStateR = lpR;
            meters.lowL += lpL * lpL;
            meters.lowR += lpR * lpR;
            meters.correlation += outL * outR;
        }
    }
    outPeakL = peakL;
    outPeakR = peakR;
#endif
}

} // namespace Audio
} // namespace Aestra
