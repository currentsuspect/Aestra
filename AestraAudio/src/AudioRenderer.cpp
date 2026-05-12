// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioRenderer.h"

#include "../../AestraCore/include/AestraMath.h"
#include "ArsenalProcessingContext.h"
#include "AudioEngine.h"
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
    float p = (static_cast<float>(pan) + 1.0f) * 0.5f;
    gainL = static_cast<double>(std::cos(p * 1.57079632679f)) * vol;
    gainR = static_cast<double>(std::sin(p * 1.57079632679f)) * vol;
}
} // namespace

AudioRenderer::AudioRenderer() {}
AudioRenderer::~AudioRenderer() {}

void AudioRenderer::renderBlock(const Context& ctx, AudioGraphState& state, AudioEngine& engineRef) {
    // Get meter snapshot buffer for track metering
    auto* snaps = engineRef.m_meterSnapshotsRaw.load(std::memory_order_relaxed);
    auto* slotMap = engineRef.m_channelSlotMapRaw.load(std::memory_order_relaxed);

    // Iterate through topologically sorted render tracks
    for (const auto& track : state.renderTracks) {
        if (track.trackIndex >= state.trackStates.size())
            continue;

        // If we are isolating a track, skip others.
        if (ctx.isolatedTrackIndex >= 0 && track.trackIndex != (uint32_t)ctx.isolatedTrackIndex) {
            continue;
        }

        TrackRTState& trackState = state.trackStates[track.trackIndex];

        // 1. Render Clips (Generates Audio) -> track.selfBuffer
        renderClipAudio(track.selfBuffer, trackState, track.trackIndex, ctx, engineRef);

        // [NEW] 1.5 Render Arsenal Units assigned to this track
        renderArsenalUnitsForTrack(track.trackIndex, track.selfBuffer, ctx, engineRef);

        // 2. Process Effects (In-Place) -> track.selfBuffer
        processTrackEffects(track, state, ctx.numFrames, ctx.bufferOffset, engineRef);

        // 3. Calculate Track Meter Peaks (post-fader)
        if (!ctx.isOffline && track.selfBuffer && snaps && slotMap && track.trackIndex < ctx.graph->tracks.size()) {
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
    std::memset(outputBuffer + bufferOffset * 2, 0, numFrames * 2 * sizeof(double));
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
        const double ratio = srcRate / outputRate;
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
                    const double fade =
                        static_cast<double>(start + i - clip.startSample) / CLIP_EDGE_FADE_SAMPLES;
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
                    const double fade =
                        static_cast<double>(clip.endSample - (start + i)) / CLIP_EDGE_FADE_SAMPLES;
                    dst[i * 2] += static_cast<double>(src[i * 2]) * clipGainD * fade;
                    dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGainD * fade;
                }
            }
        } else {
            using InterpFn = void (*)(const float*, int64_t, double, float&, float&);
            InterpFn interpolateFunc = Interpolators::Sinc64Turbo::interpolate;
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
                interpolateFunc = Interpolators::Sinc32Turbo::interpolate;
                break;
            case Interpolators::InterpolationQuality::Sinc64:
                interpolateFunc = Interpolators::Sinc64Turbo::interpolate;
                break;
            default: {
                static_assert(static_cast<int>(Interpolators::InterpolationQuality::Sinc64) == 4,
                             "All InterpolationQuality values must be handled above");
                interpolateFunc = Interpolators::Sinc64Turbo::interpolate;
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
                                        uint32_t bufferOffset, AudioEngine& engineRef) {
    if (track.trackIndex >= graphState.trackStates.size())
        return;
    TrackRTState& state = graphState.trackStates[track.trackIndex];
    const AudioGraph& graph = engineRef.engineState().activeGraph();
    if (track.trackIndex < graph.tracks.size()) {
        const auto& gt = graph.tracks[track.trackIndex];
        double gainL, gainR;
        fastPanGainsD(clampD(gt.pan, -1.0, 1.0), (double)gt.volume, gainL, gainR);
        state.gainL.setTarget(gainL);
        state.gainR.setTarget(gainR);
    }
    double* self = track.selfBuffer + bufferOffset * 2;
    for (uint32_t i = 0; i < numFrames; ++i) {
        self[i * 2] *= state.gainL.next();
        self[i * 2 + 1] *= state.gainR.next();
    }
    state.gainL.snap();
    state.gainR.snap();
}

void AudioRenderer::processArsenalMidi(const Context& ctx, AudioEngine& engineRef) {
    auto* pe = engineRef.m_patternEngine.load(std::memory_order_acquire);
    ArsenalProcessingContext arsenal(engineRef.m_unitManager.load(std::memory_order_acquire), pe);
    if (!pe || !arsenal.unitManager() || ctx.sampleRate == 0)
        return;
    auto snap = arsenal.getSnapshot();
    if (!snap || snap->units.empty())
        return;

    std::array<PatternPlaybackEngine::UnitMidiRoute, 256> routes{};
    size_t count = 0, bIdx = 0;
    for (const auto& u : snap->units) {
        if (count >= 256 || bIdx >= engineRef.m_unitMidiBuffers.size())
            break;
        // engineRef.m_unitMidiBuffers[bIdx].clear(); // REMOVED: Handled externally
        routes[count++] = {static_cast<UnitID>(u.id), &engineRef.m_unitMidiBuffers[bIdx]};
        bIdx++;
    }
    if (engineRef.m_transportPlaying.load(std::memory_order_relaxed)) {
        pe->refillWindow(ctx.globalPos, (int)ctx.sampleRate, 2048);
        pe->processAudio(ctx.globalPos, (int)ctx.numFrames, routes.data(), count);
    }
}

void AudioRenderer::renderArsenalUnitsForTrack(uint32_t trackIndex, double* trackBuffer, const Context& ctx,
                                               AudioEngine& engineRef) {
    // Arsenal currently participates inside the main engine render path.
    ArsenalProcessingContext arsenal(engineRef.m_unitManager.load(std::memory_order_acquire));
    auto snap = arsenal.getSnapshot();
    if (!snap || snap->units.empty())
        return;

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        if (u.enabled && u.plugin && arsenal.shouldRenderToTimelineTrack(u, trackIndex)) {
            std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2, 0.0f);
            float* outs[2] = {engineRef.m_pluginBufferF.data(), engineRef.m_pluginBufferF.data() + ctx.numFrames};
            MidiBuffer* mIn =
                (bIdx < engineRef.m_unitMidiBuffers.size()) ? &engineRef.m_unitMidiBuffers[bIdx] : nullptr;
            MidiBuffer mOut;
            u.plugin->process(ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);

            double* dst = trackBuffer + (size_t)ctx.bufferOffset * 2;
            for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                dst[i * 2] += (double)outs[0][i];
                dst[i * 2 + 1] += (double)outs[1][i];
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

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        // Only handle units not routed to a Timeline track.
        if (u.enabled && u.plugin && arsenal.shouldRenderToMasterPreview(u)) {
            std::fill(engineRef.m_pluginBufferF.begin(), engineRef.m_pluginBufferF.begin() + ctx.numFrames * 2, 0.0f);
            float* outs[2] = {engineRef.m_pluginBufferF.data(), engineRef.m_pluginBufferF.data() + ctx.numFrames};
            MidiBuffer* mIn =
                (bIdx < engineRef.m_unitMidiBuffers.size()) ? &engineRef.m_unitMidiBuffers[bIdx] : nullptr;
            MidiBuffer mOut;
            u.plugin->process(ins, outs, 2, 2, ctx.numFrames, mIn, &mOut);

            double* dst = ctx.masterBuffer + (size_t)ctx.bufferOffset * 2;
            for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                dst[i * 2] += (double)outs[0][i];
                dst[i * 2 + 1] += (double)outs[1][i];
            }
        }
        bIdx++;
    }
}

} // namespace Audio
} // namespace Aestra
