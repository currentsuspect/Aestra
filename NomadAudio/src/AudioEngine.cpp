// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace Nomad {
namespace Audio {

namespace {
    inline double clampD(double v, double lo, double hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    inline double dbToLinearD(double db) {
        // UI uses -90 dB as "silence"
        if (db <= -90.0) return 0.0;
        return std::pow(10.0, db / 20.0);
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
            case AudioQueueCommandType::SetTrackVolume: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.currentVolume = cmd.value1;
                
                // Recalculate Targets
                const double panClamped = clampD(static_cast<double>(state.currentPan), -1.0, 1.0);
                const double angle = (panClamped + 1.0) * QUARTER_PI_D;
                const double vol = static_cast<double>(state.currentVolume);
                
                state.gainL.setTarget(vol * std::cos(angle));
                state.gainR.setTarget(vol * std::sin(angle));
                break;
            }
            case AudioQueueCommandType::SetTrackPan: {
                auto& state = ensureTrackState(cmd.trackIndex);
                state.currentPan = cmd.value1;
                
                // Recalculate Targets
                const double panClamped = clampD(static_cast<double>(state.currentPan), -1.0, 1.0);
                const double angle = (panClamped + 1.0) * QUARTER_PI_D;
                const double vol = static_cast<double>(state.currentVolume);
                
                state.gainL.setTarget(vol * std::cos(angle));
                state.gainR.setTarget(vol * std::sin(angle));
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

void AudioEngine::processBlock(float* outputBuffer,
                               const float* inputBuffer,
                               uint32_t numFrames,
                               double streamTime) {
    (void)inputBuffer;
    (void)streamTime;

    if (!outputBuffer || numFrames == 0) {
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

    // Proper loop handling is done later in processBlock() at lines 433-454
    // No hardcoded timeline wrapping - let the loop system handle it
    if (!m_masterBufferD.empty() && (m_transportPlaying.load(std::memory_order_relaxed) || m_fadeState == FadeState::FadingOut)) {
        renderGraph(graph, numFrames);
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
    
    const double* src = m_masterBufferD.data();
    auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_acquire);
    const bool publishMasterSnapshot = (snaps != nullptr);
    double& masterLfStateL = m_meterLfStateL[ChannelSlotMap::MASTER_SLOT_INDEX];
    double& masterLfStateR = m_meterLfStateR[ChannelSlotMap::MASTER_SLOT_INDEX];
    
    const bool safety = m_safetyProcessingEnabled.load(std::memory_order_relaxed);
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
        }
        
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
    if (numFrames > 0) {
        const double invN = 1.0 / static_cast<double>(numFrames);
        masterRmsL = static_cast<float>(std::sqrt(rmsAccL * invN));
        masterRmsR = static_cast<float>(std::sqrt(rmsAccR * invN));
        m_rmsL.store(masterRmsL, std::memory_order_relaxed);
        m_rmsR.store(masterRmsR, std::memory_order_relaxed);
        if (publishMasterSnapshot) {
            masterLowL = static_cast<float>(std::sqrt(lowAccL * invN));
            masterLowR = static_cast<float>(std::sqrt(lowAccR * invN));
        }
    } else {
        m_rmsL.store(0.0f, std::memory_order_relaxed);
        m_rmsR.store(0.0f, std::memory_order_relaxed);
    }

    // Publish master meter snapshot (post-gain, pre-fade; good enough for current UI checkpoint).
    if (publishMasterSnapshot) {
        const float masterPeakL = static_cast<float>(peakL);
        const float masterPeakR = static_cast<float>(peakR);
        snaps->writeLevels(ChannelSlotMap::MASTER_SLOT_INDEX,
                                         masterPeakL, masterPeakR,
                                         masterRmsL, masterRmsR,
                                         masterLowL, masterLowR);
        if (masterPeakL >= 1.0f || masterPeakR >= 1.0f) {
            snaps->setClip(ChannelSlotMap::MASTER_SLOT_INDEX, masterPeakL >= 1.0f, masterPeakR >= 1.0f);
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
        m_transportPlaying.load(std::memory_order_relaxed) && (!m_clickSamplesDown.empty() || !m_clickSamplesUp.empty())) {
        
        const float bpm = m_bpm.load(std::memory_order_relaxed);
        const float clickVol = m_metronomeVolume.load(std::memory_order_relaxed);
        const int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);
        
        // Samples per beat = sampleRate * 60 / BPM
        const uint64_t samplesPerBeat = static_cast<uint64_t>(
            (static_cast<double>(m_sampleRate) * 60.0) / static_cast<double>(bpm));
        
        // Reset beat tracking if we jumped backwards (loop or seek)
        if (m_globalSamplePos < m_nextBeatSample && m_nextBeatSample > samplesPerBeat) {
            m_nextBeatSample = (m_globalSamplePos / samplesPerBeat) * samplesPerBeat;
            m_currentBeat = static_cast<int>((m_globalSamplePos / samplesPerBeat) % beatsPerBar);
            m_clickPlaying = false;
            m_clickPlayhead = 0;
        }
        
        // Initialize next beat position if needed
        if (m_nextBeatSample == 0 && m_globalSamplePos == 0) {
            m_nextBeatSample = 0;  // First beat at position 0
            m_currentBeat = 0;     // Start on downbeat
            m_activeClickSamples = &m_clickSamplesDown;  // First beat uses downbeat sample
            m_currentClickGain = 1.0f;
            m_clickPlaying = true;
            m_clickPlayhead = 0;
        }
        
        // Check if a new beat starts in this block
        const uint64_t blockEnd = m_globalSamplePos + numFrames;
        while (m_nextBeatSample < blockEnd && samplesPerBeat > 0) {
            if (m_nextBeatSample >= m_globalSamplePos) {
                // Start new click - select correct sample based on beat position
                m_clickPlaying = true;
                m_clickPlayhead = 0;
                // Beat 0 = downbeat (low pitch), others = upbeat (high pitch)
                m_activeClickSamples = (m_currentBeat == 0) ? &m_clickSamplesDown : &m_clickSamplesUp;
                m_currentClickGain = 1.0f;
                // Advance beat counter AFTER selecting sample
                m_currentBeat = (m_currentBeat + 1) % beatsPerBar;
            } else {
                // Beat already passed, just advance counter
                m_currentBeat = (m_currentBeat + 1) % beatsPerBar;
            }
            m_nextBeatSample += samplesPerBeat;
        }
        
        // Mix click samples into output
        if (m_clickPlaying && m_activeClickSamples && !m_activeClickSamples->empty()) {
            const size_t clickLen = m_activeClickSamples->size();
            for (uint32_t i = 0; i < numFrames && m_clickPlayhead < clickLen; ++i) {
                float sample = (*m_activeClickSamples)[m_clickPlayhead] * clickVol * m_currentClickGain;
                outputBuffer[i * 2] += sample;      // Left
                outputBuffer[i * 2 + 1] += sample;  // Right (mono click to stereo)
                ++m_clickPlayhead;
            }
            if (m_clickPlayhead >= clickLen) {
                m_clickPlaying = false;
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

    // Advance position when playing OR when fading out (so audio continues during fade)
    // This prevents the same samples from being rendered repeatedly during fade-out → no buzz
    if (m_transportPlaying.load(std::memory_order_relaxed) || m_fadeState == FadeState::FadingOut) {
        uint64_t currentGlobalPos = m_globalSamplePos.load(std::memory_order_relaxed);
        currentGlobalPos += numFrames;
        
        // Loop handling: jump back to loop start when exceeding loop end
        if (m_loopEnabled.load(std::memory_order_relaxed)) {
            double loopEndBeat = m_loopEndBeat.load(std::memory_order_relaxed);
            double loopStartBeat = m_loopStartBeat.load(std::memory_order_relaxed);
            float bpm = m_bpm.load(std::memory_order_relaxed);
            
            // Convert loop end beat to sample position
            double samplesPerBeat = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) / static_cast<double>(bpm);
            uint64_t loopEndSample = static_cast<uint64_t>(loopEndBeat * samplesPerBeat);
            uint64_t loopStartSample = static_cast<uint64_t>(loopStartBeat * samplesPerBeat);
            
            if (currentGlobalPos >= loopEndSample && loopEndSample > loopStartSample) {
                currentGlobalPos = loopStartSample;
                
                // Reset metronome beat tracking for loop jump
                int beatsPerBar = m_beatsPerBar.load(std::memory_order_relaxed);
                uint64_t samplesPerBeatInt = static_cast<uint64_t>(samplesPerBeat);
                m_nextBeatSample = (currentGlobalPos / samplesPerBeatInt) * samplesPerBeatInt;
                m_currentBeat = static_cast<int>((currentGlobalPos / samplesPerBeatInt) % beatsPerBar);
                m_clickPlaying = false;
                m_clickPlayhead = 0;
            }
        }
        m_globalSamplePos.store(currentGlobalPos, std::memory_order_relaxed);
    }

    // Telemetry (lightweight counter only on RT thread)
    m_telemetry.incrementBlocksProcessed();
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

#if 0
void AudioEngine::renderGraph(const AudioGraph& graph, uint32_t numFrames) {
    bool srcActiveThisBlock = false;

    // Guard
    if (numFrames > m_maxBufferFrames.load(std::memory_order_relaxed) || m_outputChannels.load(std::memory_order_relaxed) != 2) {
        std::memset(m_masterBufferD.data(), 0, 
                   static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(double));
       m_telemetry.incrementUnderruns();
       return;
    }

    const size_t availableTracks = m_trackBuffersD.size();
    if (availableTracks == 0) {
        std::memset(m_masterBufferD.data(), 0,
                    static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(double));
        m_telemetry.incrementUnderruns();
        return;
    }

    // Clear master with memset (faster than std::fill for POD)
    std::memset(m_masterBufferD.data(), 0, 
               static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(double));

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
            state.volume.setTarget(volTarget);
            state.pan.setTarget(panTarget);
            state.volume.snap();
            state.pan.snap();
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
                srcActiveThisBlock = true;
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
        state.volume.setTarget(volTarget);
        state.pan.setTarget(panTarget);
        
        // Get current smoothed values
        const double vol = state.volume.current;
        const double pan = state.pan.current;
        const double volTargetNow = state.volume.target;
        const double panTargetNow = state.pan.target;
        
        // Pre-compute start/end gains (linear interpolation across block)
        const double panAngleStart = (pan + 1.0) * QUARTER_PI_D;
        const double panAngleEnd = (panTargetNow + 1.0) * QUARTER_PI_D;
        
        const double leftGainStart = std::cos(panAngleStart) * vol;
        const double rightGainStart = std::sin(panAngleStart) * vol;
        const double leftGainEnd = std::cos(panAngleEnd) * volTargetNow;
        const double rightGainEnd = std::sin(panAngleEnd) * volTargetNow;
        
        // Linear interpolation of gains across block (cheap, smooth)
        const double leftGainDelta = (leftGainEnd - leftGainStart) / static_cast<double>(numFrames);
        const double rightGainDelta = (rightGainEnd - rightGainStart) / static_cast<double>(numFrames);
        
        double leftGain = leftGainStart;
        double rightGain = rightGainStart;
        
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

            leftGain += leftGainDelta;
            rightGain += rightGainDelta;
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
        state.volume.snap();
        state.pan.snap();
    }

    if (srcActiveThisBlock) {
        m_telemetry.incrementSrcActiveBlocks();
    }
}
#endif

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
    
    void AudioEngine::renderGraph(const AudioGraph& graph, uint32_t numFrames) {
        (void)graph; // Unused in Antigravity mode (we use pre-compiled m_renderTracks)
        
        // 0. The output buffers (Master/Busses) must be cleared externally before this? 
        // Currently processBlock clears masterBufferD if not playing, but we rely on accumulation.
        // renderGraph assumes buffers are ready to be accumulated INTO.
        // Wait, processBlock clears masterBufferD at line 170 IF NOT PLAYING.
        // But if playing, it calls renderGraph.
        // We MUST clear the accumulation buffers at the start of the block.
        // Since we are adding, we need to start from zero.
        std::fill(m_masterBufferD.begin(), 
                  m_masterBufferD.begin() + static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed), 
                  0.0);

        // 1. Get Active Graph (Wait-Free)
        const int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_acquire);
        const auto& tracks = m_renderTracks[activeIdx];
        
        // 2. Iterate Sorted List
        for (const auto& track : tracks) {
            TrackRTState& state = m_trackState[track.trackIndex];
            
            // A. Process Audio (Generate/Process)
            // Render clips to track.selfBuffer (Pre-Fader)
            renderClipAudio(track.selfBuffer, state, track.trackIndex, graph, numFrames);

            // B. Apply Real-Time Fader & Pan (Post-Fader Processing)
            // -----------------------------------------------------
            // 1. Fetch live parameters
            float faderDb = 0.0f;
            float panParam = 0.0f;
            float trimDb = 0.0f;
            bool hasLiveParams = false;
            
            // Note: We need the Track ID to find the slot. TrackRenderState has it.
            // But 'RenderTrack' only has index. 'graph.tracks' has the ID.
            if (track.trackIndex < graph.tracks.size()) {
                const auto& graphTrack = graph.tracks[track.trackIndex];
                
                auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_acquire);
                auto* params = m_continuousParamsRaw.load(std::memory_order_acquire);
                
                if (slotMap && params) {
                   uint32_t slot = slotMap->getSlotIndex(graphTrack.trackId);
                   if (slot != ChannelSlotMap::INVALID_SLOT) {
                       params->read(slot, faderDb, panParam, trimDb);
                       hasLiveParams = true;
                   }
                }
            }
            
            // 2. Update Smoothing Targets from RT Params (if available)
            if (hasLiveParams) {
                // Fader dB to Linear
                const double userVol = (faderDb <= -90.0f) ? 0.0 : std::pow(10.0, static_cast<double>(faderDb) * 0.05);
                
                // Pan Law: -3dB Constant Power (DONE ONCE PER BLOCK)
                const double panClamped = clampD(static_cast<double>(panParam), -1.0, 1.0);
                const double angle = (panClamped + 1.0) * QUARTER_PI_D;
                
                // Effective L/R Gains
                const double targetL = userVol * std::cos(angle);
                const double targetR = userVol * std::sin(angle);
                
                // Set targets for smoothing
                state.gainL.setTarget(targetL);
                state.gainR.setTarget(targetR);
            } else if (track.trackIndex < graph.tracks.size()) {
                // Fallback: No live params (e.g. unmapped track), use Track Snapshot
                const auto& graphTrack = graph.tracks[track.trackIndex];
                
                // Consistently apply Pan Law here too
                const double vol = static_cast<double>(graphTrack.volume);
                const double panClamped = clampD(static_cast<double>(graphTrack.pan), -1.0, 1.0);
                const double angle = (panClamped + 1.0) * QUARTER_PI_D;
                
                state.gainL.setTarget(vol * std::cos(angle));
                state.gainR.setTarget(vol * std::sin(angle));
            }
            
            // 3. Apply to Buffer (In-Place) & Metering
            double* selfL = track.selfBuffer;
            double* selfR = track.selfBuffer + 1;
            
            double peakL = 0.0;
            double peakR = 0.0;
            double sumSqL = 0.0;
            double sumSqR = 0.0;
            
            for (uint32_t i = 0; i < numFrames; ++i) {
                // Compute per-sample gains (Linear Ramp is cheap)
                const double gL = state.gainL.next(); 
                const double gR = state.gainR.next();
                
                // Apply
                double valL = selfL[i * 2] * gL;
                double valR = selfR[i * 2] * gR;
                
                // Store back (Post-Fader)
                selfL[i * 2] = valL;
                selfR[i * 2] = valR;
                
                // Metering stats
                const double absL = std::abs(valL);
                const double absR = std::abs(valR);
                if (absL > peakL) peakL = absL;
                if (absR > peakR) peakR = absR;
                sumSqL += valL * valL;
                sumSqR += valR * valR;
            }
            
            // 4. Publish Metering
            auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
            auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_relaxed);
            
            if (snaps && slotMap && track.trackIndex < graph.tracks.size()) {
                 const auto& graphTrack = graph.tracks[track.trackIndex];
                 uint32_t slot = slotMap->getSlotIndex(graphTrack.trackId);
                 
                 if (slot != ChannelSlotMap::INVALID_SLOT) {
                     const float rmsL = static_cast<float>(std::sqrt(sumSqL / numFrames));
                     const float rmsR = static_cast<float>(std::sqrt(sumSqR / numFrames));
                     
                     // We don't have separate Low-Pass state per track here easily without looking it up.
                     // For Phase 4, let's just write Peak/RMS. UI handles decay.
                     snaps->writeLevels(slot, static_cast<float>(peakL), static_cast<float>(peakR), rmsL, rmsR, rmsL, rmsR);
                     
                     if (peakL >= 1.0 || peakR >= 1.0) {
                        snaps->setClip(slot, peakL >= 1.0, peakR >= 1.0);
                     }
                 }
            }

            // C. Antigravity Mixing (Sum to Output)
            for (const auto& conn : track.activeConnections) {
                double* destL = conn.destinationBufferL;
                double* destR = conn.destinationBufferR;
                const size_t stride = conn.stride;
                const double gainL = conn.gainL;
                const double gainR = conn.gainR;
                
                // SIMD-friendly loop
                // Buffer layout: Interleaved [L, R, L, R...]
                for (uint32_t i = 0; i < numFrames; ++i) {
                    destL[i * stride] += selfL[i * 2] * gainL;
                    destR[i * stride] += selfR[i * 2] * gainR;
                }
            }
            
            // D. Clear Buffer for next block (Latency/Loop Support)
            std::memset(track.selfBuffer, 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));
        }
    }
    
    // Helper to render clips for a specific track into its buffer
    // Extracted from original renderGraph logic
    void AudioEngine::renderClipAudio(double* outputBuffer, TrackRTState& state, uint32_t trackIndex, const AudioGraph& graph, uint32_t numFrames) {
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
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
                            dst[i * 2] += static_cast<double>(outL) * clipGain * fade;
                            dst[i * 2 + 1] += static_cast<double>(outR) * clipGain * fade;
                            phase += ratio;
                        }
                        break;
                }
            }
        }
    }

} // namespace Audio
} // namespace Nomad
