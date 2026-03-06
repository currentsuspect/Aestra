// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioRenderer.h"

#include "../../AestraCore/include/AestraMath.h"
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
#include <immintrin.h>

namespace Aestra {
namespace Audio {

namespace {
static constexpr double PI_D = 3.14159265358979323846;
static constexpr double QUARTER_PI_D = PI_D * 0.25;
static constexpr uint64_t CLIP_EDGE_FADE_SAMPLES = 128;

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
    double* dstBase = outputBuffer + bufferOffset * 2;

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
            for (uint32_t i = 0; i < framesToRender; ++i) {
                double fade = 1.0;
                if (start + i < clip.startSample + CLIP_EDGE_FADE_SAMPLES)
                    fade = std::min(fade, (double)(start + i - clip.startSample) / CLIP_EDGE_FADE_SAMPLES);
                if (start + i + CLIP_EDGE_FADE_SAMPLES > clip.endSample)
                    fade = std::min(fade, (double)(clip.endSample - (start + i)) / CLIP_EDGE_FADE_SAMPLES);
                dst[i * 2] += (double)src[i * 2] * clip.gain * fade;
                dst[i * 2 + 1] += (double)src[i * 2 + 1] * clip.gain * fade;
            }
        } else {
            auto processResample = [&](auto interpolateFunc) {
                for (uint32_t i = 0; i < framesToRender; ++i) {
                    float outL, outR;
                    interpolateFunc(clip.audioData, clip.totalFrames, phase, outL, outR);
                    double fade = 1.0;
                    if (start + i < clip.startSample + CLIP_EDGE_FADE_SAMPLES)
                        fade = std::min(fade, (double)(start + i - clip.startSample) / CLIP_EDGE_FADE_SAMPLES);
                    if (start + i + CLIP_EDGE_FADE_SAMPLES > clip.endSample)
                        fade = std::min(fade, (double)(clip.endSample - (start + i)) / CLIP_EDGE_FADE_SAMPLES);
                    dst[i * 2] += (double)outL * clip.gain * fade;
                    dst[i * 2 + 1] += (double)outR * clip.gain * fade;
                    phase += ratio;
                }
            };
            auto q = engineRef.getInterpolationQuality();
            if (q == Interpolators::InterpolationQuality::Cubic)
                processResample(Interpolators::CubicInterpolator::interpolate);
            else if (q == Interpolators::InterpolationQuality::Sinc64)
                processResample(Interpolators::Sinc64Turbo::interpolate);
            else
                processResample(Interpolators::Sinc32Turbo::interpolate);
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
    auto* um = engineRef.m_unitManager.load(std::memory_order_acquire);
    if (!pe || !um || ctx.sampleRate == 0)
        return;
    auto snap = um->getAudioSnapshot();
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
    // [FIX] Audio during arsenal: Route to specific mixer tracks
    auto* um = engineRef.m_unitManager.load(std::memory_order_acquire);
    if (!um)
        return;
    auto snap = um->getAudioSnapshot();
    if (!snap || snap->units.empty())
        return;

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        if (u.enabled && u.plugin && u.routeId == (int)trackIndex) {
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
    auto* um = engineRef.m_unitManager.load(std::memory_order_acquire);
    if (!um)
        return;
    auto snap = um->getAudioSnapshot();
    if (!snap || snap->units.empty())
        return;

    const float* ins[2] = {engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data()};
    size_t bIdx = 0;
    for (const auto& u : snap->units) {
        // Only handle units NOT routed to a track (those going to Master)
        if (u.enabled && u.plugin && u.routeId < 0) {
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
