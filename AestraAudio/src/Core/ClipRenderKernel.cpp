// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Core/ClipRenderKernel.h"

#include "Core/MixMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Aestra {
namespace Audio {
namespace ClipRenderKernel {

ClipRenderResult renderClipInto(const ClipRenderState& clip, uint64_t blockStart, uint64_t blockEnd,
                                double* destination, uint32_t engineSampleRate,
                                Interpolators::InterpolationQuality quality) {
    // Verbatim relocation of the per-clip body of renderClips(). The only
    // changes are mechanical: the loop-carried telemetry flag became this
    // result, and each clip-level `continue` became a return of it. Transport
    // and pattern-mode guards stay at the call site; this never sees them.
    ClipRenderResult result{};

        // Preconditions the call site used to guarantee.
        //
        // AudioEngine::renderGraph() returns early when the engine sample rate
        // is zero, so this body never saw one. As a free function that the
        // consolidation renderer also calls, it has to say so itself: with a
        // zero output rate and a clip whose own rate is unset, ratio becomes
        // 0/0 = NaN, the `phase >= totalFrames` guard is false for NaN, and
        // the frame-count clamp then casts NaN to uint32_t — undefined.
        //
        // A zero-length source is the same class: both existing bounds checks
        // are gated on totalFrames > 0, so it would fall through to the
        // direct-rate branch and read past the buffer.
        if (engineSampleRate == 0 || clip.totalFrames == 0) {
            return result;
        }

        if (!clip.audioData || blockEnd <= clip.startSample || blockStart >= clip.endSample) {
            return result;
        }

        const uint64_t start = std::max(blockStart, clip.startSample);
        const uint64_t end = std::min(blockEnd, clip.endSample);
        const uint32_t localOffset = static_cast<uint32_t>(start - blockStart);
        uint32_t framesToRender = static_cast<uint32_t>(end - start);

        // Sample rate ratio
        const double outputRate = static_cast<double>(engineSampleRate);
        const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
        const double playbackRate =
            std::isfinite(clip.playbackRate) ? MixMath::clampD(static_cast<double>(clip.playbackRate), 0.25, 4.0) : 1.0;
        const double ratio = (srcRate / outputRate) * playbackRate;

        // Source position
        const double outputFrameOffset = static_cast<double>(start - clip.startSample);
        double phase = clip.sampleOffset + outputFrameOffset * ratio;

        // Bounds
        const int64_t totalFrames = static_cast<int64_t>(clip.totalFrames);
        const uint64_t totalFrameCount = static_cast<uint64_t>(totalFrames);
        if (totalFrames > 0 && phase >= static_cast<double>(totalFrames)) {
            return result;
        }
        if (totalFrames > 0) {
            const double remaining = static_cast<double>(totalFrames) - phase;
            const uint32_t maxFrames = static_cast<uint32_t>(remaining / ratio);
            framesToRender = std::min(framesToRender, maxFrames);
        }
        if (framesToRender == 0)
            return result;

        const uint32_t channels = clip.channels;
        const uint32_t stride = channels;

        double* dstBase = destination;
        const float* data = clip.audioData;
        double* dst = dstBase + static_cast<size_t>(localOffset) * 2;

        const uint64_t clipLength = clip.endSample - clip.startSample;
        const uint64_t fadeInLen =
            std::min(clipLength, std::max<uint64_t>(kClipEdgeFadeSamples, clip.fadeInSamples));
        const uint64_t fadeOutLen =
            std::min(clipLength, std::max<uint64_t>(kClipEdgeFadeSamples, clip.fadeOutSamples));
        const auto clipFadeAt = [&clip, fadeInLen, fadeOutLen](uint64_t projectSample) {
            double fade = 1.0;
            if (fadeInLen > 0 && projectSample < clip.startSample + fadeInLen) {
                fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) /
                                          static_cast<double>(fadeInLen));
            }
            if (fadeOutLen > 0 && projectSample + fadeOutLen > clip.endSample) {
                fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) /
                                          static_cast<double>(fadeOutLen));
            }
            return fade;
        };
        // Clip pan is an instance-level stereo balance before the insert.
        // Centre must remain unity so opening the editor cannot change old
        // projects or stack a second -3 dB centre law with the insert pan.
        const double clipPan = MixMath::clampD(static_cast<double>(clip.pan), -1.0, 1.0);
        const double clipGain = static_cast<double>(clip.gain);
        const double clipGainL = clipGain * (clipPan > 0.0 ? 1.0 - clipPan : 1.0);
        const double clipGainR = clipGain * (clipPan < 0.0 ? 1.0 + clipPan : 1.0);

        // Fast path: matching sample rates - direct copy to double
        if (std::abs(ratio - 1.0) < 1e-9) {
            const uint64_t srcStart = static_cast<uint64_t>(phase);
            const float* src = data + srcStart * stride;
            for (uint32_t i = 0; i < framesToRender; ++i) {
                // Micro-fade at clip edges to avoid clicks/crackles.
                const uint64_t projectSample = start + i;
                const double fade = clipFadeAt(projectSample);

                // Sanitize clip source reads: a corrupted audio file can
                // contain NaN/Inf that would otherwise poison the mix.
                double sL, sR;
                if (channels == 1) {
                    sL = MixMath::sanitizeMix(static_cast<double>(src[i]));
                    sR = sL;
                } else {
                    sL = MixMath::sanitizeMix(static_cast<double>(src[i * 2]));
                    sR = MixMath::sanitizeMix(static_cast<double>(src[i * 2 + 1]));
                }

                dst[i * 2] += sL * clipGainL * fade;
                dst[i * 2 + 1] += sR * clipGainR * fade;
            }
        } else {
            result.usedSampleRateConversion = true;
            // Resampling - use selected quality, pre-compute end condition
            const double phaseEnd = static_cast<double>(totalFrames);

            if (channels == 1) {
                // Mono Resampling — use the same quality interpolators as stereo.
                // The mono path reads a single float per frame, so we duplicate to L/R.
                switch (quality) {
                case Interpolators::InterpolationQuality::Cubic:
                    for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                        const uint64_t projectSample = start + i;
                        const double fade = clipFadeAt(projectSample);
                        float sample = 0.0f;
                        uint64_t idx = static_cast<uint64_t>(phase);
                        double frac = phase - static_cast<double>(idx);
                        // Catmull-Rom 4-point on mono data
                        float s0 = (idx > 0) ? data[idx - 1] : data[idx];
                        float s1 = data[idx];
                        float s2 = (idx + 1 < totalFrameCount) ? data[idx + 1] : data[idx];
                        float s3 = (idx + 2 < totalFrameCount) ? data[idx + 2] : s2;
                        double f = frac;
                        sample = static_cast<float>(0.5 * ((2.0 * s1) + (-s0 + s2) * f +
                                                           (2.0 * s0 - 5.0 * s1 + 4.0 * s2 - s3) * f * f +
                                                           (-s0 + 3.0 * s1 - 3.0 * s2 + s3) * f * f * f));
                        dst[i * 2] += sample * clipGainL * fade;
                        dst[i * 2 + 1] += sample * clipGainR * fade;
                        phase += ratio;
                    }
                    return result;
                case Interpolators::InterpolationQuality::Sinc8:
                case Interpolators::InterpolationQuality::Sinc16:
                case Interpolators::InterpolationQuality::Sinc32:
                case Interpolators::InterpolationQuality::Sinc64:
                    // Sinc on mono: compute weighted sum, duplicate to L/R
                    for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                        const uint64_t projectSample = start + i;
                        const double fade = clipFadeAt(projectSample);
                        double val =
                            Interpolators::sincInterpolateMono(data, totalFrames, phase, quality);
                        dst[i * 2] += val * clipGainL * fade;
                        dst[i * 2 + 1] += val * clipGainR * fade;
                        phase += ratio;
                    }
                    return result;
                default:
                    // Linear fallback
                    for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                        const uint64_t projectSample = start + i;
                        const double fade = clipFadeAt(projectSample);
                        uint64_t idx = static_cast<uint64_t>(phase);
                        double frac = phase - static_cast<double>(idx);
                        float s0 = data[idx];
                        float s1 = (idx + 1 < static_cast<uint64_t>(totalFrames)) ? data[idx + 1] : s0;
                        double val = s0 + frac * (s1 - s0);
                        dst[i * 2] += val * clipGainL * fade;
                        dst[i * 2 + 1] += val * clipGainR * fade;
                        phase += ratio;
                    }
                    return result;
                }
            }

            // Select interpolator at block level, not per-sample
            // Block snapshot, matching mono above and every other cached
            // atomic renderGraph() takes once per block (slot map, params,
            // meter snapshots, pattern mode, and this same quality value at
            // its line 2168 read). Loading it again here let stereo observe a
            // different quality from mono inside one block, and potentially a
            // different one per clip — incoherent with the snapshot the
            // context already carries, and unreachable once this body becomes
            // a free kernel that has no engine to load from.
            switch (quality) {
            case Interpolators::InterpolationQuality::Cubic:
                for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                    float outL, outR;
                    Interpolators::CubicInterpolator::interpolate(data, totalFrames, phase, outL, outR);
                    const uint64_t projectSample = start + i;
                    const double fade = clipFadeAt(projectSample);
                    dst[i * 2] += static_cast<double>(outL) * clipGainL * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clipGainR * fade;
                    phase += ratio;
                }
                break;
            case Interpolators::InterpolationQuality::Sinc8:
                for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                    float outL, outR;
                    Interpolators::Sinc8Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                    const uint64_t projectSample = start + i;
                    const double fade = clipFadeAt(projectSample);
                    dst[i * 2] += static_cast<double>(outL) * clipGainL * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clipGainR * fade;
                    phase += ratio;
                }
                break;
            case Interpolators::InterpolationQuality::Sinc16:
                for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                    float outL, outR;
                    Interpolators::Sinc16Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                    const uint64_t projectSample = start + i;
                    const double fade = clipFadeAt(projectSample);
                    dst[i * 2] += static_cast<double>(outL) * clipGainL * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clipGainR * fade;
                    phase += ratio;
                }
                break;
            case Interpolators::InterpolationQuality::Sinc32:
                for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                    float outL, outR;
                    Interpolators::Sinc32Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                    const uint64_t projectSample = start + i;
                    const double fade = clipFadeAt(projectSample);
                    dst[i * 2] += static_cast<double>(outL) * clipGainL * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clipGainR * fade;
                    phase += ratio;
                }
                break;
            case Interpolators::InterpolationQuality::Sinc64:
                for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                    float outL, outR;
                    Interpolators::Sinc64Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                    const uint64_t projectSample = start + i;
                    const double fade = clipFadeAt(projectSample);
                    dst[i * 2] += static_cast<double>(outL) * clipGainL * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clipGainR * fade;
                    phase += ratio;
                }
                break;
            }
        }
    return result;
}

} // namespace ClipRenderKernel
} // namespace Audio
} // namespace Aestra
