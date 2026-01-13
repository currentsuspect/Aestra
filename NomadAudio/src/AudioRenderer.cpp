// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioRenderer.h"
#include "AudioEngine.h" 
#include "Interpolators.h"
#include "../../NomadCore/include/NomadMath.h"
#include "UnitManager.h"
#include "PluginHost.h"
#include "EffectChain.h"
#include "Plugin/SamplerPlugin.h"
#include "PatternPlaybackEngine.h"
#include <immintrin.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Nomad {
namespace Audio {

    namespace {
        static constexpr double PI_D = 3.14159265358979323846;
        static constexpr double QUARTER_PI_D = PI_D * 0.25;
        static constexpr uint64_t CLIP_EDGE_FADE_SAMPLES = 128;

        inline double dbToLinearD(double db) {
            if (db <= -90.0) return 0.0;
            return static_cast<double>(Nomad::dbToGain(static_cast<float>(db)));
        }

        inline double clampD(double v, double lo, double hi) {
            return (v < lo) ? lo : (v > hi) ? hi : v;
        }

        inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
            float p = (static_cast<float>(pan) + 1.0f) * 0.5f; 
            gainL = static_cast<double>(std::cos(p * 1.57079632679f)) * vol;
            gainR = static_cast<double>(std::sin(p * 1.57079632679f)) * vol;
        }
    }

    AudioRenderer::AudioRenderer() {}
    AudioRenderer::~AudioRenderer() {}

    void AudioRenderer::renderBlock(const Context& ctx, AudioGraphState& state, AudioEngine& engineRef) {
        
        // Get meter snapshot buffer for track metering
        auto* snaps = engineRef.m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        auto* slotMap = engineRef.m_channelSlotMapRaw.load(std::memory_order_relaxed);
        
        // Iterate through topologically sorted render tracks
        for (const auto& track : state.renderTracks) {
            if (track.trackIndex >= state.trackStates.size()) continue;
            
            // If we are isolating a track, skip others.
            // Note: In a real DAW, we'd also need to render its BUSES/SENDS if complex routing exists.
            // But here, renderTracks are sorted and tracks usually route to Master.
            if (ctx.isolatedTrackIndex >= 0 && track.trackIndex != (uint32_t)ctx.isolatedTrackIndex) {
                continue;
            }

            TrackRTState& trackState = state.trackStates[track.trackIndex];

        // 1. Render Clips (Generates Audio) -> track.selfBuffer
        renderClipAudio(track.selfBuffer, trackState, track.trackIndex, ctx, engineRef);

        // 2. Process Effects (In-Place) -> track.selfBuffer
        processTrackEffects(track, state, ctx.numFrames, ctx.bufferOffset, engineRef);

            // 3. Calculate Track Meter Peaks (post-fader)
            if (!ctx.isOffline && track.selfBuffer && snaps && slotMap && track.trackIndex < ctx.graph->tracks.size()) {
                const auto& graphTrack = ctx.graph->tracks[track.trackIndex];
                const uint32_t slotIdx = slotMap->getSlotIndex(graphTrack.trackId);
                
                if (slotIdx != ChannelSlotMap::INVALID_SLOT && graphTrack.trackId != 0) {
                    // Do not meter Master in AudioRenderer (handled by AudioEngine processBlock)
                    double* src = track.selfBuffer + ctx.bufferOffset * 2;
                    double peakL = 0.0, peakR = 0.0;
                    double rmsAccL = 0.0, rmsAccR = 0.0;
                    
                    // Correlation accumulators
                    double sumLR = 0.0;
                    double sumLL = 0.0;
                    double sumRR = 0.0;
                    
                    for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                        const double L = src[i * 2];
                        const double R = src[i * 2 + 1];
                        const double absL = std::abs(L);
                        const double absR = std::abs(R);
                        if (absL > peakL) peakL = absL;
                        if (absR > peakR) peakR = absR;
                        rmsAccL += L * L;
                        rmsAccR += R * R;
                        
                        // Phase Correlation
                        sumLR += L * R;
                        sumLL += L * L;
                        sumRR += R * R;
                    }
                    
                    // Calculate RMS
                    const double invN = 1.0 / static_cast<double>(ctx.numFrames);
                    const float rmsL = static_cast<float>(std::sqrt(rmsAccL * invN));
                    const float rmsR = static_cast<float>(std::sqrt(rmsAccR * invN));
                    
                    // Calculate Correlation
                    float correlation = 0.0f;
                    const double den = std::sqrt(sumLL * sumRR);
                    if (den > 1e-9) {
                        correlation = static_cast<float>(sumLR / den);
                    } else if (sumLL < 1e-9 && sumRR < 1e-9) {
                         correlation = 0.0f;
                    }
                    
         // Write to Snapshot
        const float correlationValue = correlation;
        
         // Pass LUFS only for master (ID 0) - individual tracks get silence (-144)
        float lufs = -144.0f; 
        
        snaps->writeLevels(slotIdx,
                           static_cast<float>(peakL), static_cast<float>(peakR),
                           rmsL, rmsR, 
                           0.0f, 0.0f, // Low (not calc)
                           correlationValue,
                           lufs);
                }
            }

            // 4. Route to Destinations (Mixing)
            if (track.selfBuffer) {
                double* src = track.selfBuffer + ctx.bufferOffset * 2;
                
                // For each connection from this track
                for (const auto& conn : track.activeConnections) {
                    if (conn.destinationBufferL && conn.destinationBufferR) {
                         double* dstL = conn.destinationBufferL + ctx.bufferOffset * conn.stride;
                         double* dstR = conn.destinationBufferR + ctx.bufferOffset * conn.stride;
                         
                         for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                             double valL = src[i * 2] * conn.gainL;
                             double valR = src[i * 2 + 1] * conn.gainR;
                             
                             dstL[i * conn.stride] += valL;
                             dstR[i * conn.stride] += valR;
                         }
                    }
                }
            }
        }
    }

    void AudioRenderer::renderClipAudio(double* outputBuffer, 
                                        TrackRTState& state, 
                                        uint32_t trackIndex, 
                                        const Context& ctx,
                                        AudioEngine& engineRef) {
        
        uint32_t numFrames = ctx.numFrames;
        uint32_t bufferOffset = ctx.bufferOffset;
        const AudioGraph& graph = *ctx.graph;
                                            
        // Zero the buffer (Silence start)
        std::memset(outputBuffer + bufferOffset * 2, 0, numFrames * 2 * sizeof(double));
        
        // If muted at track level, we stop here (buffer remains silent)
        // Note: Effects might generate tail even if muted? 
        // Standard DAW: Mute silences the channel strip output. 
        // Pre-fader sends might be active?
        // For now, let's stick to the logic: Mute silences generation.
        if (state.mute) return;

        // Skip clips if pattern mode active
        if (engineRef.isPatternPlaybackMode()) return;
        
        // Retrieve clips from Graph
        if (trackIndex >= graph.tracks.size()) return;
        const auto& track = graph.tracks[trackIndex];
        const auto& clips = track.clips;

        if (clips.empty()) return;

        // Render Context
        const uint64_t blockStart = ctx.globalPos;
        const uint64_t blockEnd = blockStart + numFrames;
        const double outputRate = static_cast<double>(ctx.sampleRate);
        
        // Offset output buffer for partial renders
        double* dstBase = outputBuffer + bufferOffset * 2;
        
        for (const auto& clip : clips) {
             // Bounds Check
             if (!clip.audioData || blockEnd <= clip.startSample || blockStart >= clip.endSample) {
                 continue;
             }

             const uint64_t start = std::max(blockStart, clip.startSample);
             const uint64_t end = std::min(blockEnd, clip.endSample);
             const uint32_t localOffset = static_cast<uint32_t>(start - blockStart);
             uint32_t framesToRender = static_cast<uint32_t>(end - start);

             // Source sample rate processing
             const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
             const double ratio = srcRate / outputRate;
             
             // Calculate phases
             const double outputFrameOffset = static_cast<double>(start - clip.startSample);
             double phase = clip.sampleOffset + outputFrameOffset * ratio;
             
             // Source Length Check
             const int64_t totalFrames = static_cast<int64_t>(clip.totalFrames);
             if (totalFrames > 0) {
                 if (phase >= static_cast<double>(totalFrames)) continue;
                 const double remaining = static_cast<double>(totalFrames) - phase;
                 const uint32_t maxFrames = static_cast<uint32_t>(remaining / ratio);
                 framesToRender = std::min(framesToRender, maxFrames);
             }
             if (framesToRender == 0) continue;

             const float* data = clip.audioData;
             double* dst = dstBase + localOffset * 2;
             
             // Direct Copy (Fast Path)
             if (std::abs(ratio - 1.0) < 1e-9) {
                 const uint64_t srcStart = static_cast<uint64_t>(phase);
                 const float* src = data + srcStart * 2; // Stereo
                 const double clipGain = static_cast<double>(clip.gain);
                 
                 for (uint32_t i = 0; i < framesToRender; ++i) {
                     // Clip Edge Micro-Fades
                     double fade = 1.0;
                     const uint64_t projectSample = start + i;
                     if (projectSample < clip.startSample + CLIP_EDGE_FADE_SAMPLES) {
                         fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(CLIP_EDGE_FADE_SAMPLES)));
                     }
                     if (projectSample + CLIP_EDGE_FADE_SAMPLES > clip.endSample) {
                          fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(CLIP_EDGE_FADE_SAMPLES)));
                     }
                     
                     // Accumulate (Mix)
                     dst[i * 2]     += static_cast<double>(src[i * 2]) * clipGain * fade;
                     dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGain * fade;
                 }
             } else {
                 // High Quality Resampling
                 const double phaseEnd = static_cast<double>(totalFrames);
                 const auto quality = engineRef.getInterpolationQuality();
                 
                 // Using manual loop to avoid heavy copy-paste of big switch
                 // But for correctness we must support the modes.
                 // We will stick to Cubic and Sinc64Turbo as primary paths for now.
                 
                 // Helper lambda for inner loop
                 auto processResample = [&](auto interpolateFunc) {
                      for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                          float outL, outR;
                          interpolateFunc(data, totalFrames, phase, outL, outR);
                          double fade = 1.0;
                          const uint64_t projectSample = start + i;
                          if (projectSample < clip.startSample + CLIP_EDGE_FADE_SAMPLES) {
                               fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(CLIP_EDGE_FADE_SAMPLES)));
                          }
                          if (projectSample + CLIP_EDGE_FADE_SAMPLES > clip.endSample) {
                               fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(CLIP_EDGE_FADE_SAMPLES)));
                          }
                          const double clipGain = static_cast<double>(clip.gain);
                          dst[i * 2]     += static_cast<double>(outL) * clipGain * fade;
                          dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
                          phase += ratio;
                      }
                 };

                 if (quality == Interpolators::InterpolationQuality::Cubic) {
                     processResample(Interpolators::CubicInterpolator::interpolate);
                 } else if (quality == Interpolators::InterpolationQuality::Sinc64) {
                     processResample(Interpolators::Sinc64Turbo::interpolate);
                 } else {
                     // Fallback for others to Sinc32Turbo
                     processResample(Interpolators::Sinc32Turbo::interpolate);
                 }
             }
        }
    }

    void AudioRenderer::processTrackEffects(const RenderTrack& track, 
                                            AudioGraphState& graphState,
                                            uint32_t numFrames, 
                                            uint32_t bufferOffset,
                                            AudioEngine& engineRef) {
        
        if (track.trackIndex >= graphState.trackStates.size()) return;
        TrackRTState& state = graphState.trackStates[track.trackIndex];
        
        const AudioGraph& graph = engineRef.engineState().activeGraph();
        
        // 1. Process Effects via EffectChain (if any)
        if (track.trackIndex < graph.tracks.size()) {
             const auto& graphTrack = graph.tracks[track.trackIndex];
             
             if (graphTrack.effectChain && graphTrack.effectChain->getActiveSlotCount() > 0) {
                  // [TODO] FX Processing - Need scratch buffer injection
             }
        }

        // 2. Read Volume/Pan from Graph TrackRenderState and update gain targets
        if (track.trackIndex < graph.tracks.size()) {
            const auto& graphTrack = graph.tracks[track.trackIndex];
            
            // Use track's volume and pan directly
            const double volume = static_cast<double>(graphTrack.volume);
            const double pan = clampD(static_cast<double>(graphTrack.pan), -1.0, 1.0);
            
            // Calculate stereo pan gains
            double gainL, gainR;
            fastPanGainsD(pan, volume, gainL, gainR);
            
            // Update smoother targets
            state.gainL.setTarget(gainL);
            state.gainR.setTarget(gainR);
        }

        // 3. Apply Fader & Pan (in-place on selfBuffer)
        // Note: selfBuffer is interleaved stereo, so we process L/R pairs
        double* self = track.selfBuffer + bufferOffset * 2;
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            const double gL = state.gainL.next();
            const double gR = state.gainR.next();
            
            self[i * 2]     *= gL;  // Left channel
            self[i * 2 + 1] *= gR;  // Right channel
        }
        
        state.gainL.snap();
        state.gainR.snap();
    }

    void AudioRenderer::processArsenalUnits(const Context& ctx, AudioEngine& engineRef) {
        // If we are isolating a specific timeline track, we usually DON'T want Arsenal units mixed in
        // unless they are explicitly routed to that track (which they aren't in NOMAD's current design).
        if (ctx.isolatedTrackIndex >= 0) return;

        if (!ctx.graph || ctx.graph->tracks.empty()) return;

        // Skip Arsenal processing in Timeline mode
        if (!engineRef.isPatternPlaybackMode()) {
            return;
        }

        // Get dependencies via friend access
        auto* patternEngine = engineRef.m_patternEngine.load(std::memory_order_acquire);
        auto* unitManager = engineRef.m_unitManager.load(std::memory_order_acquire);
        
        if (!patternEngine || !unitManager) return;
        
        const uint32_t sampleRate = ctx.sampleRate;
        if (sampleRate == 0) return;
        
        const bool transportPlaying = engineRef.m_transportPlaying.load(std::memory_order_relaxed);
        const uint64_t currentFrame = ctx.globalPos;
        
        // Get Arsenal snapshot for RT-safe unit iteration
        auto snapshot = unitManager->getAudioSnapshot();
        if (!snapshot || snapshot->units.empty()) return;
        
        // Sync sample rate to units when it changes
        static uint32_t s_lastSyncedSampleRate = 0;
        if (s_lastSyncedSampleRate != sampleRate) {
            for (const auto& unitState : snapshot->units) {
                if (unitState.plugin) {
                    auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unitState.plugin);
                    if (sampler) {
                        sampler->setSampleRate(static_cast<double>(sampleRate));
                    }
                }
            }
            s_lastSyncedSampleRate = sampleRate;
        }
        
        const size_t requiredStereoSamples = static_cast<size_t>(ctx.numFrames) * 2;
        
        // Validate buffers (via friend access)
        if (engineRef.m_unitBufferD.size() < requiredStereoSamples ||
            engineRef.m_pluginBufferF.size() < requiredStereoSamples ||
            engineRef.m_silentBufferF.size() < ctx.numFrames) {
            return;
        }
        
        // Zero the silent buffer
        std::fill(engineRef.m_silentBufferF.begin(), 
                  engineRef.m_silentBufferF.begin() + ctx.numFrames, 0.0f);
        
        // Build unit MIDI routes (allocation-free)
        std::array<PatternPlaybackEngine::UnitMidiRoute, 256> unitMidiRoutes{};
        size_t unitMidiRouteCount = 0;
        size_t bufIdx = 0;
        for (const auto& unit : snapshot->units) {
            if (unitMidiRouteCount >= unitMidiRoutes.size()) break;
            if (bufIdx >= engineRef.m_unitMidiBuffers.size()) break;
            engineRef.m_unitMidiBuffers[bufIdx].clear();
            unitMidiRoutes[unitMidiRouteCount++] = { unit.id, &engineRef.m_unitMidiBuffers[bufIdx] };
            ++bufIdx;
        }
        
        // Process pattern MIDI events only while playing
        if (transportPlaying) {
            constexpr int LOOKAHEAD_SAMPLES = 2048;
            patternEngine->refillWindow(currentFrame, static_cast<int>(sampleRate), LOOKAHEAD_SAMPLES);
            patternEngine->processAudio(currentFrame, static_cast<int>(ctx.numFrames), 
                                        unitMidiRoutes.data(), unitMidiRouteCount);
        }

        // Process each unit plugin
        bufIdx = 0;
        const float* inputs[2] = { engineRef.m_silentBufferF.data(), engineRef.m_silentBufferF.data() };
        
        for (const auto& unit : snapshot->units) {
            if (!unit.enabled || !unit.plugin) {
                bufIdx++;
                continue;
            }
            
            // Clear plugin output buffer
            std::fill(engineRef.m_pluginBufferF.begin(), 
                      engineRef.m_pluginBufferF.begin() + requiredStereoSamples, 0.0f);
            
            // Output pointers (non-interleaved)
            float* outputs[2] = { engineRef.m_pluginBufferF.data(), 
                                  engineRef.m_pluginBufferF.data() + ctx.numFrames };
            
            // Find MIDI for this unit
            MidiBuffer* midiIn = nullptr;
            for (size_t r = 0; r < unitMidiRouteCount; ++r) {
                if (unitMidiRoutes[r].unitId == unit.id) {
                    midiIn = unitMidiRoutes[r].midiBuffer;
                    break;
                }
            }
            MidiBuffer midiOut;
            
            unit.plugin->process(inputs, outputs, 2, 2, ctx.numFrames, midiIn, &midiOut);
            
            // Mix plugin output into master buffer
            double* masterD = ctx.masterBuffer + static_cast<size_t>(ctx.bufferOffset) * 2;
            for (uint32_t i = 0; i < ctx.numFrames; ++i) {
                masterD[i * 2 + 0] += static_cast<double>(outputs[0][i]);
                masterD[i * 2 + 1] += static_cast<double>(outputs[1][i]);
            }
            
            bufIdx++;
        }
    }

} // namespace Audio
} // namespace Nomad


