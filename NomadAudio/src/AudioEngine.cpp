// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Nomad {
namespace Audio {

void AudioEngine::applyPendingCommands() {
    AudioQueueCommand cmd;
    // Bounded drain - max 16 commands per block (less work = less RT risk)
    int cmdCount = 0;
    bool hasTransport = false;
    AudioQueueCommand lastTransport;
    
    while (cmdCount < 16 && m_commandQueue.pop(cmd)) {
        ++cmdCount;
        // Coalesce transport commands: keep only the latest per block.
        if (cmd.type == AudioQueueCommandType::SetTransportState) {
            lastTransport = cmd;
            hasTransport = true;
            continue;
        }

        switch (cmd.type) {
            case AudioQueueCommandType::None:
                break;
            case AudioQueueCommandType::SetTrackVolume: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.volume.setTarget(static_cast<double>(cmd.value1));
                break;
            }
            case AudioQueueCommandType::SetTrackPan: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.pan.setTarget(static_cast<double>(cmd.value1));
                break;
            }
            case AudioQueueCommandType::SetTrackMute: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.mute = (cmd.value1 != 0.0f);
                break;
            }
            case AudioQueueCommandType::SetTrackSolo: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.solo = (cmd.value1 != 0.0f);
                break;
            }
            default:
                break;
        }
    }

    if (hasTransport) {
        const bool wasPlaying = m_transportPlaying;
        const uint64_t oldPos = m_globalSamplePos;
        const bool nextPlaying = (lastTransport.value1 != 0.0f);
        const bool posChanged = (lastTransport.samplePos != oldPos);

        m_transportPlaying = nextPlaying;
        m_globalSamplePos = lastTransport.samplePos;

        if (nextPlaying && (!wasPlaying || posChanged)) {
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (nextPlaying && m_fadeState == FadeState::Silent) {
            m_fadeState = FadeState::None;
            m_fadeSamplesRemaining = 0;
        }
    }
}

void AudioEngine::processBlock(float* outputBuffer,
                               const float* inputBuffer,
                               uint32_t numFrames,
                               double streamTime) {
    (void)inputBuffer;
    (void)streamTime;

    if (!outputBuffer || numFrames == 0) {
        return;
    }

    const bool wasPlaying = m_transportPlaying;

    // Process commands FIRST (lock-free)
    applyPendingCommands();

    // State transitions
    if (wasPlaying && !m_transportPlaying &&
        m_fadeState != FadeState::FadingOut && m_fadeState != FadeState::Silent) {
        m_fadeState = FadeState::FadingOut;
        m_fadeSamplesRemaining = FADE_OUT_SAMPLES;
    }
    if (!wasPlaying && m_transportPlaying && m_fadeState != FadeState::FadingIn) {
        m_fadeState = FadeState::None;
        m_fadeSamplesRemaining = 0;
    }

    // Fast path: silent
    if (m_fadeState == FadeState::Silent) {
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * m_outputChannels * sizeof(float));
        m_telemetry.blocksProcessed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Render to double-precision master buffer
    const AudioGraph& graph = m_state.activeGraph();

    // Transport looping: if playback has passed the end of the timeline, wrap to 0.
    // This is a simple whole-timeline loop until loop regions are implemented.
    if (m_transportPlaying && graph.timelineEndSample > 0 &&
        m_globalSamplePos >= graph.timelineEndSample) {
        m_globalSamplePos = 0;
        m_fadeState = FadeState::FadingIn;
        m_fadeSamplesRemaining = FADE_IN_SAMPLES;
    }
    if (!m_masterBufferD.empty() && (m_transportPlaying || m_fadeState == FadeState::FadingOut)) {
        renderGraph(graph, numFrames);
    } else {
        // Zero the double buffer
        std::fill(m_masterBufferD.begin(), 
                  m_masterBufferD.begin() + static_cast<size_t>(numFrames) * m_outputChannels, 
                  0.0);
    }

    // === Final Output Stage (double -> float with processing) ===
    // Pre-compute master gain for this block (avoid per-sample target update)
    const double targetGain = static_cast<double>(m_masterGainTarget) * static_cast<double>(m_headroomLinear);
    const double currentGain = m_smoothedMasterGain.current;
    const double gainDelta = (targetGain - currentGain) / static_cast<double>(numFrames);
    double gain = currentGain;
    
    double peakL = 0.0;
    double peakR = 0.0;
    
    const double* src = m_masterBufferD.data();
    
    const bool safety = m_safetyProcessingEnabled;
    // Optimized output loop - minimal branches
    for (uint32_t i = 0; i < numFrames; ++i) {
        // Read from double buffer
        double L = src[i * 2] * gain;
        double R = src[i * 2 + 1] * gain;

        if (safety) {
            // DC blocking
            {
                double y = L - m_dcBlockerL.x1 + DCBlockerD::R * m_dcBlockerL.y1;
                m_dcBlockerL.x1 = L;
                m_dcBlockerL.y1 = y;
                L = y;
            }
            {
                double y = R - m_dcBlockerR.x1 + DCBlockerD::R * m_dcBlockerR.y1;
                m_dcBlockerR.x1 = R;
                m_dcBlockerR.y1 = y;
                R = y;
            }

            // Soft safety clip (disabled by default; enable for debugging only)
            if (L > 1.5) L = 1.0;
            else if (L < -1.5) L = -1.0;
            else { const double x2 = L * L; L = L * (27.0 + x2) / (27.0 + 9.0 * x2); }

            if (R > 1.5) R = 1.0;
            else if (R < -1.5) R = -1.0;
            else { const double x2 = R * R; R = R * (27.0 + x2) / (27.0 + 9.0 * x2); }
        }
        
        // Track peaks
        const double absL = (L >= 0.0) ? L : -L;
        const double absR = (R >= 0.0) ? R : -R;
        if (absL > peakL) peakL = absL;
        if (absR > peakR) peakR = absR;
        
        // Output as float
        outputBuffer[i * 2] = static_cast<float>(L);
        outputBuffer[i * 2 + 1] = static_cast<float>(R);
        
        gain += gainDelta;
    }
    
    // Update smoothed gain state
    m_smoothedMasterGain.current = targetGain;
    m_smoothedMasterGain.target = targetGain;
    
    m_peakL.store(static_cast<float>(peakL), std::memory_order_relaxed);
    m_peakR.store(static_cast<float>(peakR), std::memory_order_relaxed);

    // Fade envelopes (short ramps prevent clicks on stop/seek)
    if (m_fadeState == FadeState::FadingIn) {
        const double fadeTotal = static_cast<double>(FADE_IN_SAMPLES);
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (m_fadeSamplesRemaining == 0) {
                m_fadeState = FadeState::None;
                break;
            }
            const double progress = 1.0 - (static_cast<double>(m_fadeSamplesRemaining) / fadeTotal);
            const double fadeGain = progress * progress * (3.0 - 2.0 * progress); // Smoothstep
            outputBuffer[i * 2] *= static_cast<float>(fadeGain);
            outputBuffer[i * 2 + 1] *= static_cast<float>(fadeGain);
            --m_fadeSamplesRemaining;
        }
    } else if (m_fadeState == FadeState::FadingOut) {
        const double fadeTotal = static_cast<double>(FADE_OUT_SAMPLES);
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (m_fadeSamplesRemaining == 0) {
                std::memset(outputBuffer + i * 2, 0,
                            static_cast<size_t>(numFrames - i) * m_outputChannels * sizeof(float));
                m_fadeState = FadeState::Silent;
                break;
            }
            const double t = static_cast<double>(m_fadeSamplesRemaining) / fadeTotal;
            const double fadeGain = t * t * (3.0 - 2.0 * t);  // Smoothstep

            outputBuffer[i * 2] *= static_cast<float>(fadeGain);
            outputBuffer[i * 2 + 1] *= static_cast<float>(fadeGain);
            --m_fadeSamplesRemaining;
        }
    }

    if (m_transportPlaying) {
        m_globalSamplePos += numFrames;
    }

    // Telemetry (lightweight counter only on RT thread)
    m_telemetry.blocksProcessed.fetch_add(1, std::memory_order_relaxed);
}

void AudioEngine::setBufferConfig(uint32_t maxFrames, uint32_t numChannels) {
    // Treat maxFrames as a hint; never shrink RT buffers.
    // Some drivers deliver larger blocks than requested, and shrinking can cause
    // renderGraph() to early-out -> audible crackles.
    m_outputChannels = numChannels;
    if (maxFrames > m_maxBufferFrames) {
        m_maxBufferFrames = maxFrames;
    }

    const size_t requiredSize = static_cast<size_t>(m_maxBufferFrames) * m_outputChannels;
    const bool needAlloc = m_masterBufferD.size() < requiredSize ||
                           m_trackBuffersD.size() != kMaxTracks;

    if (needAlloc) {
        m_masterBufferD.resize(requiredSize);
        std::memset(m_masterBufferD.data(), 0, requiredSize * sizeof(double));

        m_trackBuffersD.clear();
        m_trackBuffersD.resize(kMaxTracks);
        for (auto& buf : m_trackBuffersD) {
            buf.assign(requiredSize, 0.0);
        }
        if (m_trackState.size() != kMaxTracks) {
            m_trackState.assign(kMaxTracks, TrackRTState{});
        }
    }

    // Initialize smoothing coefficients based on requested buffer size
    const uint32_t coeffFrames = std::max<uint32_t>(1, maxFrames);
    m_smoothedMasterGain.coeff = 1.0 / static_cast<double>(coeffFrames);
}

void AudioEngine::renderGraph(const AudioGraph& graph, uint32_t numFrames) {
    // Guard
    if (numFrames > m_maxBufferFrames || m_outputChannels != 2) {
        std::memset(m_masterBufferD.data(), 0, 
                   static_cast<size_t>(numFrames) * m_outputChannels * sizeof(double));
        m_telemetry.underruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const size_t availableTracks = m_trackBuffersD.size();
    if (availableTracks == 0) {
        std::memset(m_masterBufferD.data(), 0,
                    static_cast<size_t>(numFrames) * m_outputChannels * sizeof(double));
        m_telemetry.underruns.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Clear master with memset (faster than std::fill for POD)
    std::memset(m_masterBufferD.data(), 0, 
               static_cast<size_t>(numFrames) * m_outputChannels * sizeof(double));

    const uint64_t blockStart = m_globalSamplePos;
    const uint64_t blockEnd = blockStart + numFrames;

    // Solo detection (single pass)
    bool anySolo = false;
    for (const auto& tr : graph.tracks) {
        auto& state = ensureTrackState(tr.trackIndex);
        if (tr.solo || state.solo) {
            anySolo = true;
            break;
        }
    }

    // Process tracks
    for (const auto& track : graph.tracks) {
        const uint32_t trackIdx = track.trackIndex;
        if (static_cast<size_t>(trackIdx) >= availableTracks) {
            m_telemetry.overruns.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        auto& state = ensureTrackState(trackIdx);
        
        // Skip early
        const bool muted = track.mute || state.mute;
        const bool soloed = track.solo || state.solo;
        if (muted || (anySolo && !soloed)) {
            continue;
        }

        // Empty tracks should not touch RT buffers. Still keep param state updated
        // so automation is consistent when clips appear later.
        if (track.clips.empty()) {
            state.volume.setTarget(static_cast<double>(track.volume));
            state.pan.setTarget(static_cast<double>(track.pan));
            state.volume.snap();
            state.pan.snap();
            continue;
        }
        
        auto& buffer = m_trackBuffersD[trackIdx];
        
        // Clear track buffer with memset
        std::memset(buffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));

        // Render clips
        for (const auto& clip : track.clips) {
            if (!clip.audioData || blockEnd <= clip.startSample || blockStart >= clip.endSample) {
                continue;
            }
            
            const uint64_t start = std::max(blockStart, clip.startSample);
            const uint64_t end = std::min(blockEnd, clip.endSample);
            const uint32_t localOffset = static_cast<uint32_t>(start - blockStart);
            uint32_t framesToRender = static_cast<uint32_t>(end - start);
            
            // Sample rate ratio
            const double outputRate = static_cast<double>(m_sampleRate);
            const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
            const double ratio = srcRate / outputRate;
            
            // Source position
            const double outputFrameOffset = static_cast<double>(start - clip.startSample);
            double phase = static_cast<double>(clip.sampleOffset) + outputFrameOffset * ratio;

            // Bounds
            const int64_t totalFrames = static_cast<int64_t>(clip.totalFrames);
            if (totalFrames > 0 && phase >= static_cast<double>(totalFrames)) {
                continue;
            }
            if (totalFrames > 0) {
                const double remaining = static_cast<double>(totalFrames) - phase;
                const uint32_t maxFrames = static_cast<uint32_t>(remaining / ratio);
                framesToRender = std::min(framesToRender, maxFrames);
            }
            if (framesToRender == 0) continue;

            const float* data = clip.audioData;
            double* dst = buffer.data() + static_cast<size_t>(localOffset) * 2;

            const uint64_t fadeLen = CLIP_EDGE_FADE_SAMPLES;

            // Fast path: matching sample rates - direct copy to double
            if (std::abs(ratio - 1.0) < 1e-9) {
                const uint64_t srcStart = static_cast<uint64_t>(phase);
                const float* src = data + srcStart * 2;
                const double clipGain = static_cast<double>(clip.gain);
                for (uint32_t i = 0; i < framesToRender; ++i) {
                    // Micro-fade at clip edges to avoid clicks/crackles.
                    double fade = 1.0;
                    const uint64_t projectSample = start + i;
                    if (fadeLen > 0) {
                        if (projectSample < clip.startSample + fadeLen) {
                            fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                        }
                        if (projectSample + fadeLen > clip.endSample) {
                            fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                        }
                    }
                    dst[i * 2] = static_cast<double>(src[i * 2]) * clipGain * fade;
                    dst[i * 2 + 1] = static_cast<double>(src[i * 2 + 1]) * clipGain * fade;
                }
            } else {
                // Resampling - use selected quality, pre-compute end condition
                const double phaseEnd = static_cast<double>(totalFrames);
                
                // Select interpolator at block level, not per-sample
                switch (m_interpQuality) {
                    case Interpolators::InterpolationQuality::Cubic:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::CubicInterpolator::interpolate(data, totalFrames, phase, outL, outR);
                            double fade = 1.0;
                            const uint64_t projectSample = start + i;
                            if (fadeLen > 0) {
                                if (projectSample < clip.startSample + fadeLen) {
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                                }
                            }
                            const double clipGain = static_cast<double>(clip.gain);
                            dst[i * 2] = static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] = static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc8:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::Sinc8Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                            double fade = 1.0;
                            const uint64_t projectSample = start + i;
                            if (fadeLen > 0) {
                                if (projectSample < clip.startSample + fadeLen) {
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                                }
                            }
                            const double clipGain = static_cast<double>(clip.gain);
                            dst[i * 2] = static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] = static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc16:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::Sinc16Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                            double fade = 1.0;
                            const uint64_t projectSample = start + i;
                            if (fadeLen > 0) {
                                if (projectSample < clip.startSample + fadeLen) {
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                                }
                            }
                            const double clipGain = static_cast<double>(clip.gain);
                            dst[i * 2] = static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] = static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc32:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::Sinc32Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                            double fade = 1.0;
                            const uint64_t projectSample = start + i;
                            if (fadeLen > 0) {
                                if (projectSample < clip.startSample + fadeLen) {
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                                }
                            }
                            const double clipGain = static_cast<double>(clip.gain);
                            dst[i * 2] = static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] = static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc64:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::Sinc64Interpolator::interpolate(data, totalFrames, phase, outL, outR);
                            double fade = 1.0;
                            const uint64_t projectSample = start + i;
                            if (fadeLen > 0) {
                                if (projectSample < clip.startSample + fadeLen) {
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen)));
                                }
                            }
                            const double clipGain = static_cast<double>(clip.gain);
                            dst[i * 2] = static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] = static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                }
            }
        }

        // Mix track into master - PRE-COMPUTE gains per block to avoid per-sample trig
        state.volume.setTarget(static_cast<double>(track.volume));
        state.pan.setTarget(static_cast<double>(track.pan));
        
        // Get current smoothed values
        const double vol = state.volume.current;
        const double pan = state.pan.current;
        const double volTarget = static_cast<double>(track.volume);
        const double panTarget = static_cast<double>(track.pan);
        
        // Pre-compute start/end gains (linear interpolation across block)
        const double panAngleStart = (pan + 1.0) * QUARTER_PI_D;
        const double panAngleEnd = (panTarget + 1.0) * QUARTER_PI_D;
        
        const double leftGainStart = std::cos(panAngleStart) * vol;
        const double rightGainStart = std::sin(panAngleStart) * vol;
        const double leftGainEnd = std::cos(panAngleEnd) * volTarget;
        const double rightGainEnd = std::sin(panAngleEnd) * volTarget;
        
        // Linear interpolation of gains across block (cheap, smooth)
        const double leftGainDelta = (leftGainEnd - leftGainStart) / static_cast<double>(numFrames);
        const double rightGainDelta = (rightGainEnd - rightGainStart) / static_cast<double>(numFrames);
        
        double leftGain = leftGainStart;
        double rightGain = rightGainStart;
        
        const double* trackData = buffer.data();
        double* master = m_masterBufferD.data();
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            master[i * 2] += trackData[i * 2] * leftGain;
            master[i * 2 + 1] += trackData[i * 2 + 1] * rightGain;
            leftGain += leftGainDelta;
            rightGain += rightGainDelta;
        }
        
        // Snap smoothed params to target for next block
        state.volume.snap();
        state.pan.snap();
    }
}

AudioEngine::TrackRTState& AudioEngine::ensureTrackState(uint32_t trackIndex) {
    if (m_trackState.empty()) {
        static TrackRTState dummy;
        return dummy;
    }
    if (trackIndex >= m_trackState.size()) {
        static TrackRTState dummy;
        return dummy;
    }
    return m_trackState[trackIndex];
}

} // namespace Audio
} // namespace Nomad
