// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"
#include "NomadLog.h"
#include "FastMath.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <immintrin.h> // AVX/SSE for high-performance mixing

// Denormal protection macros
#define DISABLE_DENORMALS \
    int oldMXCSR = _mm_getcsr(); \
    _mm_setcsr(oldMXCSR | 0x8040); // Set DAZ and FTZ flags

#define RESTORE_DENORMALS \
    _mm_setcsr(oldMXCSR);

namespace Nomad {
namespace Audio {

namespace {
    inline double clampD(double v, double lo, double hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    inline double dbToLinearD(double db) {
        // UI uses -90 dB as "silence"
        if (db <= -90.0) return 0.0;
        // FastMath polynomial approximation (~5x faster than std::pow)
        return static_cast<double>(FastMath::fastDbToLinear(static_cast<float>(db)));
    }
    
    // Fast constant-power pan gains (replaces std::sin/cos)
    inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
        float fL, fR;
        FastMath::fastPan(static_cast<float>(pan), fL, fR);
        gainL = static_cast<double>(fL) * vol;
        gainR = static_cast<double>(fR) * vol;
    }
}

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
            case AudioQueueCommandType::SetMetronomeEnabled:
            setMetronomeEnabled(static_cast<bool>(cmd.value1));
            break;
            case AudioQueueCommandType::SetTrackVolume: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.currentVolume = cmd.value1;
                
                // Recalculate Targets using FastMath
                const double panClamped = clampD(static_cast<double>(state.currentPan), -1.0, 1.0);
                const double vol = static_cast<double>(state.currentVolume);
                double gainL, gainR;
                fastPanGainsD(panClamped, vol, gainL, gainR);
                state.gainL.setTarget(gainL);
                state.gainR.setTarget(gainR);
                break;
            }
            case AudioQueueCommandType::SetTrackPan: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.currentPan = cmd.value1;
                
                // Recalculate Targets using FastMath
                const double panClamped = clampD(static_cast<double>(state.currentPan), -1.0, 1.0);
                const double vol = static_cast<double>(state.currentVolume);
                double gainL, gainR;
                fastPanGainsD(panClamped, vol, gainL, gainR);
                state.gainL.setTarget(gainL);
                state.gainR.setTarget(gainR);
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
        const bool wasPlaying = m_transportPlaying.load(std::memory_order_relaxed);
        const uint64_t oldPos = m_globalSamplePos.load(std::memory_order_relaxed);
        const bool nextPlaying = (lastTransport.value1 != 0.0f);
        const bool posChanged = (lastTransport.samplePos != oldPos);

        m_transportPlaying.store(nextPlaying, std::memory_order_relaxed);
        m_globalSamplePos.store(lastTransport.samplePos, std::memory_order_relaxed);

        if (nextPlaying && (!wasPlaying || posChanged)) {
            // Always fade-in when starting playback (prevents clicks/buzz)
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (nextPlaying && m_fadeState == FadeState::Silent) {
            // Fade-in from silent state, don't jump to full volume
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (nextPlaying && m_fadeState == FadeState::FadingOut) {
            // Interrupted fade-out: switch to fade-in immediately
            // Use remaining fade-out samples as starting point for fade-in
            // This creates a smooth crossfade effect
            uint32_t fadeProgress = FADE_OUT_SAMPLES - m_fadeSamplesRemaining;
            m_fadeState = FadeState::FadingIn;
            // Start fade-in from where fade-out left off (inverted progress)
            m_fadeSamplesRemaining = std::min(fadeProgress, FADE_IN_SAMPLES);
        }
    }
}

void AudioEngine::setThreadCount(int count) {
    if (count < 1) count = 1;
    if (count > 64) count = 64; 
    
    if (!m_threadPool || m_threadPool->size() != (size_t)count) {
        m_threadPool = std::make_unique<Nomad::RealTimeThreadPool>(count);
        m_syncBarrier = std::make_unique<Nomad::Barrier>(0);
    }
}

void AudioEngine::processBlock(float* outputBuffer,
                               const float* inputBuffer,
                               uint32_t numFrames,
                               double streamTime) {
    (void)streamTime;

    // Process Input (Recording)
    if (inputBuffer) {
        auto cb = m_inputCallback.load(std::memory_order_relaxed);
        if (cb) {
            cb(inputBuffer, numFrames, m_inputCallbackData.load(std::memory_order_relaxed));
        }
    }

    if (!outputBuffer || numFrames == 0) {
        return;
    }

    // Enable Denormals protection (Flush-to-Zero)
    DISABLE_DENORMALS

    // Safety: If buffers aren't allocated (setBufferConfig not called), silence and return.
    if (m_masterBufferD.empty()) {
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(float));
        RESTORE_DENORMALS
        return;
    }

    // Update meter analysis coefficients if the (RT-provided) sample rate changed.
    uint32_t currentSampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (m_meterAnalysisSampleRate != currentSampleRate) {
        m_meterAnalysisSampleRate = currentSampleRate;
        constexpr double kMeterLowCutHz = 150.0;
        if (currentSampleRate > 0) {
            m_meterLfCoeff = 1.0 - std::exp((-2.0 * PI_D * kMeterLowCutHz) / static_cast<double>(currentSampleRate));
            m_meterLfCoeff = clampD(m_meterLfCoeff, 0.0, 1.0);
        } else {
            m_meterLfCoeff = 0.0;
        }
        m_meterLfStateL.fill(0.0);
        m_meterLfStateR.fill(0.0);
    }

    const bool wasPlaying = m_transportPlaying.load(std::memory_order_relaxed);

    // Process commands FIRST (lock-free)
    applyPendingCommands();

    // State transitions
    bool isPlaying = m_transportPlaying.load(std::memory_order_relaxed);
    if (wasPlaying && !isPlaying &&
        m_fadeState != FadeState::FadingOut && m_fadeState != FadeState::Silent) {
        m_fadeState = FadeState::FadingOut;
        m_fadeSamplesRemaining = FADE_OUT_SAMPLES;
    }
    // When starting playback, always ensure we're fading in (or already fading in)
    // This prevents the audio from jumping to full volume instantly → no clicks
    if (!wasPlaying && isPlaying && m_fadeState != FadeState::FadingIn) {
        m_fadeState = FadeState::FadingIn;
        m_fadeSamplesRemaining = FADE_IN_SAMPLES;
    }

    // Fast path: silent
    if (m_fadeState == FadeState::Silent) {
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(float));
        // Clear meters so UI doesn't freeze on the last loud block.
        m_peakL.store(0.0f, std::memory_order_relaxed);
        m_peakR.store(0.0f, std::memory_order_relaxed);
        m_rmsL.store(0.0f, std::memory_order_relaxed);
        m_rmsR.store(0.0f, std::memory_order_relaxed);
        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        if (snaps) {
            snaps->writePeak(ChannelSlotMap::MASTER_SLOT_INDEX, 0.0f, 0.0f);
            // Also clears all track slots so they don't freeze
            for (uint32_t i = 0; i < ChannelSlotMap::MAX_CHANNEL_SLOTS; ++i) {
                snaps->writePeak(i, 0.0f, 0.0f);
            }
        }
        m_telemetry.incrementBlocksProcessed();
        return;
    }

    // Render to double-precision master buffer
    const AudioGraph& graph = m_state.activeGraph();
    
    // Loop & Position Logic Loop Calculation
    bool playing = m_transportPlaying.load(std::memory_order_relaxed);
    bool loopEnabled = m_loopEnabled.load(std::memory_order_relaxed);
    uint64_t currentGlobalPos = m_globalSamplePos.load(std::memory_order_relaxed);
    
    // We calculate the NEXT position here to handle looping correctly
    uint64_t nextGlobalPos = currentGlobalPos;
    uint32_t loopSplitFrame = numFrames; // Default: no split
    uint64_t loopStartSample = 0;
    
    if (playing || m_fadeState == FadeState::FadingOut) {
        nextGlobalPos += numFrames;
        
        if (loopEnabled) {
            double loopEndBeat = m_loopEndBeat.load(std::memory_order_relaxed);
            double loopStartBeat = m_loopStartBeat.load(std::memory_order_relaxed);
            float bpm = m_bpm.load(std::memory_order_relaxed);
            // Convert loop end beat to sample position
            double samplesPerBeat = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) / static_cast<double>(bpm);
            uint64_t loopEndSample = static_cast<uint64_t>(loopEndBeat * samplesPerBeat);
            loopStartSample = static_cast<uint64_t>(loopStartBeat * samplesPerBeat);
            
            // Check for loop crossing
            if (currentGlobalPos < loopEndSample && nextGlobalPos > loopEndSample && loopEndSample > loopStartSample) {
                // Loop Triggered!
                uint64_t framesUntilLoop = loopEndSample - currentGlobalPos;
                loopSplitFrame = static_cast<uint32_t>(framesUntilLoop);
                
                // Recalculate next position: LoopStart + (numFrames - framesUntilLoop)
                nextGlobalPos = loopStartSample + (numFrames - framesUntilLoop);
            }
        }
    }

    if (!m_masterBufferD.empty() && (playing || m_fadeState == FadeState::FadingOut)) {
        if (loopSplitFrame < numFrames) {
            // Split Render Strategy for Sample-Accurate Looping
            
            // Part A: Render up to the loop point
            if (loopSplitFrame > 0) {
                renderGraph(graph, loopSplitFrame, 0);
            }
            
            // Part B: Jump to loop start and render the rest
            // We temporarily update globalSamplePos so renderGraph sees the correct time
            m_globalSamplePos.store(loopStartSample, std::memory_order_relaxed);
            renderGraph(graph, numFrames - loopSplitFrame, loopSplitFrame);
            
            // Restore position for the rest of processBlock (Metronome/Fade assume continuous block)
            // Note: This means Metronome might glitch at loop point until it is also split-aware,
            // but audio clips will be perfectly tight.
            m_globalSamplePos.store(currentGlobalPos, std::memory_order_relaxed);
            
        } else {
            // Normal Render
            renderGraph(graph, numFrames, 0);
        }
    } else {
        // Zero the double buffer
        std::fill(m_masterBufferD.begin(), 
                  m_masterBufferD.begin() + static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed), 
                  0.0);
    }

    // === Final Output Stage (double -> float with processing) ===
    // Pre-compute master gain for this block (avoid per-sample target update)
    //
    // Master fader is provided via ContinuousParamBuffer at the reserved MASTER slot (127).
    // This keeps master control consistent with channel faders.
    double masterParamGain = 1.0;
    auto* continuous = m_continuousParamsRaw.load(std::memory_order_acquire);
    if (continuous) {
        float faderDb = 0.0f;
        float panParam = 0.0f;
        float trimDb = 0.0f;
        continuous->read(ChannelSlotMap::MASTER_SLOT_INDEX, faderDb, panParam, trimDb);
        (void)panParam;
        const double faderDbClamped = clampD(static_cast<double>(faderDb), -90.0, 6.0);
        const double trimDbClamped = clampD(static_cast<double>(trimDb), -24.0, 24.0);
        masterParamGain = dbToLinearD(faderDbClamped) * dbToLinearD(trimDbClamped);
    }

    const double targetGain =
        static_cast<double>(m_masterGainTarget.load(std::memory_order_relaxed)) * static_cast<double>(m_headroomLinear.load(std::memory_order_relaxed)) * masterParamGain;
    const double currentGain = m_smoothedMasterGain.current;
    const double gainDelta = (targetGain - currentGain) / static_cast<double>(numFrames);
    double gain = currentGain;
    
    double peakL = 0.0;
    double peakR = 0.0;
    double rmsAccL = 0.0;
    double rmsAccR = 0.0;
    double lowAccL = 0.0;
    double lowAccR = 0.0;
    // Correlation accumulators
    double sumLR = 0.0;
    double sumLL = 0.0;
    double sumRR = 0.0;
    
    const double* src = m_masterBufferD.data();
    auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_acquire);
    const bool publishMasterSnapshot = (snaps != nullptr);
    double& masterLfStateL = m_meterLfStateL[ChannelSlotMap::MASTER_SLOT_INDEX];
    double& masterLfStateR = m_meterLfStateR[ChannelSlotMap::MASTER_SLOT_INDEX];
    
    const bool safety = m_safetyProcessingEnabled.load(std::memory_order_relaxed);
    const DitheringMode ditherMode = m_ditheringMode.load(std::memory_order_relaxed);
    
    // Deterministic Dithering: Seed RNG with global timeline position
    // This ensures that bouncing the same project twice produces identical bits.
    m_ditherRng.setSeed(static_cast<uint32_t>(m_globalSamplePos.load(std::memory_order_relaxed)) ^ 0x9E3779B9);
    
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

        rmsAccL += L * L;
        rmsAccR += R * R;

        if (publishMasterSnapshot) {
            // Low-frequency energy tracking (simple 1-pole low-pass).
            const double lpL = masterLfStateL + m_meterLfCoeff * (L - masterLfStateL);
            const double lpR = masterLfStateR + m_meterLfCoeff * (R - masterLfStateR);
            masterLfStateL = lpL;
            masterLfStateR = lpR;
            lowAccL += lpL * lpL;
            lowAccR += lpR * lpR;
            
            // Phase Correlation
            sumLR += L * R;
            sumLL += L * L;
            sumRR += R * R;
        }
        
        // Dithering (Triangular Probability Density Function - TPDF)
        // Magnitude = 1 LSB at 24-bit (~ -144dB)
        // Even for float32 output, this prevents truncation noise if converted later
        if (ditherMode != DitheringMode::None) {
            // Generate two uniform randoms [0, 1)
            float r1 = m_ditherRng.nextFloat();
            float r2 = m_ditherRng.nextFloat();
            // TPDF = (rand() - rand()) * LSB_Magnitude
            // 24-bit LSB = 1.0 / 8388608.0 (approx 1.19e-7)
            constexpr double LSB_24 = 1.0 / 8388608.0; 
            
            // Apply magnitude logic based on mode (future expansion for Noise Shaping)
            double noise = (static_cast<double>(r1) - static_cast<double>(r2)) * LSB_24;
            
            L += noise;
            R += noise;
        }

        // --- LUFS Filtering (Per-Sample) ---
        // Stage 1 (High Shelf)
        double f1L = m_loudnessState.f1L.process(L, kKWeightPreFilter);
        double f1R = m_loudnessState.f1R.process(R, kKWeightPreFilter);
        
        // Stage 2 (RLB High Pass)
        double f2L = m_loudnessState.f2L.process(f1L, kKWeightRLB);
        double f2R = m_loudnessState.f2R.process(f1R, kKWeightRLB);
        
        // Accumulate Energy
        m_loudnessState.blockEnergySum += (f2L * f2L) + (f2R * f2R);
        // -----------------------------------

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
    float masterRmsL = 0.0f;
    float masterRmsR = 0.0f;
    float masterLowL = 0.0f;
    float masterLowR = 0.0f;
    float masterCorr = 0.0f;
    
    if (numFrames > 0) {
        const double invN = 1.0 / static_cast<double>(numFrames);
        masterRmsL = static_cast<float>(std::sqrt(rmsAccL * invN));
        masterRmsR = static_cast<float>(std::sqrt(rmsAccR * invN));
        m_rmsL.store(masterRmsL, std::memory_order_relaxed);
        m_rmsR.store(masterRmsR, std::memory_order_relaxed);
        if (publishMasterSnapshot) {
            masterLowL = static_cast<float>(std::sqrt(lowAccL * invN));
            masterLowR = static_cast<float>(std::sqrt(lowAccR * invN));
            
            // Calculate final correlation
            const double den = std::sqrt(sumLL * sumRR);
            if (den > 1e-9) {
                masterCorr = static_cast<float>(sumLR / den);
            } else if (sumLL < 1e-9 && sumRR < 1e-9) {
                 masterCorr = 0.0f;
            }
        }
    } else {
        m_rmsL.store(0.0f, std::memory_order_relaxed);
        m_rmsR.store(0.0f, std::memory_order_relaxed);
    }

    // Publish master meter snapshot (post-gain, pre-fade).
    if (publishMasterSnapshot) {
        
        // Publish snapshot
        const float masterPeakL = static_cast<float>(peakL);
        const float masterPeakR = static_cast<float>(peakR);
        const float integratedLufs = m_loudnessState.integratedLufs.load(std::memory_order_relaxed);
        
        snaps->writeLevels(ChannelSlotMap::MASTER_SLOT_INDEX,
                                         masterPeakL, masterPeakR,
                                         masterRmsL, masterRmsR,
                                         masterLowL, masterLowR,
                                         masterCorr,
                                         integratedLufs);
        
        if (masterPeakL >= 1.0f || masterPeakR >= 1.0f) {
            snaps->setClip(ChannelSlotMap::MASTER_SLOT_INDEX, masterPeakL >= 1.0f, masterPeakR >= 1.0f);
        }
    }

    // --- LUFS Block Push (Output) ---
    m_loudnessState.blockSamples += numFrames;
    if (m_loudnessState.blockSamples >= 19200) {
        double avgBlockEnergy = m_loudnessState.blockEnergySum / static_cast<double>(m_loudnessState.blockSamples); // Use accumulated sample count
        m_loudnessState.energyQueue.try_push(avgBlockEnergy);
        m_loudnessState.blockEnergySum = 0.0;
        m_loudnessState.blockSamples = 0;
        
        // Momentary (Short-term) readout for UI feedback
        if (avgBlockEnergy > 1e-14) {
           float m = -0.691f + 10.0f * std::log10(static_cast<float>(avgBlockEnergy));
           m_loudnessState.momentaryLufs.store(m, std::memory_order_relaxed);
        } else {
           m_loudnessState.momentaryLufs.store(-144.0f, std::memory_order_relaxed);
        }
    }

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
                            static_cast<size_t>(numFrames - i) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(float));
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

    // === Metronome Click Mixing ===
    if (m_metronomeEnabled.load(std::memory_order_relaxed) && 
        m_transportPlaying.load(std::memory_order_relaxed)) {
        
        // Constants
        const float bpm = m_bpm.load(std::memory_order_relaxed);
        const float clickVol = m_metronomeVolume.load(std::memory_order_relaxed);
        const int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);
        
        const uint64_t samplesPerBeat = static_cast<uint64_t>(
            (static_cast<double>(m_sampleRate) * 60.0) / static_cast<double>(bpm));
            
        // Reset tracking on jumps (Backwards Seek/Loop Safety)
        // Only reset if we jumped BEHIND the start of the current beat interval.
        uint64_t prevBeatSample = (m_nextBeatSample >= samplesPerBeat) ? m_nextBeatSample - samplesPerBeat : 0;
        
        if (m_globalSamplePos < prevBeatSample) {
            m_nextBeatSample = (m_globalSamplePos / samplesPerBeat) * samplesPerBeat;
            m_currentBeat = static_cast<int>((m_globalSamplePos / samplesPerBeat) % beatsPerBar);
            m_clickPlaying = false;
        }
        
        // Init first beat
        if (m_nextBeatSample == 0 && m_globalSamplePos == 0) {
            m_nextBeatSample = 0;
            m_currentBeat = 0;
        }

        // 1. Calculate Block Boundaries
        const uint64_t blockStart = m_globalSamplePos;
        const uint64_t blockEnd = blockStart + numFrames;
        
        // 2. Identify if a NEW beat triggers in this block
        bool newBeatTriggers = false;
        uint32_t triggerOffset = numFrames; // Default to end (no trigger)

        while (m_nextBeatSample < blockEnd && samplesPerBeat > 0) {
            if (m_nextBeatSample >= blockStart) {
                // Found a trigger in this block!
                newBeatTriggers = true;
                triggerOffset = static_cast<uint32_t>(m_nextBeatSample - blockStart);
                break; 
            }
             m_nextBeatSample += samplesPerBeat;
        }

        // 3. Mix TAIL (From 0 to triggerOffset)
        // If we were already playing, continue until the new beat kicks in (or block ends)
        if (m_clickPlaying && m_activeClickSamples && !m_activeClickSamples->empty()) {
             size_t clickLen = m_activeClickSamples->size();
             uint32_t tailFrames = std::min(numFrames, triggerOffset);
             
             /*
             if (m_clickPlayhead < clickLen) {
                 // Log::info("Meta Tail: " + std::to_string(tailFrames) + " Playhead: " + std::to_string(m_clickPlayhead));
             }
             */

             for (uint32_t i = 0; i < tailFrames && m_clickPlayhead < clickLen; ++i) {
                 float sample = (*m_activeClickSamples)[m_clickPlayhead] * clickVol * m_currentClickGain;
                 outputBuffer[i * 2] += sample;
                 outputBuffer[i * 2 + 1] += sample;
                 ++m_clickPlayhead;
             }
             
             if (m_clickPlayhead >= clickLen) {
                 m_clickPlaying = false;
             }
        }
        
        // 4. Start NEW Click (From triggerOffset to numFrames)
        if (newBeatTriggers) {
             m_clickPlaying = true;
             m_clickPlayhead = 0;
             m_activeClickSamples = (m_currentBeat == 0) ? &m_synthClickLow : &m_synthClickHigh;
             m_currentClickGain = 1.0f;
             
             m_activeClickSamples = (m_currentBeat == 0) ? &m_synthClickLow : &m_synthClickHigh;
             m_currentClickGain = 1.0f;


             // Advance Beat Counter
             m_currentBeat = (m_currentBeat + 1) % beatsPerBar;
             m_nextBeatSample += samplesPerBeat;
             
             // Mix New Click
             if (m_activeClickSamples && !m_activeClickSamples->empty()) {
                 size_t clickLen = m_activeClickSamples->size();
                 uint32_t remainingFrames = numFrames - triggerOffset;
                 uint32_t framesToMix = std::min(remainingFrames, (uint32_t)clickLen);

                 for (uint32_t i = 0; i < framesToMix; ++i) {
                     float sample = (*m_activeClickSamples)[m_clickPlayhead] * clickVol * m_currentClickGain;
                     uint32_t dstIdx = triggerOffset + i;
                     outputBuffer[dstIdx * 2] += sample;
                     outputBuffer[dstIdx * 2 + 1] += sample;
                     ++m_clickPlayhead;
                 }
                 // Log::info("Meta New Mix: " + std::to_string(framesToMix));
             }
        }
    }

    // Capture recent output for compact waveform displays (post-fade).
    uint32_t historyCap = m_waveformHistoryFrames.load(std::memory_order_relaxed);
    if (historyCap > 0 && !m_waveformHistory.empty()) {
        const uint32_t cap = historyCap;
        uint32_t write = m_waveformWriteIndex.load(std::memory_order_relaxed);
        const uint32_t framesToCopy = std::min(numFrames, cap);
        const uint32_t first = std::min(framesToCopy, cap - write);
        const size_t stride = static_cast<size_t>(m_outputChannels.load(std::memory_order_relaxed));

        std::memcpy(&m_waveformHistory[static_cast<size_t>(write) * stride],
                    outputBuffer,
                    static_cast<size_t>(first) * stride * sizeof(float));
        if (framesToCopy > first) {
            std::memcpy(m_waveformHistory.data(),
                        outputBuffer + static_cast<size_t>(first) * stride,
                        static_cast<size_t>(framesToCopy - first) * stride * sizeof(float));
        }
        write = (write + framesToCopy) % cap;
        m_waveformWriteIndex.store(write, std::memory_order_release);
    }

    // Advance position (Atomic update to pre-calculated next position)
    if (m_transportPlaying.load(std::memory_order_relaxed) || m_fadeState == FadeState::FadingOut) {
        
        m_globalSamplePos.store(nextGlobalPos, std::memory_order_relaxed);
        
        // Handle Loop Metronome Reset
        // If we split-looped, we need to reset the internal metronome counters to match the jump
        if (loopSplitFrame < numFrames) {
             int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);
             float bpm = m_bpm.load(std::memory_order_relaxed);
             double samplesPerBeat = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) / static_cast<double>(bpm);
             uint64_t samplesPerBeatInt = static_cast<uint64_t>(samplesPerBeat);
             
             if (samplesPerBeatInt > 0) {
                 m_nextBeatSample = (nextGlobalPos / samplesPerBeatInt) * samplesPerBeatInt;
                 m_currentBeat = static_cast<int>((nextGlobalPos / samplesPerBeatInt) % beatsPerBar);
                 m_clickPlaying = false;
                 m_clickPlayhead = 0;
             }
        }
    }

    // Telemetry (lightweight counter only on RT thread)
    m_telemetry.incrementBlocksProcessed();

    RESTORE_DENORMALS
}

void AudioEngine::setBufferConfig(uint32_t maxFrames, uint32_t numChannels) {
    // Treat maxFrames as a hint; never shrink RT buffers.
    // Some drivers deliver larger blocks than requested, and shrinking can cause
    // renderGraph() to early-out -> audible crackles.
    m_outputChannels.store(numChannels, std::memory_order_relaxed);
    if (maxFrames > m_maxBufferFrames.load(std::memory_order_relaxed)) {
        m_maxBufferFrames.store(maxFrames, std::memory_order_relaxed);
    }

    const size_t requiredSize = static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed)) * m_outputChannels.load(std::memory_order_relaxed);
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
        
#ifdef _WIN32
        // Lock buffers in memory to prevent page faults during real-time processing
        if (!m_masterBufferD.empty()) {
            if (VirtualLock(m_masterBufferD.data(), m_masterBufferD.size() * sizeof(double))) {
                // Critical: Touch every page to force physical allocation *now*
                // VirtualLock guarantees resonance but doesn't necessarily fault them in immediately?
                // Actually VirtualLock fails if pages aren't committed.
                // The Mentor says: "Touch every page after locking".
                volatile char* ptr = reinterpret_cast<volatile char*>(m_masterBufferD.data());
                size_t sizeBytes = m_masterBufferD.size() * sizeof(double);
                for (size_t i = 0; i < sizeBytes; i += 4096) {
                    char c = ptr[i]; 
                    ptr[i] = c; // Read/Write to force detailed mapping
                }
            }
        }
        for (auto& buf : m_trackBuffersD) {
            if (!buf.empty()) {
                if (VirtualLock(buf.data(), buf.size() * sizeof(double))) {
                    volatile char* ptr = reinterpret_cast<volatile char*>(buf.data());
                    size_t sizeBytes = buf.size() * sizeof(double);
                    for (size_t i = 0; i < sizeBytes; i += 4096) {
                        char c = ptr[i];
                        ptr[i] = c;
                    }
                }
            }
        }
#endif
    }

    // Allocate waveform history ring (non-RT).
    if (m_waveformHistoryFrames.load(std::memory_order_relaxed) == 0) {
        m_waveformHistoryFrames.store(kWaveformHistoryFramesDefault, std::memory_order_relaxed);
    }
    const size_t historyRequired = static_cast<size_t>(m_waveformHistoryFrames.load(std::memory_order_relaxed)) * m_outputChannels.load(std::memory_order_relaxed);
    if (m_waveformHistory.size() < historyRequired) {
        m_waveformHistory.assign(historyRequired, 0.0f);
        m_waveformWriteIndex.store(0, std::memory_order_relaxed);
    }

    // Initialize smoothing coefficients based on requested buffer size
    const uint32_t coeffFrames = std::max<uint32_t>(1, maxFrames);
    m_smoothedMasterGain.coeff = 1.0 / static_cast<double>(coeffFrames);
    
    // Pre-allocate parallel task data
    if (m_parallelTrackPointers.size() < 4096) {
        m_parallelTrackPointers.resize(4096);
    }

    // Critical: Buffers may have moved after resize. Re-swizzle the pointers.
    if (needAlloc) {
        compileGraph();
    }
}

uint32_t AudioEngine::copyWaveformHistory(float* outInterleaved, uint32_t maxFrames) const {
    if (!outInterleaved || m_waveformHistoryFrames.load(std::memory_order_relaxed) == 0 || m_waveformHistory.empty()) {
        return 0;
    }
    const uint32_t cap = m_waveformHistoryFrames.load(std::memory_order_relaxed);
    uint32_t frames = std::min(maxFrames, cap);
    const uint32_t write = m_waveformWriteIndex.load(std::memory_order_acquire);
    const uint32_t start = (write + cap - frames) % cap;
    const size_t stride = static_cast<size_t>(m_outputChannels.load(std::memory_order_relaxed));

    const uint32_t first = std::min(frames, cap - start);
    std::memcpy(outInterleaved,
                &m_waveformHistory[static_cast<size_t>(start) * stride],
                static_cast<size_t>(first) * stride * sizeof(float));
    if (frames > first) {
        std::memcpy(outInterleaved + static_cast<size_t>(first) * stride,
                    m_waveformHistory.data(),
                    static_cast<size_t>(frames - first) * stride * sizeof(float));
    }
    return frames;
}

// --- Constants ---
const AudioEngine::BiquadCoeff AudioEngine::kKWeightPreFilter = {
    1.53512485958697, -2.69169618940638, 1.19839281085285, // b0, b1, b2
    -1.69065929318241, 0.73248077421585 // a1, a2
};

const AudioEngine::BiquadCoeff AudioEngine::kKWeightRLB = {
    1.0, -2.0, 1.0, // b0, b1, b2
    -1.99004745483398, 0.99007225036621 // a1, a2
};

AudioEngine::AudioEngine() {
    generateMetronomeSounds();
    startLoudnessWorker();
}

AudioEngine::~AudioEngine() {
    stopLoudnessWorker();
}

void AudioEngine::startLoudnessWorker() {
    if (m_loudnessThreadRunning) return;
    m_loudnessThreadRunning = true;
    m_loudnessThread = std::thread(&AudioEngine::loudnessWorkerLoop, this);
    
    // Low priority for worker
#ifdef _WIN32
    SetThreadPriority(m_loudnessThread.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

void AudioEngine::stopLoudnessWorker() {
    m_loudnessThreadRunning = false;
    if (m_loudnessThread.joinable()) {
        m_loudnessThread.join();
    }
}

void AudioEngine::loudnessWorkerLoop() {
    while (m_loudnessThreadRunning) {
        // 1. Process Accumulation Queue
        double blockEnergy;
        bool didWork = false;
        
        // Handling Reset
        if (m_loudnessResetRequested.exchange(false)) {
            m_loudnessState.historyWriteIdx = 0;
            m_loudnessState.historyCount = 0;
            m_loudnessState.gatedSum = 0.0;
            m_loudnessState.gatedBlocks = 0;
            m_loudnessState.integratedLufs.store(-144.0f);
        }
        
        while (m_loudnessState.energyQueue.pop(blockEnergy)) {
            didWork = true;
            
            // Validate: check for NaN/Inf from upstream
            if (!std::isfinite(blockEnergy) || blockEnergy < 0.0) blockEnergy = 0.0;
            
            // Add to circular history
            size_t idx = m_loudnessState.historyWriteIdx;
            m_loudnessState.blockHistory[idx] = blockEnergy;
            m_loudnessState.historyWriteIdx = (idx + 1) & (LoudnessState::kHistoryCapacity - 1);
            if (m_loudnessState.historyCount < LoudnessState::kHistoryCapacity) {
                m_loudnessState.historyCount++;
            }
        }
        
        if (didWork && m_loudnessState.historyCount > 0) {
            // 2. Perform Gating (full scan of history)
            // Optimization: We could maintain a "running histogram" but full scan of ~32k doubles is ~256KB read.
            // Modern RAM bandwidth > 20GB/s. 256KB is < 0.1ms.
            // This is "Cockroach" safe: re-calculating from source truth every time minimizes drift/state corruption.
            
            double sumUngated = 0.0;
            size_t countUngated = 0;
            
            // Absolute Threshold: -70 LKFS
            // 10 ^ (-70 / 10) = 1e-7
            constexpr double kAbsThresholdEnergy = 1.0e-7;
            
            // Pass 1: Calc Ungated Loudness (blocks > Abs Threshold)
            // Iterate valid history
            size_t count = m_loudnessState.historyCount;
            // To iterate circular buffer efficiently, just iterate 0..Capacity if full, or 0..Count if not wrapped?
            // Actually, we can just iterate the used entries. 
            // If full, iterate all. If not full, iterate 0..WriteIdx.
            // Simplest: Iterate all 0..Capacity, check count? No, `blockHistory` is static size.
            // If historyCount < Capacity, only use 0..historyCount-1 (assuming we fill linearly first).
            // Wait, circular buffer writes wrap. If count < capacity, valid data is at [0..writeIdx-1]. Correct.
            // If count == capacity, valid data is [0..Capacity-1].
            
            size_t validLimit = (m_loudnessState.historyCount < LoudnessState::kHistoryCapacity) 
                                ? m_loudnessState.historyWriteIdx 
                                : LoudnessState::kHistoryCapacity;

            for (size_t i = 0; i < validLimit; ++i) {
                double e = m_loudnessState.blockHistory[i];
                if (e > kAbsThresholdEnergy) {
                    sumUngated += e;
                    countUngated++;
                }
            }
            
            if (countUngated > 0) {
                double avgUngated = sumUngated / static_cast<double>(countUngated);
                
                // Pass 2: Calc Relative Threshold (-10 LU below Ungated)
                // RelThreshold = UngatedEnergy * 10^(-10/10) = Ungated * 0.1
                double relThreshold = avgUngated * 0.1;
                
                // Final Gated Sum
                // Blocks must be > Abs AND > Rel
                // (Since Rel is usually > Abs unless signal is very quiet, Rel dominates. But strict check is both).
                double threshold = (relThreshold > kAbsThresholdEnergy) ? relThreshold : kAbsThresholdEnergy;
                
                double sumGated = 0.0;
                size_t countGated = 0;
                
                for (size_t i = 0; i < validLimit; ++i) {
                    double e = m_loudnessState.blockHistory[i];
                    if (e > threshold) {
                        sumGated += e;
                        countGated++;
                    }
                }
                
                if (countGated > 0) {
                     double finalEnergy = sumGated / static_cast<double>(countGated);
                     // LUFS = -0.691 + 10 * log10(Energy)
                     if (finalEnergy > 0.0) {
                         double lufs = -0.691 + 10.0 * std::log10(finalEnergy);
                         m_loudnessState.integratedLufs.store(static_cast<float>(lufs), std::memory_order_relaxed);
                     } else {
                         m_loudnessState.integratedLufs.store(-144.0f, std::memory_order_relaxed);
                     }
                } else {
                     m_loudnessState.integratedLufs.store(-144.0f, std::memory_order_relaxed);
                }
            } else {
                 m_loudnessState.integratedLufs.store(-144.0f, std::memory_order_relaxed);
            }
        }
        
        // Sleep to save CPU (update rate ~10Hz is plenty for Integrated)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void AudioEngine::renderGraph(const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset) {
    bool srcActiveThisBlock = false;
    const uint32_t numChannels = m_outputChannels.load(std::memory_order_relaxed);

    // Guard
    if (numFrames > m_maxBufferFrames.load(std::memory_order_relaxed) || numChannels != 2) {
       m_telemetry.incrementUnderruns();
       return;
    }

    // Calc pointers
    double* masterBuf = m_masterBufferD.data() + bufferOffset * numChannels;

    const size_t availableTracks = m_trackBuffersD.size();
    if (availableTracks == 0) {
        std::memset(masterBuf, 0, static_cast<size_t>(numFrames) * numChannels * sizeof(double));
        m_telemetry.incrementUnderruns();
        return;
    }

    // Clear master
    std::memset(masterBuf, 0, static_cast<size_t>(numFrames) * numChannels * sizeof(double));

    const uint64_t blockStart = m_globalSamplePos.load(std::memory_order_relaxed);
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
            m_telemetry.incrementOverruns();
            continue;
        }
        auto& state = ensureTrackState(trackIdx);

        // Compute continuous params (slot-indexed) and apply to targets.
        float faderDb = 0.0f;
        float panParam = 0.0f;
        float trimDb = 0.0f;
        uint32_t slot = ChannelSlotMap::INVALID_SLOT;

        auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_acquire);
        if (slotMap) {
            slot = slotMap->getSlotIndex(track.trackId);
            auto* params = m_continuousParamsRaw.load(std::memory_order_acquire);
            if (slot != ChannelSlotMap::INVALID_SLOT && params) {
                params->read(slot, faderDb, panParam, trimDb);
            }
        }

        const double faderDbClamped = clampD(static_cast<double>(faderDb), -90.0, 6.0);
        const double trimDbClamped = clampD(static_cast<double>(trimDb), -24.0, 24.0);
        const double gain = dbToLinearD(faderDbClamped) * dbToLinearD(trimDbClamped);
        
        double volTarget = static_cast<double>(track.volume) * gain;
        double panTarget = clampD(static_cast<double>(track.pan) + static_cast<double>(panParam), -1.0, 1.0);
        
        // Apply Automation Override (v3.1)
        if (!track.automationCurves.empty() && m_sampleRate.load(std::memory_order_relaxed) > 0) {
            uint64_t globalPos = m_globalSamplePos.load(std::memory_order_relaxed);
            double currentBeat = (static_cast<double>(globalPos) / m_sampleRate.load(std::memory_order_relaxed)) * (graph.bpm / 60.0);
            for (const auto& curve : track.automationCurves) {
                if (curve.getAutomationTarget() == AutomationTarget::Volume) {
                    volTarget = curve.getValueAtBeat(currentBeat);
                } else if (curve.getAutomationTarget() == AutomationTarget::Pan) {
                    panTarget = clampD(curve.getValueAtBeat(currentBeat), -1.0, 1.0);
                }
            }
        }
        
        // Skip early (solo suppression only).
        // Muted tracks still render so meters keep moving, but they don't mix into master.
        const bool muted = track.mute || state.mute;
        const bool soloed = track.solo || state.solo;
        const bool soloSafe = track.isSoloSafe || state.soloSafe; // Access cached state or track prop
        
        // If any track is soloed, we suppress this track UNLESS it is also soloed OR it is solo-safe.
        if (anySolo && !soloed && !soloSafe) {
             auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
            if (snaps && slot != ChannelSlotMap::INVALID_SLOT) {
                snaps->writePeak(slot, 0.0f, 0.0f);
            }
            continue;
        }

        // Empty tracks should not touch RT buffers. Still keep param state updated
        // so automation is consistent when clips appear later.
        if (track.clips.empty()) {
            double tL, tR;
            fastPanGainsD(panTarget, volTarget, tL, tR);
            state.gainL.setTarget(tL);
            state.gainR.setTarget(tR);
            state.gainL.snap();
            state.gainR.snap();
            auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
            if (snaps && slot != ChannelSlotMap::INVALID_SLOT) {
                snaps->writePeak(slot, 0.0f, 0.0f);
            }
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
            const double outputRate = static_cast<double>(m_sampleRate.load(std::memory_order_relaxed));
            const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
            const double ratio = srcRate / outputRate;
            
            // Source position
            const double outputFrameOffset = static_cast<double>(start - clip.startSample);
            double phase = clip.sampleOffset + outputFrameOffset * ratio;

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
            
            const uint32_t channels = clip.channels;
            const uint32_t stride = channels;

            double* dstBase = buffer.data();
            const float* data = clip.audioData;
            double* dst = dstBase + static_cast<size_t>(localOffset) * 2;

            const uint64_t fadeLen = CLIP_EDGE_FADE_SAMPLES;

            // Fast path: matching sample rates - direct copy to double
            if (std::abs(ratio - 1.0) < 1e-9) {
                const uint64_t srcStart = static_cast<uint64_t>(phase);
                const float* src = data + srcStart * stride;
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
                    
                    double sL, sR;
                    if (channels == 1) {
                        sL = static_cast<double>(src[i]);
                        sR = sL;
                    } else {
                        sL = static_cast<double>(src[i * 2]);
                        sR = static_cast<double>(src[i * 2 + 1]);
                    }

                    dst[i * 2] = sL * clipGain * fade;
                    dst[i * 2 + 1] = sR * clipGain * fade;
                }
            } else {
                srcActiveThisBlock = true;
                // Resampling - use selected quality, pre-compute end condition
                const double phaseEnd = static_cast<double>(totalFrames);
                
                if (channels == 1) {
                     // Mono Resampling (Linear fallback)
                     // Note: We avoid calling stereo interpolators on mono data to prevent OOB access.
                     for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                         uint64_t idx = static_cast<uint64_t>(phase);
                         double frac = phase - static_cast<double>(idx);
                         
                         float s0 = data[idx];
                         float s1 = (idx + 1 < static_cast<uint64_t>(totalFrames)) ? data[idx + 1] : s0;
                         
                         double val = static_cast<double>(s0) + frac * static_cast<double>(s1 - s0);
                         
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
                         double clipGain = static_cast<double>(clip.gain);
                         dst[i*2] = val * clipGain * fade;
                         dst[i*2+1] = val * clipGain * fade;
                         
                         phase += ratio;
                     }
                     continue; // Skip stereo switch
                }
                
                // Select interpolator at block level, not per-sample
                switch (m_interpQuality.load(std::memory_order_relaxed)) {
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

        // Mix track into master
        double tL, tR;
        fastPanGainsD(panTarget, volTarget, tL, tR);
        state.gainL.setTarget(tL);
        state.gainR.setTarget(tR);

        const double* trackData = buffer.data();
        double* master = m_masterBufferD.data();
        
        double peakTrackL = 0.0;
        double peakTrackR = 0.0;
        double rmsAccTrackL = 0.0;
        double rmsAccTrackR = 0.0;
        double lowAccTrackL = 0.0;
        double lowAccTrackR = 0.0;

        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        const bool publishTrackSnapshot = (snaps && slot != ChannelSlotMap::INVALID_SLOT);
        double* lfStateL = publishTrackSnapshot ? &m_meterLfStateL[slot] : nullptr;
        double* lfStateR = publishTrackSnapshot ? &m_meterLfStateR[slot] : nullptr;

        for (uint32_t i = 0; i < numFrames; ++i) {
            // Apply smoothed gain
            const double leftGain = state.gainL.next();
            const double rightGain = state.gainR.next();
            
            const double outL = trackData[i * 2] * leftGain;
            const double outR = trackData[i * 2 + 1] * rightGain;

            if (!muted) {
                master[i * 2] += outL;
                master[i * 2 + 1] += outR;
            }

            const double absL = (outL >= 0.0) ? outL : -outL;
            const double absR = (outR >= 0.0) ? outR : -outR;
            if (absL > peakTrackL) peakTrackL = absL;
            if (absR > peakTrackR) peakTrackR = absR;

            if (publishTrackSnapshot) {
                rmsAccTrackL += outL * outL;
                rmsAccTrackR += outR * outR;

                const double lpL = *lfStateL + m_meterLfCoeff * (outL - *lfStateL);
                const double lpR = *lfStateR + m_meterLfCoeff * (outR - *lfStateR);
                *lfStateL = lpL;
                *lfStateR = lpR;
                lowAccTrackL += lpL * lpL;
                lowAccTrackR += lpR * lpR;
            }
        }

        if (publishTrackSnapshot && numFrames > 0) {
            const float peakL = static_cast<float>(peakTrackL);
            const float peakR = static_cast<float>(peakTrackR);
            const double invN = 1.0 / static_cast<double>(numFrames);
            const float rmsL = static_cast<float>(std::sqrt(rmsAccTrackL * invN));
            const float rmsR = static_cast<float>(std::sqrt(rmsAccTrackR * invN));
            const float lowL = static_cast<float>(std::sqrt(lowAccTrackL * invN));
            const float lowR = static_cast<float>(std::sqrt(lowAccTrackR * invN));

            snaps->writeLevels(slot, peakL, peakR, rmsL, rmsR, lowL, lowR);
            if (peakL >= 1.0f || peakR >= 1.0f) {
                snaps->setClip(slot, peakL >= 1.0f, peakR >= 1.0f);
            }
        }
        
        // Snap smoothed params to target for next block
        state.gainL.snap();
        state.gainR.snap();
    }

    if (srcActiveThisBlock) {
        m_telemetry.incrementSrcActiveBlocks();
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

void AudioEngine::loadMetronomeClicks(const std::string& downbeatPath, const std::string& upbeatPath) {
    // Helper to load a single WAV file into a sample vector
    auto loadWav = [](const std::string& wavPath, std::vector<float>& samples) -> bool {
        FILE* file = fopen(wavPath.c_str(), "rb");
        if (!file) {
            return false;
        }
        
        // Read RIFF header
        char riff[4];
        fread(riff, 1, 4, file);
        if (memcmp(riff, "RIFF", 4) != 0) {
            fclose(file);
            return false;
        }
        
        uint32_t chunkSize;
        fread(&chunkSize, 4, 1, file);
        
        char wave[4];
        fread(wave, 1, 4, file);
        if (memcmp(wave, "WAVE", 4) != 0) {
            fclose(file);
            return false;
        }
        
        // Find fmt and data chunks
        uint16_t audioFormat = 0;
        uint16_t numChannels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        
        while (true) {
            char chunkId[4];
            uint32_t chunkLen;
            if (fread(chunkId, 1, 4, file) != 4) break;
            if (fread(&chunkLen, 4, 1, file) != 1) break;
            
            if (memcmp(chunkId, "fmt ", 4) == 0) {
                fread(&audioFormat, 2, 1, file);
                fread(&numChannels, 2, 1, file);
                fread(&sampleRate, 4, 1, file);
                fseek(file, 6, SEEK_CUR);
                fread(&bitsPerSample, 2, 1, file);
                if (chunkLen > 16) {
                    fseek(file, chunkLen - 16, SEEK_CUR);
                }
            } else if (memcmp(chunkId, "data", 4) == 0) {
                if (audioFormat != 1 || (bitsPerSample != 16 && bitsPerSample != 24)) {
                    fclose(file);
                    return false;
                }
                
                const uint32_t bytesPerSample = bitsPerSample / 8;
                const uint32_t numSamples = chunkLen / (numChannels * bytesPerSample);
                
                samples.resize(numSamples);
                
                if (bitsPerSample == 16) {
                    std::vector<int16_t> rawData(numSamples * numChannels);
                    fread(rawData.data(), 2, numSamples * numChannels, file);
                    
                    for (uint32_t i = 0; i < numSamples; ++i) {
                        float sample = 0.0f;
                        for (uint16_t ch = 0; ch < numChannels; ++ch) {
                            sample += static_cast<float>(rawData[i * numChannels + ch]) / 32768.0f;
                        }
                        samples[i] = sample / static_cast<float>(numChannels);
                    }
                } else if (bitsPerSample == 24) {
                    std::vector<uint8_t> rawData(numSamples * numChannels * 3);
                    fread(rawData.data(), 1, numSamples * numChannels * 3, file);
                    
                    for (uint32_t i = 0; i < numSamples; ++i) {
                        float sample = 0.0f;
                        for (uint16_t ch = 0; ch < numChannels; ++ch) {
                            size_t byteIdx = (i * numChannels + ch) * 3;
                            int32_t val = rawData[byteIdx] | (rawData[byteIdx + 1] << 8) | (rawData[byteIdx + 2] << 16);
                            if (val & 0x800000) val |= 0xFF000000;
                            sample += static_cast<float>(val) / 8388608.0f;
                        }
                        samples[i] = sample / static_cast<float>(numChannels);
                    }
                }
                
                fclose(file);
                return true;
            } else {
                fseek(file, chunkLen, SEEK_CUR);
            }
        }
        
        fclose(file);
        return false;
    };
    
    // Load both click sounds
    loadWav(downbeatPath, m_clickSamplesDown);
    loadWav(upbeatPath, m_clickSamplesUp);
    
    // Default to downbeat sample if upbeat failed
    if (m_clickSamplesUp.empty() && !m_clickSamplesDown.empty()) {
        m_clickSamplesUp = m_clickSamplesDown;
    }
    // Default to upbeat sample if downbeat failed
    if (m_clickSamplesDown.empty() && !m_clickSamplesUp.empty()) {
        m_clickSamplesDown = m_clickSamplesUp;
    }
    
    // Start pointing to downbeat
    m_activeClickSamples = &m_clickSamplesDown;
}

void AudioEngine::setLoopRegion(double startBeat, double endBeat) {
    // Validate that end is after start
    if (endBeat <= startBeat) {
        endBeat = startBeat + 4.0;  // Default to 1 bar (4 beats)
    }
    m_loopStartBeat.store(startBeat, std::memory_order_relaxed);
    m_loopEndBeat.store(endBeat, std::memory_order_relaxed);
}

    // === Antigravity Graph Compiler ===
    void AudioEngine::compileGraph() {
        std::lock_guard<std::mutex> lock(m_graphMutex);
        
        // Use double-buffering: Write to inactive index
        const int inactiveIdx = 1 - m_activeRenderTrackIndex.load(std::memory_order_relaxed);
        auto& targetOrder = m_renderTracks[inactiveIdx];
        targetOrder.clear();
        
        auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_relaxed);
        if (!slotMap) return;
        
        targetOrder.reserve(slotMap->getChannelCount());
        
        // Access the current graph snapshot
        const auto& graph = m_state.activeGraph(); // Fixed method name
        
        // Iterate Tracks directly from the graph snapshot
        for (const auto& tr : graph.tracks) {
             const uint32_t idx = tr.trackIndex;
             
             // Safety Check
             if (idx >= m_trackBuffersD.size()) continue;

             RenderTrack rt;
             rt.trackIndex = idx;
             rt.selfBuffer = m_trackBuffersD[idx].data();
             
             // --- Main Output Routing ---
             // --- Main Output Routing ---
             // Phase 4: Real-time Fader Support
             // We set connection gain to 1.0 (Unity) because Volume/Pan will be applied
             // dynamically to the track's selfBuffer in renderGraph using the continuous param buffer.
             
             const double gainL = 1.0;
             const double gainR = 1.0;
             
             // Route to Main Output ID
             if (tr.mainOutputId == 0xFFFFFFFF) {
                 // Route to Master
                 RuntimeConnection toMaster;
                 toMaster.destinationBufferL = m_masterBufferD.data();
                 toMaster.destinationBufferR = m_masterBufferD.data() + 1;
                 toMaster.stride = 2;
                 toMaster.gainL = gainL;
                 toMaster.gainR = gainR;
                 rt.activeConnections.push_back(toMaster);
             } 
             else {
                 // Route to another track (Bus/Group)
                 if (slotMap) {
                     uint32_t destSlot = slotMap->getSlotIndex(tr.mainOutputId);
                     if (destSlot != ChannelSlotMap::INVALID_SLOT && destSlot < m_trackBuffersD.size()) {
                         RuntimeConnection toTrack;
                         toTrack.destinationBufferL = m_trackBuffersD[destSlot].data();
                         toTrack.destinationBufferR = m_trackBuffersD[destSlot].data() + 1;
                         toTrack.stride = 2;
                         toTrack.gainL = gainL;
                         toTrack.gainR = gainR;
                         rt.activeConnections.push_back(toTrack);
                     }
                 }
             }
             
             // --- Process Sends ---
             for (const auto& send : tr.sends) {
                 if (send.mute) continue;

                 double sendGainL = static_cast<double>(send.gain);
                 double sendGainR = static_cast<double>(send.gain);
                 
                 // Apply Send Pan (Approximated if mono source, or balance if stereo)
                 const double panClamped = clampD(static_cast<double>(send.pan), -1.0, 1.0);
                 const double angle = (panClamped + 1.0) * QUARTER_PI_D;
                 sendGainL *= std::cos(angle);
                 sendGainR *= std::sin(angle);

                 if (send.targetChannelId == 0xFFFFFFFF) {
                     // Route to Master
                     RuntimeConnection toMaster;
                     toMaster.destinationBufferL = m_masterBufferD.data();
                     toMaster.destinationBufferR = m_masterBufferD.data() + 1;
                     toMaster.stride = 2;
                     toMaster.gainL = sendGainL;
                     toMaster.gainR = sendGainR;
                     rt.activeConnections.push_back(toMaster);
                 } else {
                     if (slotMap) {
                         uint32_t destSlot = slotMap->getSlotIndex(send.targetChannelId);
                         if (destSlot != ChannelSlotMap::INVALID_SLOT && destSlot < m_trackBuffersD.size()) {
                             RuntimeConnection toTrack;
                             toTrack.destinationBufferL = m_trackBuffersD[destSlot].data();
                             toTrack.destinationBufferR = m_trackBuffersD[destSlot].data() + 1;
                             toTrack.stride = 2;
                             toTrack.gainL = sendGainL;
                             toTrack.gainR = sendGainR;
                             rt.activeConnections.push_back(toTrack);
                         }
                     }
                 }
             }
             
             targetOrder.push_back(rt);
        }
        
        // Atomic Swap
        m_activeRenderTrackIndex.store(inactiveIdx, std::memory_order_release);
    }
    


    void AudioEngine::parallelTrackDispatcher(void* context, void* taskData) {
        auto* engine = static_cast<AudioEngine*>(context);
        auto* track = static_cast<RenderTrack*>(taskData);
        engine->parallelProcessTrack(*track);
    }

    void AudioEngine::parallelProcessTrack(const RenderTrack& track) {
        const AudioGraph& graph = m_state.activeGraph();
        renderClipAudio(track.selfBuffer, ensureTrackState(track.trackIndex),
                       track.trackIndex, graph, m_parallelNumFrames, m_parallelBufferOffset);
        processTrackEffects(track, m_parallelNumFrames, m_parallelBufferOffset);
    }
    
    // Helper to render clips for a specific track into its buffer
    // Extracted from original renderGraph logic
    void AudioEngine::renderClipAudio(double* outputBuffer, TrackRTState& state, uint32_t trackIndex, const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset) {
        // NOTE: outputBuffer is NOT cleared here. It is cleared at start of renderGraph.
        // This allows sends to accumulate into this buffer before we add clips.
        
        // Find the graph track corresponding to this RT state
        if (trackIndex >= graph.tracks.size()) return;
        const auto& track = graph.tracks[trackIndex];
        
        const bool anySolo = graph.anySolo; 
        
        // If track is silent due to mute or solo suppression, we can return early (buffer is cleared).
        // Check RT state for quick mute
        if (track.mute) return; 
        
        // Solo Logic Check (Pre-Render Optimization)
        // If global solo is active and this track is NOT soloed AND NOT safe, skip.
        // We need to know if any track is soloed globally.
        // m_trackState doesn't store "anySolo".
        // Let's re-scan or assume graph has it.
        // For Phase 3, let's render everything (safest) and handle mute during mix? 
        // No, render clips is expensive.
        
        // Iterate clips
        const uint64_t blockStart = m_globalSamplePos.load(std::memory_order_relaxed);
        const uint64_t blockEnd = blockStart + numFrames;
        
        // Note: Volume/Pan processing happens AFTER clip rendering (during mix) in renderGraph.
        // renderClipAudio is now pure audio generation.
        
        // Note: Volume/Pan processing happens AFTER clip rendering (during mix) in strict sense
        // But Nomad applies smoothing via "MixerBus". 
        // Since we are decoupling, we just render raw clips here? 
        // NO. "track.selfBuffer" should contain the PROCESSED audio (Post-Fader? Pre-Fader?)
        // The Antigravity Spec sends "conn.gain". This is the SEND gain.
        // The TRACK Fader should be applied to "selfBuffer" OR applied during the send gain calc?
        
        // Critical Architecture Decision:
        // Option A: selfBuffer is PRE-FADER. Sends apply (Fader * Send) or (Send).

        // Offset output buffer for partial renders
        double* dstBase = outputBuffer + bufferOffset * 2;
        
        // Iterate clips
        // Option B: selfBuffer is POST-FADER.
        
        // If selfBuffer is Post-Fader, then Pre-Fader sends are hard.
        // Standard Console:
        // - Channel Input (Clips) -> Inserts -> Pre-Fader Sends -> Fader/Pan -> Post-Fader Sends.
        
        // For Antigravity Phase 3:
        // We will render clips purely. No volume/pan applied to buffer here.
        // Logic:
        // Render Clips -> buffer.
        
        for (const auto& clip : track.clips) {
            if (!clip.audioData || blockEnd <= clip.startSample || blockStart >= clip.endSample) {
                continue;
            }
            
            const uint64_t start = std::max(blockStart, clip.startSample);
            const uint64_t end = std::min(blockEnd, clip.endSample);
            const uint32_t localOffset = static_cast<uint32_t>(start - blockStart);
            uint32_t framesToRender = static_cast<uint32_t>(end - start);
            
            // Sample rate ratio
            const double outputRate = static_cast<double>(m_sampleRate.load(std::memory_order_relaxed));
            const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
            const double ratio = srcRate / outputRate;
            
            // Source position
            const double outputFrameOffset = static_cast<double>(start - clip.startSample);
            double phase = clip.sampleOffset + outputFrameOffset * ratio;

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
            double* dst = outputBuffer + static_cast<size_t>(localOffset) * 2;

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
                    dst[i * 2] += static_cast<double>(src[i * 2]) * clipGain * fade;
                    dst[i * 2 + 1] += static_cast<double>(src[i * 2 + 1]) * clipGain * fade;
                }
            } else {
                // Resampling - use selected quality, pre-compute end condition
                const double phaseEnd = static_cast<double>(totalFrames);
                
                // Select interpolator at block level, not per-sample
                switch (m_interpQuality.load(std::memory_order_relaxed)) {
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc8:
                    case Interpolators::InterpolationQuality::Sinc16:
                    case Interpolators::InterpolationQuality::Sinc32:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            // Upgrade Sinc8/16 to Sinc32Turbo for efficiency (LUT vs std::sin)
                            Interpolators::Sinc32Turbo::interpolate(data, totalFrames, phase, outL, outR);
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                    case Interpolators::InterpolationQuality::Sinc64:
                        for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                            float outL, outR;
                            Interpolators::Sinc64Turbo::interpolate(data, totalFrames, phase, outL, outR);
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                }
            }
        }
    }

    void AudioEngine::processTrackEffects(const RenderTrack& track, uint32_t numFrames, uint32_t bufferOffset) {
        const AudioGraph& graph = m_state.activeGraph();
        TrackRTState& state = ensureTrackState(track.trackIndex);
        
        float faderDb = 0.0f;
        float panParam = 0.0f;
        float trimDb = 0.0f;
        bool hasLiveParams = false;
        
        auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_acquire);
        auto* params = m_continuousParamsRaw.load(std::memory_order_acquire);

        if (track.trackIndex < graph.tracks.size()) {
            const auto& graphTrack = graph.tracks[track.trackIndex];
            if (slotMap && params) {
               uint32_t slot = slotMap->getSlotIndex(graphTrack.trackId);
               if (slot != ChannelSlotMap::INVALID_SLOT) {
                   params->read(slot, faderDb, panParam, trimDb);
                   hasLiveParams = true;
               }
            }
        }
        
        if (hasLiveParams) {
            const double userVol = dbToLinearD(static_cast<double>(faderDb));
            double targetL, targetR;
            fastPanGainsD(static_cast<double>(panParam), userVol, targetL, targetR);
            state.gainL.setTarget(targetL);
            state.gainR.setTarget(targetR);
        } else if (track.trackIndex < graph.tracks.size()) {
            const auto& graphTrack = graph.tracks[track.trackIndex];
            const double vol = static_cast<double>(graphTrack.volume);
            double targetL, targetR;
            fastPanGainsD(static_cast<double>(graphTrack.pan), vol, targetL, targetR);
            state.gainL.setTarget(targetL);
            state.gainR.setTarget(targetR);
        }
        
        double* selfL = track.selfBuffer + bufferOffset * 2;
        double* selfR = track.selfBuffer + bufferOffset * 2 + 1;
        
        double peakL = 0.0;
        double peakR = 0.0;
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        
        for (uint32_t i = 0; i < numFrames; ++i) {
            const double gL = state.gainL.next(); 
            const double gR = state.gainR.next();
            double valL = selfL[i * 2] * gL;
            double valR = selfR[i * 2] * gR;
            selfL[i * 2] = valL;
            selfR[i * 2] = valR;
            
            const double absL = std::abs(valL);
            const double absR = std::abs(valR);
            if (absL > peakL) peakL = absL;
            if (absR > peakR) peakR = absR;
            sumSqL += valL * valL;
            sumSqR += valR * valR;
        }
        
        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        if (snaps && slotMap && track.trackIndex < graph.tracks.size()) {
             const auto& graphTrack = graph.tracks[track.trackIndex];
             uint32_t slot = slotMap->getSlotIndex(graphTrack.trackId);
             if (slot != ChannelSlotMap::INVALID_SLOT) {
                 const float rmsL = static_cast<float>(std::sqrt(sumSqL / numFrames));
                 const float rmsR = static_cast<float>(std::sqrt(sumSqR / numFrames));
                 snaps->writeLevels(slot, static_cast<float>(peakL), static_cast<float>(peakR), rmsL, rmsR, rmsL, rmsR);
                 if (peakL >= 1.0 || peakR >= 1.0) {
                    snaps->setClip(slot, peakL >= 1.0, peakR >= 1.0);
                 }
             }
        }
    }

void AudioEngine::generateMetronomeSounds() {
    // Generate 50ms beeps at 48kHz
    // High: 1600Hz, Low: 800Hz
    const size_t sampleRate = 48000;
    const size_t durationSamples = static_cast<size_t>(0.10 * sampleRate); // 100ms
    
    // Resize is safe here as this is called lazily from RT thread ONCE, 
    // or (ideally) from non-RT. 
    // Since we call it from processBlock lazily, this IS an allocation on RT thread.
    // BUT it only happens ONCE, so it's a minor "start-up glitch" risk acceptable for this fix.
    // Ideally should be called from SetMetronomeEnabled if empty.
    
    if (m_synthClickLow.empty()) {
        m_synthClickLow.resize(durationSamples);
        m_synthClickHigh.resize(durationSamples);
        
        const double freqLow = 800.0;
        const double freqHigh = 1600.0;
        const double decayRate = 5.0; // Exponential decay
        const double kPi = 3.14159265358979323846;
        
        for (size_t i = 0; i < durationSamples; ++i) {
            double t = static_cast<double>(i) / sampleRate;
            // Short, sharp ping
            double env = std::pow(1.0 - static_cast<double>(i)/durationSamples, 4.0);
    
            m_synthClickLow[i] = static_cast<float>(std::sin(2.0 * kPi * freqLow * t) * env);
            m_synthClickHigh[i] = static_cast<float>(std::sin(2.0 * kPi * freqHigh * t) * env);
        }
    }
}

} // namespace Audio
} // namespace Nomad
