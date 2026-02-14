// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../../AestraCore/include/AestraMath.h"
#include "AuditionEngine.h"
#include "PathUtils.h" // [NEW] For robust path conversion
#include "EffectChain.h" // [NEW]
#include "PluginHost.h"
#include "Plugin/SamplerPlugin.h" // [NEW]
#include "UnitManager.h"
#include "PatternPlaybackEngine.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <map>
#include <immintrin.h> // AVX/SSE for high-performance mixing
#include "miniaudio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h> // ALLOW_PLATFORM_INCLUDE
#endif

// Denormal protection macros
#define DISABLE_DENORMALS \
    int oldMXCSR = _mm_getcsr(); \
    _mm_setcsr(oldMXCSR | 0x8040); // Set DAZ and FTZ flags

#define RESTORE_DENORMALS \
    _mm_setcsr(oldMXCSR);

namespace Aestra {
namespace Audio {

namespace {
    inline double clampD(double v, double lo, double hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    inline double dbToLinearD(double db) {
        // UI uses -90 dB as "silence"
        if (db <= -90.0) return 0.0;
        return static_cast<double>(Aestra::dbToGain(static_cast<float>(db)));
    }
    
    // Fast constant-power pan gains (replaces std::sin/cos)
    inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
        float p = (static_cast<float>(pan) + 1.0f) * 0.5f; // 0.0 to 1.0
        gainL = static_cast<double>(std::cos(p * 1.57079632679f)) * vol;
        gainR = static_cast<double>(std::sin(p * 1.57079632679f)) * vol;
    }

    inline void addMidiPanic(Aestra::Audio::MidiBuffer& buf) {
        // CC120: All Sound Off, CC123: All Notes Off, CC121: Reset All Controllers
        // Send on all 16 MIDI channels at sampleOffset 0.
        for (uint8_t ch = 0; ch < 16; ++ch) {
            const uint8_t allSoundOff[3] = { static_cast<uint8_t>(0xB0u | ch), 120u, 0u };
            const uint8_t resetControllers[3] = { static_cast<uint8_t>(0xB0u | ch), 121u, 0u };
            const uint8_t allNotesOff[3] = { static_cast<uint8_t>(0xB0u | ch), 123u, 0u };
            buf.addEvent(0, allSoundOff, 3);
            buf.addEvent(0, resetControllers, 3);
            buf.addEvent(0, allNotesOff, 3);
        }
    }
}

// Global pointer for singleton access
static AudioEngine* g_audioEngineInstance = nullptr;

AudioEngine& AudioEngine::getInstance() {
    // Assert if null in debug builds
    if (!g_audioEngineInstance) {
        // Critical error - engine not created yet
        static AudioEngine fallback; // Emergency fallback
        return fallback;
    }
    return *g_audioEngineInstance;
}

void AudioEngine::applyPendingCommands() {
    AudioQueueCommand cmd;
    // Bounded drain - max 16 commands per block (less work = less RT risk)
    int cmdCount = 0;
    bool hasTransport = false;
    AudioQueueCommand lastTransport;

    // Track transport transitions within this block even though we coalesce
    // the final transport state (stop->play can otherwise be missed).
    // [FIX] Use RT-side tracked state, NOT the atomic (which UI updates immediately).
    // This avoids race condition where UI sets m_transportPlaying before RT reads it.
    bool transportPlaying = m_rtLastTransportPlaying;
    uint64_t transportPos = m_rtLastTransportPos;
    bool sawRestartEdge = false;
    bool sawStopEdge = false;
    bool sawHardStopEdge = false;
    
    while (cmdCount < 16 && m_commandQueue.pop(cmd)) {
        ++cmdCount;
        // Transport commands: keep only the latest per block, but record edges.
        if (cmd.type == AudioQueueCommandType::SetTransportState) {
            const bool nextPlaying = (cmd.value1 != 0.0f);
            const uint64_t nextPos = cmd.samplePos;
            const bool posChanged = (nextPos != transportPos);

            if (nextPlaying && (!transportPlaying || posChanged)) {
                sawRestartEdge = true;
            }
            if (!nextPlaying && (transportPlaying || posChanged)) {
                sawStopEdge = true;
            }

            // If we are already stopped and receive another stop with no seek,
            // interpret it as a "hard stop" request (e.g. stop pressed twice).
            if (!nextPlaying && !transportPlaying && !posChanged) {
                sawHardStopEdge = true;
            }

            transportPlaying = nextPlaying;
            transportPos = nextPos;
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
        if (sawRestartEdge) {
            m_transportRestartRequested.store(true, std::memory_order_release);
        }
        if (sawStopEdge) {
            m_transportStopRequested.store(true, std::memory_order_release);
        }
        if (sawHardStopEdge) {
            m_transportHardStopRequested.store(true, std::memory_order_release);
        }

        // Update RT-side state tracking (used for edge detection next block)
        m_rtLastTransportPlaying = transportPlaying;
        m_rtLastTransportPos = transportPos;

        m_transportPlaying.store(transportPlaying, std::memory_order_relaxed);
        m_globalSamplePos.store(transportPos, std::memory_order_relaxed);

        if (transportPlaying && sawRestartEdge) {
            // Always fade-in when starting playback (prevents clicks/buzz)
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState == FadeState::Silent) {
            // Fade-in from silent state, don't jump to full volume
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState == FadeState::FadingOut) {
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
        m_threadPool = std::make_unique<Aestra::RealTimeThreadPool>(count);
        m_syncBarrier = std::make_unique<Aestra::Barrier>(0);
    }
}

/**
 * @brief Real-time audio processing entry point for a single block.
 *
 * Processes input (recording callback), renders the audio graph into an internal
 * double-precision master buffer, applies master gain smoothing, optional safety
 * processing (DC blocking, soft clip), per-sample LUFS filtering and dithering,
 * fades, metronome mixing, and writes the final interleaved stereo float output
 * to outputBuffer. Also updates metering (peak/RMS/low-frequency energy/correlation),
 * accumulates loudness energy for background LUFS calculation, advances the
 * global sample position (with sample-accurate looping support), and captures
 * recent output into the waveform history.
 *
 * Parameters:
 * @param outputBuffer Interleaved stereo output buffer to fill (must be non-null and sized for numFrames * output channels).
 * @param inputBuffer Optional interleaved input buffer; if provided and an input callback is registered, the callback is invoked with this buffer.
 * @param numFrames Number of frames to process in this block.
 * @param streamTime Unused (present for API compatibility).
 *
 * Notes:
 * - If internal buffers are not configured, the function silences the provided outputBuffer and returns.
 * - Handles fade-in/fade-out state transitions and will set the engine to Silent when fading completes.
 * - Performs sample-accurate looping by splitting the block if a loop boundary is crossed.
 * - Meter snapshots are published if a snapshot buffer is available; clipping flags are set when peaks >= 1.0.
 * - Dithering mode, safety processing, metronome, and LUFS accumulation are all controlled by engine state flags.
 */
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

    // Consume transport edge flags (set inside applyPendingCommands)
    const bool transportStop = m_transportStopRequested.exchange(false, std::memory_order_acq_rel);
    const bool transportRestart = m_transportRestartRequested.exchange(false, std::memory_order_acq_rel);
    const bool transportHardStop = m_transportHardStopRequested.exchange(false, std::memory_order_acq_rel);

    // Pattern mode semantics: stop/restart always resets playhead to the pattern start.
    const bool patternModeNow = m_patternPlaybackMode.load(std::memory_order_relaxed);
    if (patternModeNow && (transportStop || transportRestart)) {
        m_globalSamplePos.store(0, std::memory_order_relaxed);
    }

    // Restart/hard-stop should also send MIDI panic (helps for VST/CLAP instruments).
    if (transportRestart || transportHardStop) {
        m_transportMidiPanicRequested.store(true, std::memory_order_release);
    }

    // Hard stop: immediate silence + clear one-shots.
    // This is intentionally stronger than normal stop (which allows tails to ring out).
    if (transportHardStop) {
        auto* pe = m_patternEngine.load(std::memory_order_relaxed);
        if (pe) pe->flush();

        auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
        if (unitMgr) {
            auto snapshot = unitMgr->getAudioSnapshot();
            if (snapshot) {
                for (const auto& unitState : snapshot->units) {
                    if (unitState.plugin) {
                        auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(unitState.plugin);
                        if (sampler) {
                            sampler->requestHardResetVoices();
                        }
                    }
                }
            }
        }

        // Force silence immediately.
        m_fadeState = FadeState::Silent;
    }

    // State transitions
    bool isPlaying = m_transportPlaying.load(std::memory_order_relaxed);
    // [CHANGED] Disable global fade-out to allow effects tails to ring out.
    // if (wasPlaying && !isPlaying &&
    //     m_fadeState != FadeState::FadingOut && m_fadeState != FadeState::Silent) {
    //     m_fadeState = FadeState::FadingOut;
    //     m_fadeSamplesRemaining = FADE_OUT_SAMPLES;
    // }
    
    // Flush Pattern Engine on Stop to prevent stale events.
    // NOTE: normal stop does NOT cut one-shots (tails are allowed).
    if (transportStop) {
        auto* pe = m_patternEngine.load(std::memory_order_relaxed);
        if (pe) pe->flush();
    }

    // On transport restart (play start or seek):
    // - flush pattern queue
    // - in pattern mode, cut any still-playing one-shots so the loop restarts audibly
    if (transportRestart) {
        auto* pe = m_patternEngine.load(std::memory_order_relaxed);
        if (pe) pe->flush();

        if (patternModeNow) {
            auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
            if (unitMgr) {
                auto snapshot = unitMgr->getAudioSnapshot();
                if (snapshot) {
                    for (const auto& unitState : snapshot->units) {
                        if (unitState.plugin) {
                            auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(unitState.plugin);
                            if (sampler) {
                                sampler->requestHardResetVoices();
                            }
                        }
                    }
                }
            }
        }
    }

    // When starting playback, always ensure we're fading in (or already fading in)
    // This prevents the audio from jumping to full volume instantly → no clicks
    // [FIX] Also critical for recovering from Panic (Silent) state
    if (isPlaying) {
        if (m_fadeState == FadeState::Silent || (!wasPlaying && m_fadeState != FadeState::FadingIn)) {
            m_fadeState = FadeState::FadingIn;
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        }
    }

    // === Audition Mode Override (Exclusive) ===
    // IMPORTANT: Check BEFORE Silent state so audition plays when DAW transport is stopped
    // TODO: Investigate potential sound quality enhancements:
    //       - Higher quality resampling (polyphase vs current Sinc64Turbo)
    //       - Sample rate conversion improvements
    //       - Anti-aliasing filter tuning
    //       - Dithering options for audition playback
    if (m_auditionModeEnabled.load(std::memory_order_relaxed)) {
        auto* audition = m_auditionEngine.load(std::memory_order_relaxed);
        if (audition) {
             audition->processBlock(outputBuffer, numFrames, m_outputChannels.load(std::memory_order_relaxed));
             
             // Simple metering for Audition (optional, but good for UI feedback)
             float audPeakL = 0.0f, audPeakR = 0.0f;
             for (uint32_t i=0; i<numFrames; ++i) {
                 float l = std::abs(outputBuffer[i*2]);
                 float r = std::abs(outputBuffer[i*2+1]);
                 if (l > audPeakL) audPeakL = l;
                 if (r > audPeakR) audPeakR = r;
             }
             m_peakL.store(audPeakL, std::memory_order_relaxed);
             m_peakR.store(audPeakR, std::memory_order_relaxed);
             
             m_telemetry.incrementBlocksProcessed();
             RESTORE_DENORMALS
             return; 
        }
    }

    // Fast path: silent (only for normal DAW mode, not audition)
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
        RESTORE_DENORMALS
        return;
    }

    // Render to double-precision master buffer
    const AudioGraph& graph = m_state.activeGraph();
    
    // Loop & Position Logic Loop Calculation
    bool playing = m_transportPlaying.load(std::memory_order_relaxed);
    bool loopEnabled = m_loopEnabled.load(std::memory_order_relaxed);
    
    // Pattern Mode Override
    bool patternMode = m_patternPlaybackMode.load(std::memory_order_relaxed);
    double loopStartBeat = m_loopStartBeat.load(std::memory_order_relaxed);
    double loopEndBeat = m_loopEndBeat.load(std::memory_order_relaxed);
    
    if (patternMode) {
        loopEnabled = true; // Force loop in pattern mode
        loopStartBeat = 0.0;
        loopEndBeat = m_patternLengthBeats.load(std::memory_order_relaxed);
    }

    uint64_t currentGlobalPos = m_globalSamplePos.load(std::memory_order_relaxed);
    
    // We calculate the NEXT position here to handle looping correctly
    uint64_t nextGlobalPos = currentGlobalPos;
    uint32_t loopSplitFrame = numFrames; // Default: no split
    uint64_t loopStartSample = 0;
    
    if (playing || m_fadeState == FadeState::FadingOut) {
        nextGlobalPos += numFrames;
        
        if (loopEnabled) {
            float bpm = m_metronomeEngine.getBPM();
            // Convert loop end beat to sample position
            double samplesPerBeat = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) / static_cast<double>(bpm);
            uint64_t loopEndSample = static_cast<uint64_t>(loopEndBeat * samplesPerBeat);
            loopStartSample = static_cast<uint64_t>(loopStartBeat * samplesPerBeat);
            
            // Check for loop crossing
            // Enhanced Logic: In Pattern Mode, we might start WAY past loop end (Timeline position).
            // We must wrap if nextGlobalPos exceeds loopEndSample, regardless of where current started.
            if (nextGlobalPos > loopEndSample && loopEndSample > loopStartSample) {
                // Loop Triggered!
                // Calculate wrap
                // Case 1: Standard crossing (inside -> outside)
                // Case 2: Way outside -> further outside (Pattern toggle)
                
                uint64_t loopLength = loopEndSample - loopStartSample;
                if (loopLength > 0) {
                    // Normalize position to loop range
                    // Calculate "linear" next pos relative to start
                    uint64_t relativePos = nextGlobalPos - loopStartSample;
                    uint64_t wrappedRelative = relativePos % loopLength;
                    
                    nextGlobalPos = loopStartSample + wrappedRelative;
                    
                    // For split-rendering, we need to know WHERE in the buffer the wrap happens.
                    // If we were already outside, we should probably just jump immediately (split at 0).
                    if (currentGlobalPos >= loopEndSample) {
                         loopSplitFrame = 0; // Jump immediately
                    } else {
                         // Standard crossing
                         uint64_t framesUntilLoop = loopEndSample - currentGlobalPos;
                         if (framesUntilLoop < numFrames) {
                             loopSplitFrame = static_cast<uint32_t>(framesUntilLoop);
                         } else {
                             // This shouldn't happen if nextGlobalPos > loopEndSample
                             loopSplitFrame = 0;
                         }
                    }
                }
            }
        }
    }

    if (!m_masterBufferD.empty()) {
        if (!patternMode) {
            // === Timeline Mode: Render Graph ===
            if (loopSplitFrame < numFrames) {
                // Split Render
                if (loopSplitFrame > 0) {
                    renderGraph(graph, loopSplitFrame, 0);
                }
                
                // Part B: Jump to loop start
                // Flush pattern events on loop wrap so we don't double-trigger across the wrap.
                if (isPlaying) {
                    auto* pe = m_patternEngine.load(std::memory_order_relaxed);
                    if (pe) pe->flush();
                }
                m_globalSamplePos.store(loopStartSample, std::memory_order_relaxed);
                renderGraph(graph, numFrames - loopSplitFrame, loopSplitFrame);
                m_globalSamplePos.store(currentGlobalPos, std::memory_order_relaxed);
                
            } else {
                // Normal Render
                renderGraph(graph, numFrames, 0);
            }
        } else {
            // === Pattern Mode: Clear Buffer ===
            std::fill(m_masterBufferD.begin(), 
                      m_masterBufferD.begin() + static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed), 
                      0.0);
        }
    } else {
        // Zero the double buffer
        std::fill(m_masterBufferD.begin(), 
                  m_masterBufferD.begin() + static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed), 
                  0.0);
    }

    // === Arsenal Unit Processing (Pattern Playback) ===
    // Process pattern MIDI events and mix sampler output into master buffer
    // Must respect loop splitting similar to renderGraph
    if (loopSplitFrame < numFrames) {
        // Split Render
        if (loopSplitFrame > 0) {
            processArsenalUnits(loopSplitFrame, 0, currentGlobalPos);
        }
        // Flush pattern events on loop wrap so we don't accumulate duplicates.
        if (isPlaying) {
            auto* pe = m_patternEngine.load(std::memory_order_relaxed);
            if (pe) pe->flush();
        }
        processArsenalUnits(numFrames - loopSplitFrame, loopSplitFrame, loopStartSample);
    } else {
        // Normal
        processArsenalUnits(numFrames, 0, currentGlobalPos);
    }

    // === Test Tone Injection ===
    if (m_testToneEnabled.load(std::memory_order_relaxed)) {
        const double sampleRate = static_cast<double>(m_sampleRate.load(std::memory_order_relaxed));
        if (sampleRate > 0.0) {
            const double frequency = 440.0;
            const double amplitude = 0.05; // -26dB
            const double twoPi = 2.0 * PI_D;
            const double phaseIncrement = twoPi * frequency / sampleRate;
            
            double phase = m_testTonePhase;
            // Loop unrolling for stereo
            for (uint32_t i = 0; i < numFrames; ++i) {
                // Determine source index (might be different if we split-looped above, but test tone is global/overlay)
                // Actually, since we write to m_masterBufferD, we just mix it in.
                // Simple sin approximation or std::sin is fine for test tone.
                double sample = amplitude * std::sin(phase);
                
                // Mix into master buffer (Post-Graph, Pre-Master Fader)
                m_masterBufferD[i * 2]     += sample; // L
                m_masterBufferD[i * 2 + 1] += sample; // R
                
                phase += phaseIncrement;
                if (phase >= twoPi) phase -= twoPi;
            }
            m_testTonePhase = phase;
        }
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
    m_metronomeEngine.process(
        outputBuffer,
        numFrames,
        m_outputChannels.load(std::memory_order_relaxed),
        m_globalSamplePos.load(std::memory_order_relaxed),
        static_cast<uint32_t>(m_sampleRate.load(std::memory_order_relaxed)),
        m_transportPlaying.load(std::memory_order_relaxed)
    );

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
        if (loopSplitFrame < numFrames) {
            m_metronomeEngine.reset(nextGlobalPos, static_cast<uint32_t>(m_sampleRate.load(std::memory_order_relaxed)));
        }
    }

    // Telemetry (lightweight counter only on RT thread)
    m_telemetry.incrementBlocksProcessed();
    
    // B-009: Record stable block for underrun recovery tracking
    m_telemetry.recordStableBlock();

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
        
        // Resize plugin scratch buffers (mono size)
        size_t monoSize = static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed));
        if (m_scratchL.size() < monoSize) m_scratchL.resize(monoSize);
        if (m_scratchR.size() < monoSize) m_scratchR.resize(monoSize);

        // Pre-allocate per-unit MIDI scratch buffers so RT never resizes.
        // Note: units are expected to be low-count; we cap to a reasonable maximum.
        constexpr size_t kMaxUnitsRt = 256;
        if (m_scratchMidiBuffers.size() < kMaxUnitsRt) {
            m_scratchMidiBuffers.resize(kMaxUnitsRt);
        }
        if (m_unitMidiBuffers.size() < kMaxUnitsRt) {
            m_unitMidiBuffers.resize(kMaxUnitsRt);
        }

        // Arsenal buffers (stereo interleaved and de-interleaved scratch)
        const size_t stereoSamples = monoSize * 2;
        if (m_unitBufferD.size() < stereoSamples) {
            m_unitBufferD.resize(stereoSamples);
        }
        if (m_pluginBufferF.size() < stereoSamples) {
            m_pluginBufferF.resize(stereoSamples);
        }
        if (m_silentBufferF.size() < monoSize) {
            m_silentBufferF.resize(monoSize);
        }

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
    


    // Critical: Buffers may have moved after resize. Re-swizzle the pointers.
    if (needAlloc) {
        compileGraph();
    }

    // Prepare insert chains at a safe point (may allocate). This prevents RT prepare() calls.
    const double sampleRate = static_cast<double>(m_sampleRate.load(std::memory_order_relaxed));
    const uint32_t maxBlockSize = m_maxBufferFrames.load(std::memory_order_relaxed);
    const auto& graph = m_state.activeGraph();
    for (const auto& tr : graph.tracks) {
        if (tr.effectChain && tr.effectChain->getActiveSlotCount() > 0) {
            tr.effectChain->prepare(sampleRate, maxBlockSize);
        }
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

/**
 * @brief Initialize AudioEngine runtime state.
 *
 * Generates the metronome click samples and starts the background loudness worker
 * used for integrated LUFS calculation.
 */
AudioEngine::AudioEngine() {
    g_audioEngineInstance = this; // [NEW] Register singleton

    if (g_audioEngineInstance == nullptr) {
        g_audioEngineInstance = this;
    }
    Aestra::Log::info("[AudioEngine] Created (Original Ctor). Ptr: " + std::to_string(reinterpret_cast<uintptr_t>(this)));
    
    // Initialize default buffer config
    m_outputChannels.store(2);
    m_maxBufferFrames.store(4096);
    
    // Initialize telemetry
    m_telemetry.cycleHz.store(0);

    m_loudnessState.integratedLufs.store(-144.0f); // Force silence init
    m_loudnessState.momentaryLufs.store(-144.0f);

    // Metronome sounds generated in MetronomeEngine constructor
    startLoudnessWorker();
}

AudioEngine::~AudioEngine() {
    stopLoudnessWorker();
    if (g_audioEngineInstance == this) {
        g_audioEngineInstance = nullptr; // [NEW] Clear singleton
    }
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
    const bool isPlaying = m_transportPlaying.load(std::memory_order_relaxed); // [NEW] Check transport

    // Solo detection (single pass)
    bool anySolo = false;
    for (const auto& tr : graph.tracks) {
        auto& state = ensureTrackState(tr.trackIndex);
        if (tr.solo || state.solo) {
            anySolo = true;
            break;
        }
    }

    // === ANTIGRAVITY UNIT PRE-PROCESSING (v3.1) ===
    UnitManager* unitMgr = m_unitManager.load(std::memory_order_acquire);
    std::shared_ptr<const AudioArsenalSnapshot> unitSnapshot;
    std::array<PatternPlaybackEngine::UnitMidiRoute, 256> unitMidiRoutes{};
    size_t unitMidiRouteCount = 0;
    
    if (unitMgr) {
        unitSnapshot = unitMgr->getAudioSnapshot();
        if (unitSnapshot && !unitSnapshot->units.empty()) {
            // Map valid units to preallocated MIDI buffers (allocation-free)
            size_t bufIdx = 0;
            for (const auto& unit : unitSnapshot->units) {
                if (unitMidiRouteCount >= unitMidiRoutes.size()) break;
                if (bufIdx >= m_scratchMidiBuffers.size()) break;
                if (unit.id != 0 && unit.plugin) {
                    m_scratchMidiBuffers[bufIdx].clear();
                    unitMidiRoutes[unitMidiRouteCount++] = { unit.id, &m_scratchMidiBuffers[bufIdx] };
                    ++bufIdx;
                }
            }

            // Optional: inject MIDI panic before pattern events are scheduled.
            if (m_transportMidiPanicRequested.exchange(false, std::memory_order_acq_rel)) {
                for (size_t r = 0; r < unitMidiRouteCount; ++r) {
                    if (unitMidiRoutes[r].midiBuffer) {
                        addMidiPanic(*unitMidiRoutes[r].midiBuffer);
                    }
                }
            }
            
            // Pop MIDI from Pattern Engine (only while transport is playing)
            auto* patEng = m_patternEngine.load(std::memory_order_acquire);
            if (isPlaying && patEng && isPatternPlaybackMode()) {
                // Use global position + offset? 
                // Pattern engine expects Frame time.
                // blockStart is appropriate.
                patEng->processAudio(blockStart, static_cast<int>(numFrames), unitMidiRoutes.data(), unitMidiRouteCount);
            }
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
        auto& buffer = m_trackBuffersD[trackIdx];
        std::memset(buffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));

        // Check isPlaying inside loop to avoid brace wrapping issues
        // [FIX] Suppress timeline clips if in Pattern Mode (Audio Isolation)
        bool patternMode = m_patternPlaybackMode.load(std::memory_order_relaxed);
        
        if (!patternMode) {
            for (const auto& clip : track.clips) {
            if (!isPlaying) continue;
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
        } // End pattern mode check
            }



        // === ANTIGRAVITY UNIT RENDER (v3.1) ===
        // Render any units routed to this track
        if (unitSnapshot) {
             for (const auto& unit : unitSnapshot->units) {
                 if (static_cast<uint32_t>(unit.routeId) == trackIdx && unit.enabled && unit.plugin) {
                      // Found unit for this track
                      MidiBuffer* midiBuf = nullptr;
                      for (size_t r = 0; r < unitMidiRouteCount; ++r) {
                          if (unitMidiRoutes[r].unitId == unit.id) {
                              midiBuf = unitMidiRoutes[r].midiBuffer;
                              break;
                          }
                      }
                      
                      // Render to scratch
                      // Note: Inputs are nullptr (Generator)
                      if (m_scratchL.size() < numFrames || m_scratchR.size() < numFrames) {
                          // Should not happen (pre-sized in setBufferConfig); fail-safe
                          continue;
                      }
                      
                      float* outputs[2] = { m_scratchL.data(), m_scratchR.data() };
                      
                      // Process Plugin
                      unit.plugin->process(nullptr, outputs, 0, 2, numFrames, midiBuf, nullptr);
                      
                      // Mix to Track Buffer (Double Precision)
                      double* dDst = buffer.data();
                      for (uint32_t k = 0; k < numFrames; ++k) {
                          dDst[k * 2]     += static_cast<double>(outputs[0][k]);
                          dDst[k * 2 + 1] += static_cast<double>(outputs[1][k]);
                      }
                 }
             }
        }


        // === Plugin Processing (EffectChain) ===
        if (track.effectChain && track.effectChain->getActiveSlotCount() > 0) {
            // Check if scratches are large enough (should be from setBufferConfig)
            if (m_scratchL.size() >= numFrames && m_scratchR.size() >= numFrames) {
                // 1. De-interleave Double -> Float
                const double* dBuf = buffer.data();
                float* fL = m_scratchL.data();
                float* fR = m_scratchR.data();
                
                // Allow vectorization
                for (uint32_t k = 0; k < numFrames; ++k) {
                    fL[k] = static_cast<float>(dBuf[k * 2]);
                    fR[k] = static_cast<float>(dBuf[k * 2 + 1]);
                }

                // 2. Process
                float* channels[2] = { fL, fR };
                track.effectChain->process(channels, 2, numFrames);

                // 3. Re-interleave Float -> Double
                double* dOut = buffer.data();
                for (uint32_t k = 0; k < numFrames; ++k) {
                    dOut[k * 2]     = static_cast<double>(fL[k]);
                    dOut[k * 2 + 1] = static_cast<double>(fR[k]);
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
        double sumLRTrack   = 0.0; // Correlation accumulator

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
                sumLRTrack   += outL * outR;
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
            
            float trackCorr = 0.0f;
            const double den = std::sqrt(rmsAccTrackL * rmsAccTrackR); // rmsAcc is sumSq
            if (den > 1e-9) {
                trackCorr = static_cast<float>(sumLRTrack / den);
            }

            snaps->writeLevels(slot, peakL, peakR, rmsL, rmsR, lowL, lowR, trackCorr);
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


TrackRTState& AudioEngine::ensureTrackState(uint32_t trackIndex) {
    if (m_trackState.empty()) {
        return m_dummyTrackState;
    }
    if (trackIndex >= m_trackState.size()) {
        return m_dummyTrackState;
    }
    return m_trackState[trackIndex];
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
        auto& targetOrder = m_graphStates[inactiveIdx].renderTracks;
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
    



        
void AudioEngine::panic() {
    // 1. Force Silence (stops renderGraph calls and mutes output immediately)
    m_fadeState = FadeState::Silent;

    // 2. Reset all plugins (Main Thread)
    // We lock the graph mutex to ensure we don't access a graph that's being swapped
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    // Note: accessing activeGraph() from Main Thread is safe given we hold the mutex
    // that protects the swap.
    const AudioGraph& graph = m_state.activeGraph();
    
    for (const auto& track : graph.tracks) {
        if (track.effectChain) {
            track.effectChain->reset(); 
        }
    }
    
    // 3. [FIX] Reset all Arsenal unit samplers (kill playing voices)
    auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
    if (unitMgr) {
        auto snapshot = unitMgr->getAudioSnapshot();
        if (snapshot) {
            for (const auto& unitState : snapshot->units) {
                if (unitState.plugin) {
                    auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(unitState.plugin);
                    if (sampler) {
                        sampler->requestHardResetVoices();
                    }
                }
            }
        }
    }
    
    // 4. Flush pattern engine
    auto* pe = m_patternEngine.load(std::memory_order_relaxed);
    if (pe) pe->flush();
}

void AudioEngine::requestVoiceResetOnPatternChange() {
    // Reset all Arsenal sampler voices when pattern ID changes
    // This hard-cuts audio, making pattern selection audible
    // (vs same-pattern MIDI edits which allow audio bleed for musical effect)
    
    auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
    if (unitMgr) {
        auto snapshot = unitMgr->getAudioSnapshot();
        if (snapshot) {
            for (const auto& unitState : snapshot->units) {
                if (unitState.plugin) {
                    auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(unitState.plugin);
                    if (sampler) {
                        sampler->requestHardResetVoices();
                    }
                }
            }
        }
    }
}

//==============================================================================
// Arsenal Unit Processing (Pattern Playback)
//==============================================================================

void AudioEngine::processArsenalUnits(uint32_t numFrames, uint32_t bufferOffset, uint64_t startFrame) {
    // [FIX] Only process Arsenal units in pattern/Arsenal mode
    // In Timeline mode, skip entirely to prevent audio bleed with wrong sample rate
    if (!m_patternPlaybackMode.load(std::memory_order_relaxed)) {
        return;
    }
    
    // Get dependencies (RT-safe)
    auto* patternEngine = m_patternEngine.load(std::memory_order_acquire);
    auto* unitManager = m_unitManager.load(std::memory_order_acquire);
    
    if (!patternEngine || !unitManager) return;
    
    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (sampleRate == 0) return;
    
    // Sync logic moved after snapshot retrieval

    // Continue rendering even when transport is stopped (one-shot/tails behavior).
    // But do not schedule new MIDI when stopped.
    const bool transportPlaying = m_transportPlaying.load(std::memory_order_relaxed);
    
    const uint64_t currentFrame = startFrame;
    
    // Get Arsenal snapshot for RT-safe unit iteration
    auto snapshot = unitManager->getAudioSnapshot();
    if (!snapshot || snapshot->units.empty()) {
        static int emptyCount = 0;
        if (++emptyCount % 1000 == 0) {
            Aestra::Log::info("[Arsenal] Snapshot empty or no units: " + 
                std::to_string(snapshot ? snapshot->units.size() : 0));
        }
        return;
    }
    
    // DEBUG: Log unit processing occasionally
    static int arsenalDebug = 0;
    if (++arsenalDebug % 500 == 0) {
        int enabledCount = 0, pluginCount = 0;
        for (const auto& u : snapshot->units) {
            if (u.enabled) enabledCount++;
            if (u.plugin) pluginCount++;
        }
        Aestra::Log::info("[Arsenal] Units: " + std::to_string(snapshot->units.size()) +
            " enabled=" + std::to_string(enabledCount) + 
            " hasPlugin=" + std::to_string(pluginCount) +
            " playing=" + std::to_string(transportPlaying));
    }
    
    // Sync sample rate to units only when it changes (avoid per-block scans)
    static uint32_t s_lastSyncedSampleRate = 0;
    if (s_lastSyncedSampleRate != sampleRate) {
        for (const auto& unitState : snapshot->units) {
            if (unitState.plugin) {
                auto sampler = std::dynamic_pointer_cast<Aestra::Audio::Plugins::SamplerPlugin>(unitState.plugin);
                if (sampler) {
                    sampler->setSampleRate((double)sampleRate);
                }
            }
        }
        s_lastSyncedSampleRate = sampleRate;
    }
    
    const size_t requiredStereoSamples = static_cast<size_t>(numFrames) * 2;
    // Buffers must be pre-sized in setBufferConfig()
    if (m_unitBufferD.size() < requiredStereoSamples ||
        m_pluginBufferF.size() < requiredStereoSamples ||
        m_silentBufferF.size() < numFrames) {
        return;
    }
    // Always zero the silent buffer (just in case)
    std::fill(m_silentBufferF.begin(), m_silentBufferF.begin() + numFrames, 0.0f);
    
    // Build unit MIDI routes (allocation-free)
    std::array<PatternPlaybackEngine::UnitMidiRoute, 256> unitMidiRoutes{};
    size_t unitMidiRouteCount = 0;
    size_t bufIdx = 0;
    for (const auto& unit : snapshot->units) {
        if (unitMidiRouteCount >= unitMidiRoutes.size()) break;
        if (bufIdx >= m_unitMidiBuffers.size()) break;
        m_unitMidiBuffers[bufIdx].clear();
        unitMidiRoutes[unitMidiRouteCount++] = { unit.id, &m_unitMidiBuffers[bufIdx] };
        ++bufIdx;
    }
    
    // Refill and process pattern MIDI events only while playing
    if (transportPlaying) {
        constexpr int LOOKAHEAD_SAMPLES = 2048; // ~40ms at 48kHz
        patternEngine->refillWindow(currentFrame, static_cast<int>(sampleRate), LOOKAHEAD_SAMPLES);
        patternEngine->processAudio(currentFrame, static_cast<int>(numFrames), unitMidiRoutes.data(), unitMidiRouteCount);
    }

    // Process each unit plugin
    bufIdx = 0;
    
    // Inputs (Stereo Silence)
    const float* inputs[2] = { m_silentBufferF.data(), m_silentBufferF.data() };
    
    for (const auto& unit : snapshot->units) {
        if (!unit.enabled || !unit.plugin) {
            bufIdx++;
            continue;
        }
        
        // Clear plugin output buffer
        std::fill(m_pluginBufferF.begin(), m_pluginBufferF.begin() + requiredStereoSamples, 0.0f);
        
        // Output Pointers (Non-Interleaved)
        float* outputs[2] = { m_pluginBufferF.data(), m_pluginBufferF.data() + numFrames };
        
        // Process plugin with MIDI
        MidiBuffer* midiIn = nullptr;
        for (size_t r = 0; r < unitMidiRouteCount; ++r) {
            if (unitMidiRoutes[r].unitId == unit.id) {
                midiIn = unitMidiRoutes[r].midiBuffer;
                break;
            }
        }
        MidiBuffer midiOut; // Unused
        
        unit.plugin->process(inputs, outputs, 2, 2, numFrames, midiIn, &midiOut);
        
        // Mix plugin output into master buffer (mixing floats into double master)
        double* masterD = m_masterBufferD.data() + static_cast<size_t>(bufferOffset) * 2;
        for (uint32_t i = 0; i < numFrames; ++i) {
            masterD[i * 2 + 0] += static_cast<double>(outputs[0][i]); // Left
            masterD[i * 2 + 1] += static_cast<double>(outputs[1][i]); // Right
        }
        
        bufIdx++;
    }
}


// =================================================================================================
// Offline Bounce / Export
// =================================================================================================

bool AudioEngine::bounceRangeToWav(double startBeat, double endBeat, const std::string& outputPath, int32_t trackId) {
    if (endBeat <= startBeat) return false;

    // 1. Calculate length
    double sampleRate = (double)m_sampleRate.load(std::memory_order_relaxed);
    float bpm = m_metronomeEngine.getBPM();
    double samplesPerBeat = (sampleRate * 60.0) / static_cast<double>(bpm);
    
    uint64_t startSample = static_cast<uint64_t>(startBeat * samplesPerBeat);
    uint64_t endSample = static_cast<uint64_t>(endBeat * samplesPerBeat);
    uint64_t totalFrames = endSample - startSample;
    
    if (totalFrames == 0) return false;
    
    // 2. Prepare Encoder (MiniAudio)
    // First, stop playback to ensure safe rendering
    bool wasPlaying = m_transportPlaying.exchange(false, std::memory_order_relaxed);

    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, (ma_uint32)sampleRate);
    ma_encoder encoder;
    
    ma_result result = MA_ERROR;
#ifdef _WIN32
    std::wstring widePath = pathStringToWide(outputPath);
    result = ma_encoder_init_file_w(widePath.c_str(), &config, &encoder);
#else
    result = ma_encoder_init_file(outputPath.c_str(), &config, &encoder);
#endif

    if (result != MA_SUCCESS) {
        Aestra::Log::error("[AudioEngine] Failed to init encoder for bounce: " + outputPath);
        // Restore playback state if we paused
        if (wasPlaying) m_transportPlaying.store(true, std::memory_order_relaxed);
        return false;
    }
    
    // 3. Render Loop
    const uint32_t blockSize = 4096;
    std::vector<double> blockBuffer(blockSize * 2); // Stereo
    std::vector<float> floatBuffer(blockSize * 2);  // For writing
    
    uint64_t currentFrame = startSample;
    uint64_t framesRemaining = totalFrames;
    
    // Playback stopped at start of function
    m_transportPlaying.store(false, std::memory_order_relaxed); // Ensure redundant enforce 

    if (wasPlaying) {
         setTransportPlaying(false);
         std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Allow RT to spin down
    }
    
    // Lock graph for stability during bounce
    std::lock_guard<std::mutex> lock(m_graphMutex);
    
    int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_relaxed);
    AudioGraphState& graphState = m_graphStates[activeIdx]; // Use active, but modify copies in renderBlock if needed?
    // Note: renderBlock applies smoothing to the passed state.
    // For off-line bounce, we should PROBABLY copy the state to avoid jumping parameters on the live graph?
    // However, for simplicity and since we are stopped, we use the active graph.
    // Ideally we clone it.
    
    Aestra::Log::info("[AudioEngine] Starting bounce: " + std::to_string(totalFrames) + " frames.");
    
    while (framesRemaining > 0) {
        uint32_t framesThisBlock = (uint32_t)std::min((uint64_t)blockSize, framesRemaining);
        
        // Zero buffer
        std::fill(blockBuffer.begin(), blockBuffer.end(), 0.0);
        
        // Setup Context
        AudioRenderer::Context ctx;
        ctx.masterBuffer = blockBuffer.data();
        ctx.numFrames = framesThisBlock;
        ctx.bufferOffset = 0;
        ctx.globalPos = currentFrame;
        ctx.sampleRate = (uint32_t)sampleRate;
        // ctx.graph is usually accessed via EngineState or we assume graphState has everything.
        // AudioRenderer uses `graphState` passed in.
        ctx.isOffline = true;
        ctx.isolatedTrackIndex = trackId; 
        
        // Render
        m_rtRenderer.renderBlock(ctx, graphState, *this);
        
        // Buffer Conversion (Double -> Float)
        for (size_t i = 0; i < framesThisBlock * 2; ++i) {
            floatBuffer[i] = static_cast<float>(blockBuffer[i]);
        }
        
        // Write
        if (ma_encoder_write_pcm_frames(&encoder, floatBuffer.data(), framesThisBlock, NULL) != framesThisBlock) {
             Aestra::Log::error("[AudioEngine] Write error during bounce");
             break;
        }
        
        currentFrame += framesThisBlock;
        framesRemaining -= framesThisBlock;
    }
    
    ma_encoder_uninit(&encoder);
    
    // Restore playback state
    if (wasPlaying) setTransportPlaying(true);
    
    Aestra::Log::info("[AudioEngine] Bounce complete.");
    return true;
}

} // namespace Audio
} // namespace Aestra