// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioRenderer.h"

#include "../../AestraCore/include/AestraMath.h"
#include "ArsenalProcessingContext.h"
#include "AudioEngine.h"
#include "Core/MixMath.h"
#include "DSP/PanLaw.h"
#include "EffectChain.h"
#include "Interpolators.h"
#include "PatternPlaybackEngine.h"
#include "Plugin/SamplerPlugin.h"
#include "PluginHost.h"
#include "UnitManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace Aestra {
namespace Audio {

namespace {
bool processPluginNoexcept(IPluginInstance& plugin, const float* const* inputs, float** outputs,
                           uint32_t numInputChannels, uint32_t numOutputChannels, uint32_t numFrames,
                           const MidiBuffer* midiInput, MidiBuffer* midiOutput) noexcept {
    try {
        plugin.process(inputs, outputs, numInputChannels, numOutputChannels, numFrames, midiInput, midiOutput);
        return true;
    } catch (...) {
        return false;
    }
}

void sanitizeFloatBuffers(float** buffer, uint32_t numChannels, uint32_t numFrames) noexcept {
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        float* channel = buffer[ch];
        if (!channel) {
            continue;
        }
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (!std::isfinite(channel[i])) {
                channel[i] = 0.0f;
            }
        }
    }
}

static constexpr double PI_D = 3.14159265358979323846;
static constexpr double QUARTER_PI_D = PI_D * 0.25;

inline double dbToLinearD(double db) {
    if (db <= -90.0)
        return 0.0;
    return static_cast<double>(Aestra::dbToGain(static_cast<float>(db)));
}

inline double clampD(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
    PanLaw::equalPower(pan, vol, gainL, gainR);
}

inline void fastStereoBalanceGainsD(double pan, double vol, double& gainL, double& gainR) {
    PanLaw::stereoBalance(pan, vol, gainL, gainR);
}
} // namespace

AudioRenderer::AudioRenderer() {}
AudioRenderer::~AudioRenderer() {}

void AudioRenderer::renderBlock(const Context& ctx, AudioGraphState& state, AudioEngine& engineRef) {
    // Get meter snapshot buffer for track metering
    auto* snaps = engineRef.m_meterSnapshotsRaw.load(std::memory_order_relaxed);
    auto* slotMap = engineRef.m_channelSlotMapRaw.load(std::memory_order_relaxed);

    // Clear every track once before routing begins. Clearing inside
    // renderClipAudio would erase audio already accumulated from an upstream
    // track when a destination is processed later in topological order.
    for (const auto& track : state.renderTracks) {
        if (track.selfBuffer) {
            std::memset(track.selfBuffer + ctx.bufferOffset * 2, 0, ctx.numFrames * 2 * sizeof(double));
        }
    }

    // Iterate through topologically sorted render tracks
    for (const auto& track : state.renderTracks) {
        if (track.trackIndex >= state.trackStates.size())
            continue;

        TrackRTState& trackState = state.trackStates[track.trackIndex];

        // When isolating a track, only that track renders clips and units.
        // Every track still applies its strip and routes its connections so
        // content that arrived via sends or bus routing reaches master (#761).
        const bool rendersClips =
            ctx.isolatedTrackIndex < 0 || track.trackIndex == (uint32_t)ctx.isolatedTrackIndex;

        if (rendersClips) {
            // 1. Render Clips (Generates Audio) -> track.selfBuffer
            renderClipAudio(track.selfBuffer, trackState, track.trackIndex, ctx, engineRef);

            // 1.5 Render units assigned to this track's stable mixer identity.
            if (track.trackIndex < ctx.graph->tracks.size()) {
                renderArsenalUnitsForTrack(ctx.graph->tracks[track.trackIndex].trackId, track.selfBuffer, ctx,
                                           engineRef);
            }
        }

        // 2. Process Effects (In-Place) -> track.selfBuffer
        processTrackEffects(track, state, ctx.numFrames, ctx.bufferOffset, engineRef, *ctx.graph, ctx.isOffline);

        // 3. Calculate Track Meter Peaks (post-fader)
        if (rendersClips && !ctx.isOffline && track.selfBuffer && snaps && slotMap &&
            track.trackIndex < ctx.graph->tracks.size()) {
            const auto& graphTrack = ctx.graph->tracks[track.trackIndex];
            const uint32_t slotIdx = slotMap->getSlotIndex(graphTrack.trackId);

            if (slotIdx != ChannelSlotMap::INVALID_SLOT && graphTrack.trackId != 0) {
                double* src = track.selfBuffer + ctx.bufferOffset * 2;
                double peakL = 0.0, peakR = 0.0;
                double rmsAccL = 0.0, rmsAccR = 0.0;

                double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;

                for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                    const double L = src[i * 2], R = src[i * 2 + 1];
                    const double absL = std::abs(L), absR = std::abs(R);
                    if (absL > peakL)
                        peakL = absL;
                    if (absR > peakR)
                        peakR = absR;
                    rmsAccL += L * L;
                    rmsAccR += R * R;
                    sumLR += L * R;
                    sumLL += L * L;
                    sumRR += R * R;
                }

                const double invN = 1.0 / static_cast<double>(ctx.numFrames);
                const float rmsL = static_cast<float>(std::sqrt(rmsAccL * invN));
                const float rmsR = static_cast<float>(std::sqrt(rmsAccR * invN));

                float correlation = 0.0f;
                const double den = std::sqrt(sumLL * sumRR);
                if (den > 1e-9)
                    correlation = static_cast<float>(sumLR / den);

                snaps->writeLevels(slotIdx, (float)peakL, (float)peakR, rmsL, rmsR, 0.0f, 0.0f, correlation, -144.0f);
            }
        }

        // 4. Route to Destinations (Mixing)
        if (track.selfBuffer) {
            double* src = track.selfBuffer + ctx.bufferOffset * 2;
            for (const auto& conn : track.activeConnections) {
                if (conn.destinationBufferL && conn.destinationBufferR) {
                    double* dstL = conn.destinationBufferL + ctx.bufferOffset * conn.stride;
                    double* dstR = conn.destinationBufferR + ctx.bufferOffset * conn.stride;
                    for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                        dstL[i * conn.stride] += src[i * 2] * conn.gainL;
                        dstR[i * conn.stride] += src[i * 2 + 1] * conn.gainR;
                    }
                }
            }
        }
    }
}

void AudioRenderer::renderClipAudio(double* outputBuffer, TrackRTState& state, uint32_t trackIndex, const Context& ctx,
                                    AudioEngine& engineRef) {
    uint32_t numFrames = ctx.numFrames;
    uint32_t bufferOffset = ctx.bufferOffset;
    if (state.mute)
        return;

    const auto& graph = *ctx.graph;
    if (trackIndex >= graph.tracks.size())
        return;
    const auto& clips = graph.tracks[trackIndex].clips;
    if (clips.empty())
        return;

    const uint64_t blockStart = ctx.globalPos;
    const uint64_t blockEnd = blockStart + numFrames;
    const double outputRate = static_cast<double>(ctx.sampleRate);
    if (outputRate <= 0.0) {
        return;
    }
    double* dstBase = outputBuffer + bufferOffset * 2;

    // Cache loop-invariant atomic before clip loop — avoids redundant loads per clip.
    const auto cachedInterpQuality = engineRef.getInterpolationQuality();

    for (const auto& clip : clips) {
        if (!clip.audioData || blockEnd <= clip.startSample || blockStart >= clip.endSample)
            continue;
        const uint64_t start = std::max(blockStart, clip.startSample);
        const uint64_t end = std::min(blockEnd, clip.endSample);
        const uint32_t localOffset = static_cast<uint32_t>(start - blockStart);
        uint32_t framesToRender = static_cast<uint32_t>(end - start);
        const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
        // Mirrors ClipRenderKernel::renderClip: clip Speed (playbackRate) scales
        // the resample ratio. Without this, isolated-track bounces ignored speed
        // while live playback and full-mix export honored it (#745).
        const double playbackRate =
            std::isfinite(clip.playbackRate) ? MixMath::clampD(static_cast<double>(clip.playbackRate), 0.25, 4.0)
                                             : 1.0;
        const double ratio = (srcRate / outputRate) * playbackRate;
        double phase = clip.sampleOffset + static_cast<double>(start - clip.startSample) * ratio;

        if (clip.totalFrames > 0) {
            if (phase >= static_cast<double>(clip.totalFrames))
                continue;
            framesToRender = std::min(framesToRender,
                                      static_cast<uint32_t>((static_cast<double>(clip.totalFrames) - phase) / ratio));
        }
        if (framesToRender == 0)
            continue;

        double* dst = dstBase + localOffset * 2;
        if (std::abs(ratio - 1.0) < 1e-9) {
            const float* src = clip.audioData + static_cast<uint64_t>(phase) * 2;
            const double clipGainD = static_cast<double>(clip.gain);

            // Precompute fade region boundaries for this clip/block intersection.
            const uint32_t fadeInFrames =
                (start < clip.startSample + CLIP_EDGE_FADE_SAMPLES)
                    ? static_cast<uint32_t>(std::min(static_cast<uint64_t>(framesToRender),
                                                     clip.startSample + CLIP_EDGE_FADE_SAMPLES - start))
                    : 0;

            const uint32_t fadeOutBegin =
                (start + framesToRender > clip.endSample - CLIP_EDGE_FADE_SAMPLES)
                    ? static_cast<uint32_t>(std::max(static_cast<int64_t>(0),
                                                     static_cast<int64_t>(clip.endSample - CLIP_EDGE_FADE_SAMPLES) -
                                                         static_cast<int64_t>(start)))
                    : framesToRender;

            const uint32_t bodyBegin = fadeInFrames;
            const uint32_t bodyEnd = std::min(fadeOutBegin, framesToRender);

            if (fadeInFrames > 0) {
                for (uint32_t i = 0; i < fadeInFrames; ++i) {
                    const double fade = static_cast<double>(start + i - clip.startSample) / CLIP_EDGE_FADE_SAMPLES;
                    dst[i * 2] += static_cast<double>(src[i * 2]) * clipGainD * fade;
                    dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGainD * fade;
                }
            }

            if (bodyEnd > bodyBegin) {
                for (uint32_t i = bodyBegin; i < bodyEnd; ++i) {
                    dst[i * 2] += static_cast<double>(src[i * 2]) * clipGainD;
                    dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGainD;
                }
            }

            if (framesToRender > std::max(fadeOutBegin, fadeInFrames)) {
                const uint32_t fadeOutStart = std::max(fadeOutBegin, fadeInFrames);
                for (uint32_t i = fadeOutStart; i < framesToRender; ++i) {
                    const double fade = static_cast<double>(clip.endSample - (start + i)) / CLIP_EDGE_FADE_SAMPLES;
                    dst[i * 2] += static_cast<double>(src[i * 2]) * clipGainD * fade;
                    dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGainD * fade;
                }
            }
        } else {
            using InterpFn = void (*)(const float*, int64_t, double, float&, float&);
            // Kernel table matches AudioEngine::renderGraph's clip loop exactly, so an
            // isolated-track bounce (this renderer's only caller, via bounceRangeToWav
            // trackId >= 0) resamples identically to mainline playback/full-mix export.
            // Sinc32/Sinc64 previously dispatched to the Turbo LUT kernels here, giving
            // solo bounces a measurably lower quality floor than the full mix (~88 dB vs
            // ~147 dB SINAD at fractional ratios) — resolved F6, measured by
            // SessionResamplingTruthTest; see Aestra-Internals: aestra-docs/audio-research-bench.md §8.
            // This path runs offline only, so the slower exact-sinc kernels are fine.
            InterpFn interpolateFunc = Interpolators::Sinc64Interpolator::interpolate;
            switch (cachedInterpQuality) {
            case Interpolators::InterpolationQuality::Cubic:
                interpolateFunc = Interpolators::CubicInterpolator::interpolate;
                break;
            case Interpolators::InterpolationQuality::Sinc8:
                interpolateFunc = Interpolators::Sinc8Interpolator::interpolate;
                break;
            case Interpolators::InterpolationQuality::Sinc16:
                interpolateFunc = Interpolators::Sinc16Interpolator::interpolate;
                break;
            case Interpolators::InterpolationQuality::Sinc32:
                interpolateFunc = Interpolators::Sinc32Interpolator::interpolate;
                break;
            case Interpolators::InterpolationQuality::Sinc64:
                interpolateFunc = Interpolators::Sinc64Interpolator::interpolate;
                break;
            default: {
                static_assert(static_cast<int>(Interpolators::InterpolationQuality::Sinc64) == 4,
                              "All InterpolationQuality values must be handled above");
                interpolateFunc = Interpolators::Sinc64Interpolator::interpolate;
                break;
            }
            }

            const uint32_t fadeInFrames =
                (start < clip.startSample + CLIP_EDGE_FADE_SAMPLES)
                    ? static_cast<uint32_t>(std::min(static_cast<uint64_t>(framesToRender),
                                                     clip.startSample + CLIP_EDGE_FADE_SAMPLES - start))
                    : 0;

            const uint32_t fadeOutBegin =
                (start + framesToRender > clip.endSample - CLIP_EDGE_FADE_SAMPLES)
                    ? static_cast<uint32_t>(std::max(static_cast<int64_t>(0),
                                                     static_cast<int64_t>(clip.endSample - CLIP_EDGE_FADE_SAMPLES) -
                                                         static_cast<int64_t>(start)))
                    : framesToRender;

            const uint32_t bodyBegin = fadeInFrames;
            const uint32_t bodyEnd = std::min(fadeOutBegin, framesToRender);

            if (fadeInFrames > 0) {
                for (uint32_t i = 0; i < fadeInFrames; ++i) {
                    const double fade = static_cast<double>(start + i - clip.startSample) / CLIP_EDGE_FADE_SAMPLES;
                    float outL, outR;
                    interpolateFunc(clip.audioData, clip.totalFrames, phase, outL, outR);
                    dst[i * 2] += static_cast<double>(outL) * clip.gain * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clip.gain * fade;
                    phase += ratio;
                }
            }

            if (bodyEnd > bodyBegin) {
                for (uint32_t i = bodyBegin; i < bodyEnd; ++i) {
                    float outL, outR;
                    interpolateFunc(clip.audioData, clip.totalFrames, phase, outL, outR);
                    dst[i * 2] += static_cast<double>(outL) * clip.gain;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clip.gain;
                    phase += ratio;
                }
            }

            if (framesToRender > std::max(fadeOutBegin, fadeInFrames)) {
                const uint32_t fadeOutStart = std::max(fadeOutBegin, fadeInFrames);
                for (uint32_t i = fadeOutStart; i < framesToRender; ++i) {
                    const double fade = static_cast<double>(clip.endSample - (start + i)) / CLIP_EDGE_FADE_SAMPLES;
                    float outL, outR;
                    interpolateFunc(clip.audioData, clip.totalFrames, phase, outL, outR);
                    dst[i * 2] += static_cast<double>(outL) * clip.gain * fade;
                    dst[i * 2 + 1] += static_cast<double>(outR) * clip.gain * fade;
                    phase += ratio;
                }
            }
        }
    }
}

void AudioRenderer::processTrackEffects(const RenderTrack& track, AudioGraphState& graphState, uint32_t numFrames,
                                        uint32_t bufferOffset, AudioEngine& engineRef, const AudioGraph& graph,
                                        bool snapGains) {
    if (track.trackIndex >= graphState.trackStates.size())
        return;
    TrackRTState& state = graphState.trackStates[track.trackIndex];
    if (track.trackIndex < graph.tracks.size()) {
        const auto& gt = graph.tracks[track.trackIndex];

        // Read continuous params (fader/trim/pan) for export parity with real-time engine
        double volTarget = static_cast<double>(gt.volume);
        double panTarget = clampD(static_cast<double>(gt.pan), -1.0, 1.0);

        auto* continuous = engineRef.m_continuousParamsRaw.load(std::memory_order_acquire);
        auto* slotMap = engineRef.m_channelSlotMapRaw.load(std::memory_order_acquire);
        if (continuous && slotMap) {
            const uint32_t slot = slotMap->getSlotIndex(gt.trackId);
            if (slot != ChannelSlotMap::INVALID_SLOT) {
                float faderDb = 0.0f;
                float panParam = 0.0f;
                float trimDb = 0.0f;
                continuous->read(slot, faderDb, panParam, trimDb);
                // Same gain staging as the live path: the fader is
                // track.volume, trim lives in the continuous slot. The
                // continuous faderDb/panParam are display mirrors only.
                const double trimDbClamped = clampD(static_cast<double>(trimDb), -24.0, 24.0);
                volTarget *= dbToLinearD(trimDbClamped);
            }
        }

        // stereoBalance, always: equalPower would attenuate stereo content by
        // -3.01 dB at centre and make a channel's own level depend on whether
        // it receives an audible route. Matches the live path and the
        // direct-to-Master reference (which applies no strip pan law).
        double gainL, gainR;
        fastStereoBalanceGainsD(panTarget, volTarget, gainL, gainR);
        state.gainL.setTarget(gainL);
        state.gainR.setTarget(gainR);
    }
    if (snapGains) {
        // Offline bounce: render at the settled target from frame 0. Live
        // playback ramps 1.0 -> target over one realtime block after a graph
        // compile; the offline loop uses 4096-frame blocks, so the same ramp
        // would smear the settle over ~8x the frames and misalign a bounce
        // with live playback (#745).
        state.gainL.snap();
        state.gainR.snap();
    } else {
        state.gainL.beginRamp(numFrames);
        state.gainR.beginRamp(numFrames);
    }
    double* self = track.selfBuffer + bufferOffset * 2;
    for (uint32_t i = 0; i < numFrames; ++i) {
        self[i * 2] *= state.gainL.next();
        self[i * 2 + 1] *= state.gainR.next();
    }
}

void AudioRenderer::renderArsenalUnitsForTrack(uint32_t mixerChannelId, double* trackBuffer, const Context& ctx,
                                               AudioEngine& engineRef) {
    // Arsenal currently participates inside the main engine render path.
    ArsenalProcessingContext arsenal(engineRef.m_unitManager.load(std::memory_order_acquire));
    auto snap = arsenal.getSnapshot();
    if (!snap || snap->units.empty())
        return;
    const bool anyUnitSolo = std::any_of(snap->units.begin(), snap->units.end(), [](const UnitState& unit) {
        return unit.enabled && unit.isSolo && !unit.isMuted;
    });

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        if (u.enabled && u.plugin && !u.isMuted && (!anyUnitSolo || u.isSolo) &&
            arsenal.shouldRenderToMixerChannel(u, mixerChannelId)) {
            std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2, 0.0f);
            float* outs[2] = {engineRef.m_pluginBufferF.data(), engineRef.m_pluginBufferF.data() + ctx.numFrames};
            MidiBuffer* mIn =
                (bIdx < engineRef.m_unitMidiBuffers.size()) ? &engineRef.m_unitMidiBuffers[bIdx] : nullptr;
            MidiBuffer mOut;
            const bool processed = processPluginNoexcept(*u.plugin, ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);
            if (!processed) {
                std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2,
                          0.0f);
            } else {
                sanitizeFloatBuffers(outs, 2, ctx.numFrames);
            }

            double* dst = trackBuffer + (size_t)ctx.bufferOffset * 2;
            const double unitGain = std::isfinite(u.gain) ? static_cast<double>(u.gain) : 1.0;
            for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                dst[i * 2] += static_cast<double>(outs[0][i]) * unitGain;
                dst[i * 2 + 1] += static_cast<double>(outs[1][i]) * unitGain;
            }
        }
        bIdx++;
    }
}

void AudioRenderer::processArsenalUnits(const Context& ctx, AudioEngine& engineRef) {
    if (ctx.isolatedTrackIndex >= 0)
        return;
    // Current semantics: PreviewToMaster units are audible and export-participating
    // because export follows the same processBlock authority.
    ArsenalProcessingContext arsenal(engineRef.m_unitManager.load(std::memory_order_acquire));
    auto snap = arsenal.getSnapshot();
    if (!snap || snap->units.empty())
        return;
    const bool anyUnitSolo = std::any_of(snap->units.begin(), snap->units.end(), [](const UnitState& unit) {
        return unit.enabled && unit.isSolo && !unit.isMuted;
    });

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        if (u.enabled && u.plugin && !u.isMuted && (!anyUnitSolo || u.isSolo) && arsenal.shouldRenderToMaster(u)) {
            std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2, 0.0f);
            float* outs[2] = {engineRef.m_pluginBufferF.data(), engineRef.m_pluginBufferF.data() + ctx.numFrames};
            MidiBuffer* mIn =
                (bIdx < engineRef.m_unitMidiBuffers.size()) ? &engineRef.m_unitMidiBuffers[bIdx] : nullptr;
            MidiBuffer mOut;
            const bool processed = processPluginNoexcept(*u.plugin, ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);
            if (!processed) {
                std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2,
                          0.0f);
            } else {
                sanitizeFloatBuffers(outs, 2, ctx.numFrames);
            }

            double* dst = ctx.masterBuffer + (size_t)ctx.bufferOffset * 2;
            const double unitGain = std::isfinite(u.gain) ? static_cast<double>(u.gain) : 1.0;
            for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                dst[i * 2] += static_cast<double>(outs[0][i]) * unitGain;
                dst[i * 2 + 1] += static_cast<double>(outs[1][i]) * unitGain;
            }
        }
        bIdx++;
    }
}

} // namespace Audio
} // namespace Aestra
