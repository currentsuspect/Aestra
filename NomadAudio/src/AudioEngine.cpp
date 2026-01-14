// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"
#include "../../NomadCore/include/NomadLog.h"
#include "../../NomadCore/include/NomadMath.h"
#include "EffectChain.h" // [NEW]
#include "PluginHost.h"
#include "Plugin/SamplerPlugin.h" // [NEW]
#include "UnitManager.h"
#include "PatternPlaybackEngine.h"
#include "../include/AuditionEngine.h" // [NEW]
#include "PathUtils.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <map>
#include <immintrin.h> // AVX/SSE for high-performance mixing
#include "../External/miniaudio/miniaudio.h"

// Denormal protection macros removed in favor of ScopedDenormals RAII wrapper

namespace Nomad {
namespace Audio {

namespace {
    // Denormal protection: RAII Wrapper for DAZ/FTZ
    struct ScopedDenormals {
        int oldMXCSR;
        ScopedDenormals() {
            oldMXCSR = _mm_getcsr();
            // Set DAZ (0x0040) and FTZ (0x8000)
            _mm_setcsr(oldMXCSR | 0x8040); 
        }
        ~ScopedDenormals() {
            _mm_setcsr(oldMXCSR);
        }
    };

    inline double clampD(double v, double lo, double hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    inline double dbToLinearD(double db) {
        // UI uses -90 dB as "silence"
        if (db <= -90.0) return 0.0;
        return static_cast<double>(Nomad::dbToGain(static_cast<float>(db)));
    }
    
    // Fast constant-power pan gains (replaces std::sin/cos)
    inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
        float p = (static_cast<float>(pan) + 1.0f) * 0.5f; // 0.0 to 1.0
        gainL = static_cast<double>(std::cos(p * 1.57079632679f)) * vol;
        gainR = static_cast<double>(std::sin(p * 1.57079632679f)) * vol;
    }

    inline void addMidiPanic(Nomad::Audio::MidiBuffer& buf) {
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
        m_threadPool = std::make_unique<Nomad::RealTimeThreadPool>(count);
        m_syncBarrier = std::make_unique<Nomad::Barrier>(0);
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
    // CRITICAL: Protect against denormal float performance spikes (10x CPU load)
    ScopedDenormals denormalGuard;

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

    // [Audition Mode] Exclusive Bypass
    // Check if we should render Audition Engine instead of the DAW graph
    if (m_auditionModeEnabled.load(std::memory_order_relaxed)) {
        AuditionEngine* audition = m_auditionEngine.load(std::memory_order_relaxed);
        if (audition) {
            // Render directly to output
            audition->processBlock(outputBuffer, numFrames, m_outputChannels.load(std::memory_order_relaxed));
            return;
        }
    }

    // Enable Denormals protection (Flush-to-Zero) is now handled by ScopedDenormals RAII at function start

    // Safety: If buffers aren't allocated (setBufferConfig not called), silence and return.
    if (m_masterBufferD.empty()) {
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed) * sizeof(float));
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

        // RT-safe: use pre-cached sampler pointers (no dynamic_pointer_cast)
        size_t samplerCount = m_cachedSamplerCount.load(std::memory_order_acquire);
        for (size_t i = 0; i < samplerCount; ++i) {
            auto* sampler = m_cachedSamplers[i];
            if (sampler) {
                sampler->requestHardResetVoices();
            }
        }

        // Force silence immediately.
        m_fadeState = FadeState::Silent;
    }

    // State transitions
    bool isPlaying = m_transportPlaying.load(std::memory_order_relaxed);
    // [CHANGED] Disable global fade-out to allow effects tails to ring out.
    // [FIX] Enable global fade-out when stopping to prevent clicks and ensure meters clear
    if (wasPlaying && !isPlaying &&
        m_fadeState != FadeState::FadingOut && m_fadeState != FadeState::Silent) {
        m_fadeState = FadeState::FadingOut;
        m_fadeSamplesRemaining = FADE_OUT_SAMPLES;
    }
    
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
            // RT-safe: use pre-cached sampler pointers (no dynamic_pointer_cast)
            size_t samplerCount = m_cachedSamplerCount.load(std::memory_order_acquire);
            for (size_t i = 0; i < samplerCount; ++i) {
                auto* sampler = m_cachedSamplers[i];
                if (sampler) {
                    sampler->requestHardResetVoices();
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
            float bpm = m_bpm.load(std::memory_order_relaxed);
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
        // CRITICAL: Zero master buffer before rendering to prevent garbage/buzzing
        const size_t bufferBytes = static_cast<size_t>(numFrames) * 2 * sizeof(double);
        std::memset(m_masterBufferD.data(), 0, bufferBytes);
        
        if (!patternMode) {
            // === Timeline Mode: Render Graph ===
            // Only render when playing (or fading out after stop)
            if (playing || m_fadeState == FadeState::FadingOut) {
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
            }
            // When stopped: buffer stays zeroed (silence)
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
    // Delegate to AudioRenderer (Phase 2 Hybrid Engine)
    {
        const AudioGraph& arsenalGraph = m_state.activeGraph();
        if (loopSplitFrame < numFrames) {
            // Split Render Part A
            if (loopSplitFrame > 0) {
                AudioRenderer::Context ctxA{
                    m_masterBufferD.data(), loopSplitFrame, 0,
                    currentGlobalPos, (uint32_t)m_sampleRate.load(std::memory_order_relaxed),
                    &arsenalGraph
                };
                m_rtRenderer.processArsenalUnits(ctxA, *this);
            }
            // Flush pattern events on loop wrap
            if (isPlaying) {
                auto* pe = m_patternEngine.load(std::memory_order_relaxed);
                if (pe) pe->flush();
            }
            // Split Render Part B
            AudioRenderer::Context ctxB{
                m_masterBufferD.data(), numFrames - loopSplitFrame, loopSplitFrame,
                loopStartSample, (uint32_t)m_sampleRate.load(std::memory_order_relaxed),
                &arsenalGraph,
                (double)m_loudnessState.integratedLufs.load(std::memory_order_relaxed)
            };
            m_rtRenderer.processArsenalUnits(ctxB, *this);
        } else {
            // Normal Render
            AudioRenderer::Context ctx{
                m_masterBufferD.data(),
                numFrames,
                0, // bufferOffset
                currentGlobalPos, // globalPos
                (uint32_t)m_sampleRate.load(std::memory_order_relaxed),
                &arsenalGraph, // graph
                (double)m_loudnessState.integratedLufs.load(std::memory_order_relaxed)
            };
            m_rtRenderer.processArsenalUnits(ctx, *this);
        }
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


        
        // CRITICAL: Rebuild graph to update buffer pointers in RenderTracks!
        // Otherwise AudioRenderer writes to old (freed) memory.
        compileGraph();
        
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
    
    /* [HYBRID ENGINE] m_parallelTrackPointers removed
    if (m_parallelTrackPointers.size() < 4096) {
        m_parallelTrackPointers.resize(4096);
    }
    */

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
    if (g_audioEngineInstance == nullptr) {
        g_audioEngineInstance = this;
    }
    Nomad::Log::info("[AudioEngine] Created (Original Ctor). Ptr: " + std::to_string(reinterpret_cast<uintptr_t>(this)));
    
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
    Nomad::Log::info("[AudioEngine] Destroyed. Ptr: " + std::to_string(reinterpret_cast<uintptr_t>(this)));
    stopLoudnessWorker();
    if (g_audioEngineInstance == this) {
        g_audioEngineInstance = nullptr;
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
        
        static int s_heartbeat = 0;
        if (++s_heartbeat % 50 == 0) { // ~5s
             Nomad::Log::info("[LoudnessWorker] Heartbeat. History: " + std::to_string(m_loudnessState.historyCount) 
             + " LUFS: " + std::to_string(m_loudnessState.integratedLufs.load()));
        }
    }
}

void AudioEngine::renderGraph(const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset) {
    // [HYBRID ENGINE] Refactored to use AudioRenderer
    
    const int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_acquire);
    AudioGraphState& state = m_graphStates[activeIdx];

    // Initialize Context
    // Initialize Context
    // Note: Context references graph, so we must initialize via brace init
    // m_outputChannels is not used by Context currently.
    AudioRenderer::Context ctx{
        m_masterBufferD.data(),         // masterBuffer
        numFrames,                      // numFrames
        bufferOffset,                   // bufferOffset
        m_globalSamplePos.load(std::memory_order_relaxed), // globalPos
        (uint32_t)m_sampleRate.load(std::memory_order_relaxed), // sampleRate
        &graph,                          // graph (const pointer)
        (double)m_loudnessState.integratedLufs.load(std::memory_order_relaxed)
    };
    
    // Execute Render
    m_rtRenderer.renderBlock(ctx, state, *this);
}


TrackRTState& AudioEngine::ensureTrackState(uint32_t trackIndex) {
    if (m_trackState.empty() || trackIndex >= m_trackState.size()) {
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
        // [REF] Use m_graphStates instead of m_renderTracks
        auto& targetState = m_graphStates[inactiveIdx];
        auto& targetOrder = targetState.renderTracks;

        targetOrder.clear();
        
        auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_relaxed);
        if (!slotMap) return;
        
        targetOrder.reserve(slotMap->getChannelCount());
        
        // Access the current graph snapshot
        const auto& graph = m_state.activeGraph(); 
        
        // Iterate Tracks directly from the graph snapshot
        for (const auto& tr : graph.tracks) {
             const uint32_t idx = tr.trackIndex;
             
             // Safety Check
             if (idx >= m_trackBuffersD.size()) continue;

             RenderTrack rt;
             rt.trackIndex = idx;
             rt.selfBuffer = m_trackBuffersD[idx].data();
             
             // --- Main Output Routing ---
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
        
        // Ensure trackStates vector is sized to match track count
        // This is critical for AudioRenderer::renderBlock to work
        const size_t maxTrackIdx = m_trackBuffersD.size();
        if (targetState.trackStates.size() < maxTrackIdx) {
            targetState.trackStates.resize(maxTrackIdx);
        }
        
        // Sync mute/solo state from the main track state vector
        for (size_t i = 0; i < std::min(m_trackState.size(), targetState.trackStates.size()); ++i) {
            targetState.trackStates[i].mute = m_trackState[i].mute;
            targetState.trackStates[i].solo = m_trackState[i].solo;
            targetState.trackStates[i].soloSafe = m_trackState[i].soloSafe;
            // Keep gain smoother state intact (don't reset)
        }
        
        // Atomic Swap
        m_activeRenderTrackIndex.store(inactiveIdx, std::memory_order_release);
    }
    


    // [HYBRID ENGINE] Parallel logic removed

    
    // Helper to render clips for a specific track into its buffer
    // Extracted from original renderGraph logic
#if 0
    void AudioEngine::renderClipAudio(double* outputBuffer, TrackRTState& state, uint32_t trackIndex, const AudioGraph& graph, uint32_t numFrames, uint32_t bufferOffset) {
         /* [HYBRID ENGINE] Legacy Logic Removed
        // Suppress clips in Pattern Mode
        if (m_patternPlaybackMode.load(std::memory_order_relaxed)) {
            return;
        }

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
        // 1. Process Clips (Audio Sourcing)
        // Only render clips if transport is playing or we are scrubbing (future)
        bool isPlaying = m_transportPlaying.load(std::memory_order_relaxed);
        if (isPlaying) {
             const auto& clips = track.clips;
             if (!clips.empty()) {
                // double* buffer = m_trackBuffersD[trackIdx].data(); // This line was incorrect in the instruction, using outputBuffer
                 
                for (const auto& clip : clips) {
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
        */
    }
#endif


    // [HYBRID ENGINE] Moved to AudioRenderer



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
                    auto sampler = std::dynamic_pointer_cast<Nomad::Audio::Plugins::SamplerPlugin>(unitState.plugin);
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
                    auto sampler = std::dynamic_pointer_cast<Nomad::Audio::Plugins::SamplerPlugin>(unitState.plugin);
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






// TODO: [v4.1 Render Panel] The current bounce implementation runs on the main thread and
// can cause UI/audio crackling during the first few seconds. Solutions:
// 1. Move bounce to a dedicated worker thread with proper audio engine mutex coordination
// 2. Pause the real-time audio callback during bounce (may cause audible gap)
// 3. Implement a proper "Render to Disk" panel with progress bar and preview
// See: Render/Export Panel design task

bool AudioEngine::bounceRangeToWav(double startBeat, double endBeat, const std::string& outputPath, int32_t trackId) {
    if (startBeat >= endBeat) return false;

    uint32_t sampleRate = getSampleRate();
    if (sampleRate == 0) sampleRate = 48000;

    // 1. Initialize Encoder
    ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sampleRate);
    ma_encoder encoder;
    
#ifdef _WIN32
    // Use wide-string version for Windows to handle unicode paths
    std::wstring widePath = pathStringToWide(outputPath);
    if (ma_encoder_init_file_w(widePath.c_str(), &config, &encoder) != MA_SUCCESS) {
#else
    if (ma_encoder_init_file(outputPath.c_str(), &config, &encoder) != MA_SUCCESS) {
#endif
        Nomad::Log::error("Failed to initialize WAV encoder for bounce: " + outputPath);
        return false;
    }

    // 2. Setup rendering state
    const float bpm = getBPM();
    const double samplesPerBeat = (static_cast<double>(sampleRate) * 60.0) / static_cast<double>(bpm);
    const uint64_t startPos = static_cast<uint64_t>(startBeat * samplesPerBeat);
    const uint64_t endPos = static_cast<uint64_t>(endBeat * samplesPerBeat);
    
    if (startPos >= endPos) {
        ma_encoder_uninit(&encoder);
        Nomad::Log::error("Bounce cancelled: invalid range (startBeat=" + std::to_string(startBeat) + ", endBeat=" + std::to_string(endBeat) + ")");
        return false;
    }

    const uint64_t totalFrames = endPos - startPos;

    // 2. Setup rendering state (Isolated)
    const int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_acquire);
    AudioGraphState bounceState = m_graphStates[activeIdx]; // Snapshot state
    
    // Isolated Render setup
    const uint32_t blockSize = 1024;
    // Note: We use m_masterBufferD because the pre-compiled routing connections point there.
    // The isOffline flag prevents metering interference.
    if (m_masterBufferD.size() < blockSize * 2) {
        m_masterBufferD.assign(blockSize * 2, 0.0);
    }
    std::vector<float> bounceBuffer(blockSize * 2);      // Stereo float interleaved
    
    uint64_t renderedFrames = 0;
    Nomad::Log::info("Starting isolated bounce: " + std::to_string(totalFrames) + " frames to " + outputPath);

    while (renderedFrames < totalFrames) {
        uint32_t toRender = static_cast<uint32_t>(std::min<uint64_t>(blockSize, totalFrames - renderedFrames));
        
        AudioRenderer::Context ctx;
        ctx.masterBuffer = m_masterBufferD.data();
        ctx.numFrames = toRender;
        ctx.bufferOffset = 0;
        ctx.globalPos = startPos + renderedFrames;
        ctx.sampleRate = sampleRate;
        ctx.graph = &m_state.activeGraph();
        ctx.isolatedTrackIndex = trackId;
        ctx.isOffline = true;

        // Clear master buffer
        std::fill(m_masterBufferD.begin(), m_masterBufferD.begin() + toRender * 2, 0.0);
        
        // 1. Render Graph (Isolated)
        m_rtRenderer.renderBlock(ctx, bounceState, *this);
        
        // 2. Arsenal units (mix in if master bounce)
        if (trackId < 0) {
            m_rtRenderer.processArsenalUnits(ctx, *this);
        }

        // 3. Convert Double to Float + Safety Processing
        float* outPtr = bounceBuffer.data();
        const double* inPtr = m_masterBufferD.data();
        const bool safetyEnabled = m_safetyProcessingEnabled.load(std::memory_order_relaxed);
        
        for (uint32_t i = 0; i < toRender; ++i) {
            double l = inPtr[i * 2 + 0];
            double r = inPtr[i * 2 + 1];
            
            if (safetyEnabled) {
                l = softClipD(l);
                r = softClipD(r);
            }
            
            outPtr[i * 2 + 0] = static_cast<float>(l);
            outPtr[i * 2 + 1] = static_cast<float>(r);
        }

        // Write to file
        ma_encoder_write_pcm_frames(&encoder, bounceBuffer.data(), toRender, NULL);
        
        renderedFrames += toRender;
    }

    // 4. Cleanup
    ma_encoder_uninit(&encoder);
    
    Nomad::Log::info("Bounce complete: " + std::to_string(renderedFrames) + " frames saved.");
    return true;
}

} // namespace Audio
} // namespace Nomad