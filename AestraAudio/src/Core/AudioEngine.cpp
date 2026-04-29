// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"

#include "../../AestraCore/include/AestraLog.h"
#include "../../AestraCore/include/AestraMath.h"
#include "AuditionEngine.h"
#include "EffectChain.h" // [NEW]
#include "PathUtils.h"   // [NEW] For robust path conversion
#include "PatternPlaybackEngine.h"
#include "Playback/PreviewEngine.h"
#include "Plugin/SamplerPlugin.h" // [NEW]
#include "PluginHost.h"
#include "UnitManager.h"
#include "miniaudio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <queue>
#if defined(_MSC_VER) || defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h> // AVX/SSE for high-performance mixing
#endif
#include <map>

// Denormal protection macros
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define DISABLE_DENORMALS        \
    int oldMXCSR = _mm_getcsr(); \
    _mm_setcsr(oldMXCSR | 0x8040); // Set DAZ and FTZ flags

#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#else
// Non-x86: no denormal control needed (ARM FPU handles this differently)
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#endif

namespace Aestra {
namespace Audio {

namespace {
inline double clampD(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

inline double dbToLinearD(double db) {
    // UI uses -90 dB as "silence"
    if (db <= -90.0)
        return 0.0;
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
        const uint8_t allSoundOff[3] = {static_cast<uint8_t>(0xB0u | ch), 120u, 0u};
        const uint8_t resetControllers[3] = {static_cast<uint8_t>(0xB0u | ch), 121u, 0u};
        const uint8_t allNotesOff[3] = {static_cast<uint8_t>(0xB0u | ch), 123u, 0u};
        buf.addEvent(0, allSoundOff, 3);
        buf.addEvent(0, resetControllers, 3);
        buf.addEvent(0, allNotesOff, 3);
    }
}
} // namespace

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

void AudioEngine::startMetronomeCountIn(uint32_t beats) {
    m_pendingMetronomeCountInStop.store(false, std::memory_order_release);
    m_pendingMetronomeCountInBeats.store(std::max<uint32_t>(1, beats), std::memory_order_release);
    m_metronomeCountInActive.store(true, std::memory_order_release);
}

void AudioEngine::stopMetronomeCountIn() {
    m_pendingMetronomeCountInBeats.store(0, std::memory_order_release);
    m_pendingMetronomeCountInStop.store(true, std::memory_order_release);
    m_metronomeCountInActive.store(false, std::memory_order_release);
}

void AudioEngine::clearMetronomeCountInRt() {
    m_metronomeCountInActive.store(false, std::memory_order_relaxed);
    m_metronomeCountInRemainingSamples.store(0, std::memory_order_relaxed);
    m_metronomeCountInSamplePos.store(0, std::memory_order_relaxed);
}

void AudioEngine::applyPendingMetronomeCountInRt() {
    if (m_pendingMetronomeCountInStop.exchange(false, std::memory_order_acq_rel)) {
        clearMetronomeCountInRt();
    }

    const uint32_t beats = m_pendingMetronomeCountInBeats.exchange(0, std::memory_order_acq_rel);
    if (beats == 0) {
        return;
    }

    const uint32_t sampleRate = std::max(1u, m_sampleRate.load(std::memory_order_relaxed));
    const double bpm = std::max(1.0f, m_metronomeEngine.getBPM());
    const uint64_t samplesPerBeat =
        static_cast<uint64_t>((static_cast<double>(sampleRate) * 60.0) / bpm);
    const uint64_t totalSamples = std::max<uint64_t>(samplesPerBeat, samplesPerBeat * beats);
    if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
        m_fadeState.store(FadeState::None, std::memory_order_relaxed);
    }
    m_metronomeEngine.reset(0, sampleRate);
    m_metronomeCountInSamplePos.store(0, std::memory_order_relaxed);
    m_metronomeCountInRemainingSamples.store(totalSamples, std::memory_order_relaxed);
    m_metronomeCountInActive.store(true, std::memory_order_relaxed);
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
        case AudioQueueCommandType::AuditionUnit: {
            m_unitAuditionState.unitId = static_cast<UnitID>(cmd.trackIndex);
            m_unitAuditionState.note = static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value1), 0, 127));
            m_unitAuditionState.velocity =
                static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value2 * 127.0f), 1, 127));
            m_unitAuditionState.noteOffSamplesRemaining =
                std::max<uint32_t>(1, m_sampleRate.load(std::memory_order_relaxed) / 8);
            m_unitAuditionState.noteOnPending = true;
            m_unitAuditionState.active = true;
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
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
            m_fadeSamplesRemaining = FADE_IN_SAMPLES;
        } else if (transportPlaying && m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
            uint32_t fadeProgress = FADE_OUT_SAMPLES - m_fadeSamplesRemaining;
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
            m_fadeSamplesRemaining = std::min(fadeProgress, FADE_IN_SAMPLES);
        }
    }
}

void AudioEngine::injectPendingUnitAudition(PatternPlaybackEngine::UnitMidiRoute* routes, size_t routeCount,
                                            uint32_t numFrames) noexcept {
    if (!m_unitAuditionState.active || !routes || routeCount == 0 || numFrames == 0) {
        return;
    }

    MidiBuffer* target = nullptr;
    for (size_t i = 0; i < routeCount; ++i) {
        if (routes[i].unitId == m_unitAuditionState.unitId) {
            target = routes[i].midiBuffer;
            break;
        }
    }

    if (!target) {
        return;
    }

    if (m_unitAuditionState.noteOnPending) {
        target->addNoteOn(1, m_unitAuditionState.note, m_unitAuditionState.velocity, 0);
        m_unitAuditionState.noteOnPending = false;
    }

    if (m_unitAuditionState.noteOffSamplesRemaining <= numFrames) {
        const uint32_t noteOffOffset = std::min(numFrames - 1, m_unitAuditionState.noteOffSamplesRemaining - 1);
        target->addNoteOff(1, m_unitAuditionState.note, 0, noteOffOffset);
        m_unitAuditionState.noteOffSamplesRemaining = 0;
        m_unitAuditionState.active = false;
    } else {
        m_unitAuditionState.noteOffSamplesRemaining -= numFrames;
    }
}

void AudioEngine::setThreadCount(int count) {
    if (count < 1)
        count = 1;
    if (count > 64)
        count = 64;

    if (!m_threadPool || m_threadPool->size() != (size_t)count) {
        m_threadPool = std::make_unique<Aestra::RealTimeThreadPool>(count);
        m_syncBarrier = std::make_unique<Aestra::Barrier>(0);
    }
}

void AudioEngine::refreshSamplerCache() {
    std::array<Plugins::SamplerPlugin*, kMaxCachedSamplers> samplers{};
    size_t count = 0;

    auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
    if (unitMgr) {
        auto snapshot = unitMgr->getAudioSnapshot();
        if (snapshot) {
            for (const auto& unitState : snapshot->units) {
                if (count >= samplers.size()) {
                    break;
                }
                auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unitState.plugin);
                if (sampler) {
                    samplers[count++] = sampler.get();
                }
            }
        }
    }

    m_cachedSamplers = samplers;
    m_cachedSamplerCount.store(count, std::memory_order_release);
}

void AudioEngine::resetCachedSamplerVoicesRt() noexcept {
    const size_t cachedCount = m_cachedSamplerCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < cachedCount && i < m_cachedSamplers.size(); ++i) {
        if (m_cachedSamplers[i]) {
            m_cachedSamplers[i]->requestHardResetVoices();
        }
    }
}

void AudioEngine::syncCachedSamplerSampleRatesRt(uint32_t sampleRate) noexcept {
    if (m_lastSyncedArsenalSampleRate == sampleRate) {
        return;
    }
    const size_t cachedCount = m_cachedSamplerCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < cachedCount && i < m_cachedSamplers.size(); ++i) {
        if (m_cachedSamplers[i]) {
            m_cachedSamplers[i]->setSampleRate(static_cast<double>(sampleRate));
        }
    }
    m_lastSyncedArsenalSampleRate = sampleRate;
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
 * @param outputBuffer Interleaved stereo output buffer to fill (must be non-null and sized for numFrames * output
 * channels).
 * @param inputBuffer Optional interleaved input buffer; if provided and an input callback is registered, the callback
 * is invoked with this buffer.
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
int AudioEngine::processBlock(float* outputBuffer, const float* inputBuffer, uint32_t numFrames, double streamTime) {
    (void)streamTime;

    // Process Input (Recording)
    if (inputBuffer) {
        auto cb = m_inputCallback.load(std::memory_order_relaxed);
        if (cb) {
            cb(inputBuffer, numFrames, m_inputCallbackData.load(std::memory_order_relaxed));
        }
    }

    if (!outputBuffer || numFrames == 0) {
        return 0;
    }

    // Enable Denormals protection (Flush-to-Zero)
    DISABLE_DENORMALS

    // Safety: If buffers aren't allocated (setBufferConfig not called), silence and return.
    const uint32_t numOutputChannels = m_outputChannels.load(std::memory_order_relaxed);
    if (numOutputChannels == 0 || m_masterBufferD.empty()) {
        std::memset(outputBuffer, 0,
                    static_cast<size_t>(numFrames) * numOutputChannels * sizeof(float));
        RESTORE_DENORMALS
        return 0;
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

    applyPendingMetronomeCountInRt();

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
        if (pe)
            pe->flush();

        resetCachedSamplerVoicesRt();

        // Force silence immediately.
        m_fadeState.store(FadeState::Silent, std::memory_order_relaxed);
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
        if (pe)
            pe->flush();
    }

    // On transport restart (play start or seek):
    // - flush pattern queue
    // - in pattern mode, cut any still-playing one-shots so the loop restarts audibly
    if (transportRestart) {
        auto* pe = m_patternEngine.load(std::memory_order_relaxed);
        if (pe)
            pe->flush();

        if (patternModeNow) {
            resetCachedSamplerVoicesRt();
        }
    }

    // When starting playback, always ensure we're fading in (or already fading in)
    // This prevents the audio from jumping to full volume instantly → no clicks
    // [FIX] Also critical for recovering from Panic (Silent) state
    if (isPlaying) {
        if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent || (!wasPlaying && m_fadeState.load(std::memory_order_relaxed) != FadeState::FadingIn)) {
            m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
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
            audition->processBlock(outputBuffer, numFrames, numOutputChannels);

            // Simple metering for Audition (optional, but good for UI feedback)
            float audPeakL = 0.0f, audPeakR = 0.0f;
            for (uint32_t i = 0; i < numFrames; ++i) {
                float l = std::abs(outputBuffer[i * 2]);
                float r = std::abs(outputBuffer[i * 2 + 1]);
                if (l > audPeakL)
                    audPeakL = l;
                if (r > audPeakR)
                    audPeakR = r;
            }
            m_peakL.store(audPeakL, std::memory_order_relaxed);
            m_peakR.store(audPeakR, std::memory_order_relaxed);

            m_telemetry.incrementBlocksProcessed();
            RESTORE_DENORMALS
            return 0;
        }
    }

    // Fast path: silent (only for normal DAW mode, not audition)
    if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
        std::memset(outputBuffer, 0,
                    static_cast<size_t>(numFrames) * numOutputChannels * sizeof(float));
        // Clear meters so UI doesn't freeze on the last loud block.
        m_peakL.store(0.0f, std::memory_order_relaxed);
        m_peakR.store(0.0f, std::memory_order_relaxed);
        m_rmsL.store(0.0f, std::memory_order_relaxed);
        m_rmsR.store(0.0f, std::memory_order_relaxed);
        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        if (snaps) {
            snaps->writeLevels(ChannelSlotMap::MASTER_SLOT_INDEX,
                               0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -144.0f);
            snaps->clearClip(ChannelSlotMap::MASTER_SLOT_INDEX);
            // Also clears all track slots so they don't freeze
            for (uint32_t i = 0; i < ChannelSlotMap::MAX_CHANNEL_SLOTS; ++i) {
                snaps->writeLevels(i, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -144.0f);
                snaps->writeSidechainPeak(i, 0.0f);
            }
        }
        m_telemetry.incrementBlocksProcessed();
        RESTORE_DENORMALS
        return 0;
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

    if (playing || m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
        nextGlobalPos += numFrames;

        if (loopEnabled) {
            float bpm = m_metronomeEngine.getBPM();
            // Convert loop end beat to sample position
            double samplesPerBeat =
                (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) / std::max(static_cast<double>(bpm), 1.0);
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
                    if (pe)
                        pe->flush();
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
                      m_masterBufferD.begin() +
                          static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed),
                      0.0);
        }
    } else {
        // Zero the double buffer
        std::fill(m_masterBufferD.begin(),
                  m_masterBufferD.begin() +
                      static_cast<size_t>(numFrames) * m_outputChannels.load(std::memory_order_relaxed),
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
            if (pe)
                pe->flush();
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
                m_masterBufferD[i * 2] += sample;     // L
                m_masterBufferD[i * 2 + 1] += sample; // R

                phase += phaseIncrement;
                if (phase >= twoPi)
                    phase -= twoPi;
            }
            m_testTonePhase = phase;
        }
    }

    // === Preview Ducking Gain Calculation ===
    // When transport is playing and preview is active, duck transport by ~6dB
    const double duckFadeTimeMs = 50.0;  // ~50ms smooth fade
    const double duckFadeSamples = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * duckFadeTimeMs) / 1000.0;
    const double duckFadeDelta = 1.0 / std::max(duckFadeSamples, 1.0);
    
    // Check if preview is active
    bool previewIsActive = false;
    auto* preview = m_previewEngine.load(std::memory_order_relaxed);
    if (preview) {
        previewIsActive = preview->isPlaying();
    }
    
    // Calculate target duck gain
    // When preview + transport both playing: duck to 0.5 (-6dB)
    // Otherwise: restore to 1.0 (0dB)
    const double targetDuckGain = (previewIsActive && isPlaying) ? 0.5 : 1.0;
    
    // Smooth the duck gain transition
    double duckGain = m_smoothedPreviewDuckGain;
    if (duckGain < targetDuckGain) {
        duckGain += duckFadeDelta;
        if (duckGain > targetDuckGain) duckGain = targetDuckGain;
    } else if (duckGain > targetDuckGain) {
        duckGain -= duckFadeDelta;
        if (duckGain < targetDuckGain) duckGain = targetDuckGain;
    }
    m_smoothedPreviewDuckGain = duckGain;
    
    // Publish smoothed gain for external queries
    m_previewDuckGain.store(static_cast<float>(duckGain), std::memory_order_relaxed);

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

    const double targetGain = static_cast<double>(m_masterGainTarget.load(std::memory_order_relaxed)) *
                              static_cast<double>(m_headroomLinear.load(std::memory_order_relaxed)) * masterParamGain;
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

    const bool limiterOn = m_safetyLimiterEnabled.load(std::memory_order_relaxed);
    const DitheringMode ditherMode = m_ditheringMode.load(std::memory_order_relaxed);

    // Deterministic Dithering: Seed RNG with global timeline position
    // This ensures that bouncing the same project twice produces identical bits.
    m_ditherRng.setSeed(static_cast<uint32_t>(m_globalSamplePos.load(std::memory_order_relaxed)) ^ 0x9E3779B9);

    // Optimized output loop - minimal branches
    for (uint32_t i = 0; i < numFrames; ++i) {
        // Read from double buffer (apply master gain and preview ducking)
        double L = src[i * 2] * gain * duckGain;
        double R = src[i * 2 + 1] * gain * duckGain;

        if (limiterOn) {
            m_safetyLimiter.process(L, R);
        }

        // Track peaks
        const double absL = (L >= 0.0) ? L : -L;
        const double absR = (R >= 0.0) ? R : -R;
        if (absL > peakL)
            peakL = absL;
        if (absR > peakR)
            peakR = absR;

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

        snaps->writeLevels(ChannelSlotMap::MASTER_SLOT_INDEX, masterPeakL, masterPeakR, masterRmsL, masterRmsR,
                           masterLowL, masterLowR, masterCorr, integratedLufs);

        if (masterPeakL >= 1.0f || masterPeakR >= 1.0f) {
            snaps->setClip(ChannelSlotMap::MASTER_SLOT_INDEX, masterPeakL >= 1.0f, masterPeakR >= 1.0f);
        }
    }

    // --- LUFS Block Push (Output) ---
    m_loudnessState.blockSamples += numFrames;
    if (m_loudnessState.blockSamples >= 19200) {
        double avgBlockEnergy = m_loudnessState.blockEnergySum /
                                static_cast<double>(m_loudnessState.blockSamples); // Use accumulated sample count
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
    if (m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingIn) {
        const double fadeTotal = static_cast<double>(FADE_IN_SAMPLES);
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (m_fadeSamplesRemaining == 0) {
                m_fadeState.store(FadeState::None, std::memory_order_relaxed);
                break;
            }
            const double progress = 1.0 - (static_cast<double>(m_fadeSamplesRemaining) / fadeTotal);
            const double fadeGain = progress * progress * (3.0 - 2.0 * progress); // Smoothstep
            outputBuffer[i * 2] *= static_cast<float>(fadeGain);
            outputBuffer[i * 2 + 1] *= static_cast<float>(fadeGain);
            --m_fadeSamplesRemaining;
        }
    } else if (m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
        const double fadeTotal = static_cast<double>(FADE_OUT_SAMPLES);
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (m_fadeSamplesRemaining == 0) {
                std::memset(outputBuffer + i * 2, 0,
                            static_cast<size_t>(numFrames - i) * numOutputChannels *
                                sizeof(float));
m_fadeState.store(FadeState::Silent, std::memory_order_relaxed);
                break;
            }
            const double t = static_cast<double>(m_fadeSamplesRemaining) / fadeTotal;
            const double fadeGain = t * t * (3.0 - 2.0 * t); // Smoothstep

            outputBuffer[i * 2] *= static_cast<float>(fadeGain);
            outputBuffer[i * 2 + 1] *= static_cast<float>(fadeGain);
            --m_fadeSamplesRemaining;
        }
    }

    // === Metronome Click Mixing ===
    if (m_metronomeCountInActive.load(std::memory_order_relaxed) &&
        !m_transportPlaying.load(std::memory_order_relaxed)) {
        const uint64_t prerollPos = m_metronomeCountInSamplePos.load(std::memory_order_relaxed);
        const uint64_t remaining = m_metronomeCountInRemainingSamples.load(std::memory_order_relaxed);
        const uint32_t framesToRender = static_cast<uint32_t>(std::min<uint64_t>(numFrames, remaining));
        if (framesToRender > 0) {
            m_metronomeEngine.process(outputBuffer, framesToRender, m_outputChannels.load(std::memory_order_relaxed),
                                      prerollPos, static_cast<uint32_t>(m_sampleRate.load(std::memory_order_relaxed)),
                                      true);
        }

        if (remaining <= framesToRender) {
            clearMetronomeCountInRt();
        } else {
            m_metronomeCountInRemainingSamples.store(remaining - framesToRender, std::memory_order_relaxed);
            m_metronomeCountInSamplePos.store(prerollPos + framesToRender, std::memory_order_relaxed);
        }
    }

    m_metronomeEngine.process(outputBuffer, numFrames, m_outputChannels.load(std::memory_order_relaxed),
                              m_globalSamplePos.load(std::memory_order_relaxed),
                              static_cast<uint32_t>(m_sampleRate.load(std::memory_order_relaxed)),
                              m_transportPlaying.load(std::memory_order_relaxed));

    // Advance position (Atomic update to pre-calculated next position)
    if (m_transportPlaying.load(std::memory_order_relaxed) || m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
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
    return 0;
}

void AudioEngine::setBufferConfig(uint32_t maxFrames, uint32_t numChannels) {
    // Treat maxFrames as a hint; never shrink RT buffers.
    // Some drivers deliver larger blocks than requested, and shrinking can cause
    // renderGraph() to early-out -> audible crackles.
    m_outputChannels.store(numChannels, std::memory_order_relaxed);
    if (maxFrames > m_maxBufferFrames.load(std::memory_order_relaxed)) {
        m_maxBufferFrames.store(maxFrames, std::memory_order_relaxed);
    }

    const size_t requiredSize = static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed)) *
                                m_outputChannels.load(std::memory_order_relaxed);
    const bool needAlloc = m_masterBufferD.size() < requiredSize || m_trackBuffersD.size() != kMaxTracks;

    if (needAlloc) {
        m_masterBufferD.resize(requiredSize);

        // Resize plugin scratch buffers (mono size)
        size_t monoSize = static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed));
        if (m_scratchL.size() < monoSize)
            m_scratchL.resize(monoSize);
        if (m_scratchR.size() < monoSize)
            m_scratchR.resize(monoSize);
        if (m_sidechainScratchL.size() < monoSize)
            m_sidechainScratchL.resize(monoSize);
        if (m_sidechainScratchR.size() < monoSize)
            m_sidechainScratchR.resize(monoSize);

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
        m_trackSidechainBuffersD.clear();
        m_trackSidechainBuffersD.resize(kMaxTracks);
        for (auto& buf : m_trackSidechainBuffersD) {
            buf.assign(requiredSize, 0.0);
        }
        if (m_trackState.size() != kMaxTracks) {
            m_trackState.assign(kMaxTracks, TrackRTState{});
        }

        // Pre-allocate all RT graph scratch vectors to avoid heap allocation in renderGraph.
        // Outer vectors sized to kMaxTracks; inner vectors pre-sized to kMaxEdgesPerTrack.
        m_rtTrackIndexById.assign(kMaxTracks, kMaxTracks); // sentinel = "not present"
        m_rtAudibleDownstream.resize(kMaxTracks);
        m_rtAudibleIncoming.resize(kMaxTracks);
        m_rtSidechainIncoming.resize(kMaxTracks);
        m_rtTopoEdges.resize(kMaxTracks);
        for (size_t i = 0; i < kMaxTracks; ++i) {
            m_rtAudibleDownstream[i].reserve(kMaxEdgesPerTrack);
            m_rtAudibleIncoming[i].reserve(kMaxEdgesPerTrack);
            m_rtSidechainIncoming[i].reserve(kMaxEdgesPerTrack);
            m_rtTopoEdges[i].reserve(kMaxEdgesPerTrack);
        }
        m_rtTopoIndegree.resize(kMaxTracks);
        m_rtAudibleEligible.resize(kMaxTracks);
        m_rtProcessActive.resize(kMaxTracks);
        m_rtProcessOrder.reserve(kMaxTracks);
        m_rtIndexQueue.reserve(kMaxTracks);
        m_rtSoloProcessQueue.reserve(kMaxTracks);
        m_rtCycleVisited.resize(kMaxTracks);

        // Pre-allocate send scratch buffers for each track slot.
        for (size_t i = 0; i < kMaxTracks; ++i) {
            m_trackState[i].sendGainL.reserve(kMaxSendsPerTrack);
            m_trackState[i].sendGainR.reserve(kMaxSendsPerTrack);
            m_trackState[i].preFaderBuffer.reserve(requiredSize);
        }

        // Pre-allocate prepared-routes scratch (max sends per track).
        m_preparedRoutesScratch.reserve(kMaxSendsPerTrack);

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
        for (auto& buf : m_trackSidechainBuffersD) {
            if (!buf.empty()) {
                VirtualLock(buf.data(), buf.size() * sizeof(double));
            }
        }
#endif
    }

    // Allocate waveform history ring (non-RT).
    if (m_waveformHistoryFrames.load(std::memory_order_relaxed) == 0) {
        m_waveformHistoryFrames.store(kWaveformHistoryFramesDefault, std::memory_order_relaxed);
    }
    const size_t historyRequired = static_cast<size_t>(m_waveformHistoryFrames.load(std::memory_order_relaxed)) *
                                   m_outputChannels.load(std::memory_order_relaxed);
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
    std::memcpy(outInterleaved, &m_waveformHistory[static_cast<size_t>(start) * stride],
                static_cast<size_t>(first) * stride * sizeof(float));
    if (frames > first) {
        std::memcpy(outInterleaved + static_cast<size_t>(first) * stride, m_waveformHistory.data(),
                    static_cast<size_t>(frames - first) * stride * sizeof(float));
    }
    return frames;
}

void AudioEngine::captureWaveformHistory(const float* interleavedOutput, uint32_t numFrames) {
    if (!interleavedOutput || numFrames == 0) {
        return;
    }

    const uint32_t historyCap = m_waveformHistoryFrames.load(std::memory_order_relaxed);
    if (historyCap == 0 || m_waveformHistory.empty()) {
        return;
    }

    const uint32_t cap = historyCap;
    uint32_t write = m_waveformWriteIndex.load(std::memory_order_relaxed);
    const uint32_t framesToCopy = std::min(numFrames, cap);
    const uint32_t first = std::min(framesToCopy, cap - write);
    const size_t stride = static_cast<size_t>(m_outputChannels.load(std::memory_order_relaxed));

    std::memcpy(&m_waveformHistory[static_cast<size_t>(write) * stride], interleavedOutput,
                static_cast<size_t>(first) * stride * sizeof(float));
    if (framesToCopy > first) {
        std::memcpy(m_waveformHistory.data(), interleavedOutput + static_cast<size_t>(first) * stride,
                    static_cast<size_t>(framesToCopy - first) * stride * sizeof(float));
    }

    write = (write + framesToCopy) % cap;
    m_waveformWriteIndex.store(write, std::memory_order_release);
}

// --- Constants ---
const AudioEngine::BiquadCoeff AudioEngine::kKWeightPreFilter = {
    1.53512485958697, -2.69169618940638, 1.19839281085285, // b0, b1, b2
    -1.69065929318241, 0.73248077421585                    // a1, a2
};

const AudioEngine::BiquadCoeff AudioEngine::kKWeightRLB = {
    1.0, -2.0, 1.0,                     // b0, b1, b2
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
    Aestra::Log::info("[AudioEngine] Created (Original Ctor). Ptr: " +
                      std::to_string(reinterpret_cast<uintptr_t>(this)));

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
    if (m_loudnessThreadRunning)
        return;
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
            if (!std::isfinite(blockEnergy) || blockEnergy < 0.0)
                blockEnergy = 0.0;

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

    // Solo detection and routed solo support.
    bool anySolo = false;
    // Reset flat track-index map (no deallocation — just overwrite active entries).
    const size_t prevActive = m_rtTrackIndexByIdActiveCount;
    for (size_t i = 0; i < prevActive; ++i) {
        // We don't track which IDs were written; reset all possible entries
        // below graph.tracks.size() since that's the only range we write.
    }
    // Simpler: just reset the entries we're about to write.
    // The flat vector is pre-sized to kMaxTracks in setBufferConfig().
    for (size_t i = 0; i < graph.tracks.size(); ++i) {
        const uint32_t tid = graph.tracks[i].trackId;
        if (tid < kMaxTracks) {
            m_rtTrackIndexById[tid] = kMaxTracks; // clear sentinel
        }
    }
    for (size_t i = 0; i < graph.tracks.size(); ++i) {
        const uint32_t tid = graph.tracks[i].trackId;
        if (tid < kMaxTracks) {
            m_rtTrackIndexById[tid] = i;
        }
    }
    m_rtTrackIndexByIdActiveCount = graph.tracks.size();

    // Reset per-track edge lists (clear inner vectors; outer vector already sized).
    const size_t numTracks = graph.tracks.size();
    for (size_t i = 0; i < numTracks; ++i) {
        m_rtAudibleDownstream[i].clear();
        m_rtAudibleIncoming[i].clear();
        m_rtSidechainIncoming[i].clear();
    }
    m_rtAudibleEligible.assign(availableTracks, false);
    m_rtProcessActive.assign(availableTracks, false);

    auto addTrackEdge = [&](size_t srcIndex, uint32_t destTrackId, bool sidechainOnly) {
        if (destTrackId >= kMaxTracks || m_rtTrackIndexById[destTrackId] == kMaxTracks) {
            return;
        }
        const size_t destIndex = m_rtTrackIndexById[destTrackId];
        if (destIndex == srcIndex) {
            return;
        }
        if (sidechainOnly) {
            m_rtSidechainIncoming[destIndex].push_back(srcIndex);
        } else {
            m_rtAudibleDownstream[srcIndex].push_back(destIndex);
            m_rtAudibleIncoming[destIndex].push_back(srcIndex);
        }
    };

    for (size_t i = 0; i < graph.tracks.size(); ++i) {
        const auto& tr = graph.tracks[i];
        auto& state = ensureTrackState(tr.trackIndex);
        if (tr.solo || state.solo) {
            anySolo = true;
        }
        if (tr.mainOutputId != 0xFFFFFFFFu) {
            addTrackEdge(i, tr.mainOutputId, false);
        }
        for (const auto& send : tr.sends) {
            if (send.mute || send.targetChannelId == 0xFFFFFFFFu) {
                continue;
            }
            addTrackEdge(i, send.targetChannelId, send.sidechainOnly);
        }
    }

    if (anySolo) {
        m_rtIndexQueue.clear();
        size_t audibleQueueRead = 0;
        m_rtSoloProcessQueue.clear();
        // Capacity already reserved in setBufferConfig(); no reserve() needed.
        size_t processQueueRead = 0;

        for (size_t i = 0; i < graph.tracks.size(); ++i) {
            const auto& tr = graph.tracks[i];
            auto& state = ensureTrackState(tr.trackIndex);
            const bool soloed = tr.solo || state.solo;
            const bool soloSafe = tr.isSoloSafe || state.soloSafe;
            if (!soloed && !soloSafe) {
                continue;
            }
            if (static_cast<size_t>(tr.trackIndex) < availableTracks &&
                !m_rtAudibleEligible[tr.trackIndex]) {
                m_rtAudibleEligible[tr.trackIndex] = true;
                m_rtIndexQueue.push_back(i);
            }
        }

        while (audibleQueueRead < m_rtIndexQueue.size()) {
            const size_t index = m_rtIndexQueue[audibleQueueRead++];
            m_rtSoloProcessQueue.push_back(index);
            for (const size_t destIndex : m_rtAudibleDownstream[index]) {
                const uint32_t destTrackIndex = graph.tracks[destIndex].trackIndex;
                if (static_cast<size_t>(destTrackIndex) >= availableTracks ||
                    m_rtAudibleEligible[destTrackIndex]) {
                    continue;
                }
                m_rtAudibleEligible[destTrackIndex] = true;
                m_rtIndexQueue.push_back(destIndex);
            }
        }

        while (processQueueRead < m_rtSoloProcessQueue.size()) {
            const size_t index = m_rtSoloProcessQueue[processQueueRead++];
            const uint32_t processTrackIndex = graph.tracks[index].trackIndex;
            if (static_cast<size_t>(processTrackIndex) < availableTracks &&
                !m_rtProcessActive[processTrackIndex]) {
                m_rtProcessActive[processTrackIndex] = true;
            }

            auto enqueueUpstream = [&](const std::vector<size_t>& upstream) {
                for (const size_t upstreamIndex : upstream) {
                    const uint32_t upstreamTrackIndex = graph.tracks[upstreamIndex].trackIndex;
                    if (static_cast<size_t>(upstreamTrackIndex) >= availableTracks ||
                        m_rtProcessActive[upstreamTrackIndex]) {
                        continue;
                    }
                    m_rtProcessActive[upstreamTrackIndex] = true;
                    m_rtSoloProcessQueue.push_back(upstreamIndex);
                }
            };

            enqueueUpstream(m_rtAudibleIncoming[index]);
            enqueueUpstream(m_rtSidechainIncoming[index]);
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
                if (unitMidiRouteCount >= unitMidiRoutes.size())
                    break;
                if (bufIdx >= m_scratchMidiBuffers.size())
                    break;
                if (unit.id != 0 && unit.plugin) {
                    m_scratchMidiBuffers[bufIdx].clear();
                    unitMidiRoutes[unitMidiRouteCount++] = PatternPlaybackEngine::UnitMidiRoute{
                        static_cast<UnitID>(unit.id), &m_scratchMidiBuffers[bufIdx]};
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
            if (isPlaying && patEng) {
                constexpr int LOOKAHEAD_SAMPLES = 2048; // ~40ms at 48kHz
                const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
                // Timeline playback needs to refill the pattern scheduler too, not just consume it.
                patEng->refillWindow(blockStart, static_cast<int>(sampleRate), LOOKAHEAD_SAMPLES);
                patEng->processAudio(blockStart, static_cast<int>(numFrames), unitMidiRoutes.data(),
                                     unitMidiRouteCount);
            }

            injectPendingUnitAudition(unitMidiRoutes.data(), unitMidiRouteCount, numFrames);
        }
    }

    // Clear all per-track buffers for this block up front so routed audio can
    // accumulate into destination tracks before they render/process.
    for (const auto& track : graph.tracks) {
        const uint32_t trackIdx = track.trackIndex;
        if (static_cast<size_t>(trackIdx) >= availableTracks) {
            continue;
        }
        auto& buffer = m_trackBuffersD[trackIdx];
        std::memset(buffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));
        auto& sidechainBuffer = m_trackSidechainBuffersD[trackIdx];
        std::memset(sidechainBuffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));
    }

    // Process tracks in audible topological order so any routed upstream
    // content reaches a destination before that destination runs inserts/fader/metering.
    m_rtProcessOrder.clear();
    // All vectors pre-allocated in setBufferConfig(); no reserve()/resize() needed.
    const size_t topoCount = graph.tracks.size();
    for (size_t i = 0; i < topoCount; ++i) {
        m_rtTopoIndegree[i] = 0u;
    }
    for (size_t i = 0; i < topoCount; ++i) {
        m_rtTopoEdges[i].clear();
    }
    for (size_t i = 0; i < topoCount; ++i) {
        const auto& track = graph.tracks[i];
        auto addEdge = [&](uint32_t destTrackId) {
            if (destTrackId >= kMaxTracks || m_rtTrackIndexById[destTrackId] == kMaxTracks) {
                return;
            }
            const size_t destIndex = m_rtTrackIndexById[destTrackId];
            if (destIndex == i) {
                return;
            }
            m_rtTopoEdges[i].push_back(destIndex);
            m_rtTopoIndegree[destIndex] += 1u;
        };

        if (track.mainOutputId != 0xFFFFFFFFu) {
            addEdge(track.mainOutputId);
        }
        for (const auto& send : track.sends) {
            if (send.mute || send.targetChannelId == 0xFFFFFFFFu) {
                continue;
            }
            addEdge(send.targetChannelId);
        }
    }

    m_rtIndexQueue.clear();
    size_t readyRead = 0;
    for (size_t i = 0; i < topoCount; ++i) {
        if (m_rtTopoIndegree[i] == 0u) {
            m_rtIndexQueue.push_back(i);
        }
    }
    while (readyRead < m_rtIndexQueue.size()) {
        const size_t index = m_rtIndexQueue[readyRead++];
        m_rtProcessOrder.push_back(index);
        for (const size_t destIndex : m_rtTopoEdges[index]) {
            if (--m_rtTopoIndegree[destIndex] == 0u) {
                m_rtIndexQueue.push_back(destIndex);
            }
        }
    }
    const bool cycleDetected = m_rtProcessOrder.size() != topoCount;
    if (cycleDetected) {
        if (!m_loggedRoutingCycleWarning) {
            Aestra::Log::warning("[AudioEngine] Routing cycle detected; cyclic routes will not render correctly.");
            m_loggedRoutingCycleWarning = true;
        }
        // Zero cycle-visited flags using index writes (no alloc).
        for (size_t i = 0; i < topoCount; ++i) {
            m_rtCycleVisited[i] = false;
        }
        for (const size_t index : m_rtProcessOrder) {
            m_rtCycleVisited[index] = true;
        }
        for (size_t i = 0; i < topoCount; ++i) {
            if (!m_rtCycleVisited[i]) {
                m_rtProcessOrder.push_back(i);
            }
        }
    } else {
        m_loggedRoutingCycleWarning = false;
    }

    for (const size_t orderedIndex : m_rtProcessOrder) {
        const auto& track = graph.tracks[orderedIndex];
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
            const double samplesPerBeat = (static_cast<double>(m_sampleRate.load(std::memory_order_relaxed)) * 60.0) /
                                          std::max(graph.bpm, 1.0);
            double currentBeat =
                (static_cast<double>(globalPos) / m_sampleRate.load(std::memory_order_relaxed)) * (graph.bpm / 60.0);
            for (const auto& curve : track.automationCurves) {
                if (curve.getAutomationTarget() == AutomationTarget::Volume) {
                    volTarget = curve.getValueAtBeat(currentBeat, samplesPerBeat);
                } else if (curve.getAutomationTarget() == AutomationTarget::Pan) {
                    panTarget = clampD(curve.getValueAtBeat(currentBeat, samplesPerBeat), -1.0, 1.0);
                }
            }
        }

        // Skip early (solo suppression only).
        // Muted tracks still render so meters keep moving, but they don't mix into master.
        const bool muted = track.mute || state.mute;
        const bool audibleEligible =
            !anySolo || (static_cast<size_t>(trackIdx) < availableTracks && m_rtAudibleEligible[trackIdx]);
        const bool processActive =
            !anySolo || (static_cast<size_t>(trackIdx) < availableTracks && m_rtProcessActive[trackIdx]);

        if (state.sendGainL.size() != track.sends.size()) {
            state.sendGainL.resize(track.sends.size());
            state.sendGainR.resize(track.sends.size());
            for (size_t sendIndex = 0; sendIndex < track.sends.size(); ++sendIndex) {
                double targetL = 0.0;
                double targetR = 0.0;
                fastPanGainsD(
                    clampD(static_cast<double>(track.sends[sendIndex].pan), -1.0, 1.0),
                    static_cast<double>(track.sends[sendIndex].gain),
                    targetL,
                    targetR);
                state.sendGainL[sendIndex].current = targetL;
                state.sendGainL[sendIndex].target = targetL;
                state.sendGainR[sendIndex].current = targetR;
                state.sendGainR[sendIndex].target = targetR;
            }
        }

        for (size_t sendIndex = 0; sendIndex < track.sends.size(); ++sendIndex) {
            double targetL = 0.0;
            double targetR = 0.0;
            fastPanGainsD(
                clampD(static_cast<double>(track.sends[sendIndex].pan), -1.0, 1.0),
                static_cast<double>(track.sends[sendIndex].gain),
                targetL,
                targetR);
            state.sendGainL[sendIndex].setTarget(targetL);
            state.sendGainR[sendIndex].setTarget(targetR);
        }

        if (!processActive) {
            auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
            if (snaps && slot != ChannelSlotMap::INVALID_SLOT) {
                snaps->writeLevels(slot, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -144.0f);
                snaps->writeSidechainPeak(slot, 0.0f);
            }
            continue;
        }

        // Buffers were already cleared up front so destination tracks can receive
        // routed audio before their own clips/effects are processed.
        auto& buffer = m_trackBuffersD[trackIdx];

        // Check isPlaying inside loop to avoid brace wrapping issues
        // [FIX] Suppress timeline clips if in Pattern Mode (Audio Isolation)
        bool patternMode = m_patternPlaybackMode.load(std::memory_order_relaxed);

        if (!patternMode) {
            for (const auto& clip : track.clips) {
                if (!isPlaying)
                    continue;
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
                if (framesToRender == 0)
                    continue;

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
                                fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                       static_cast<double>(fadeLen)));
                            }
                            if (projectSample + fadeLen > clip.endSample) {
                                fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                       static_cast<double>(fadeLen)));
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

                        dst[i * 2] += sL * clipGain * fade;
                        dst[i * 2 + 1] += sR * clipGain * fade;
                    }
                } else {
                    srcActiveThisBlock = true;
                    // Resampling - use selected quality, pre-compute end condition
                    const double phaseEnd = static_cast<double>(totalFrames);

                    if (channels == 1) {
                        // Mono Resampling — use the same quality interpolators as stereo.
                        // The mono path reads a single float per frame, so we duplicate to L/R.
                        switch (m_interpQuality.load(std::memory_order_relaxed)) {
                        case Interpolators::InterpolationQuality::Cubic:
                            for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                                double fade = 1.0;
                                const uint64_t projectSample = start + i;
                                if (fadeLen > 0) {
                                    if (projectSample < clip.startSample + fadeLen)
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                float sample = 0.0f;
                                uint64_t idx = static_cast<uint64_t>(phase);
                                double frac = phase - static_cast<double>(idx);
                                // Catmull-Rom 4-point on mono data
                                float s0 = (idx > 0) ? data[idx - 1] : data[idx];
                                float s1 = data[idx];
                                float s2 = (idx + 1 < totalFrames) ? data[idx + 1] : data[idx];
                                float s3 = (idx + 2 < totalFrames) ? data[idx + 2] : s2;
                                double f = frac;
                                sample = static_cast<float>(0.5 * ((2.0 * s1) + (-s0 + s2) * f +
                                    (2.0 * s0 - 5.0 * s1 + 4.0 * s2 - s3) * f * f +
                                    (-s0 + 3.0 * s1 - 3.0 * s2 + s3) * f * f * f));
                                dst[i * 2] += sample * clipGain * fade;
                                dst[i * 2 + 1] += sample * clipGain * fade;
                                phase += ratio;
                            }
                            continue;
                        case Interpolators::InterpolationQuality::Sinc8:
                        case Interpolators::InterpolationQuality::Sinc16:
                        case Interpolators::InterpolationQuality::Sinc32:
                        case Interpolators::InterpolationQuality::Sinc64:
                            // Sinc on mono: compute weighted sum, duplicate to L/R
                            for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                                double fade = 1.0;
                                const uint64_t projectSample = start + i;
                                if (fadeLen > 0) {
                                    if (projectSample < clip.startSample + fadeLen)
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                double val = Interpolators::sincInterpolateMono(data, totalFrames, phase,
                                    m_interpQuality.load(std::memory_order_relaxed));
                                dst[i * 2] += val * clipGain * fade;
                                dst[i * 2 + 1] += val * clipGain * fade;
                                phase += ratio;
                            }
                            continue;
                        default:
                            // Linear fallback
                            for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                                double fade = 1.0;
                                const uint64_t projectSample = start + i;
                                if (fadeLen > 0) {
                                    if (projectSample < clip.startSample + fadeLen)
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) / static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) / static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                uint64_t idx = static_cast<uint64_t>(phase);
                                double frac = phase - static_cast<double>(idx);
                                float s0 = data[idx];
                                float s1 = (idx + 1 < totalFrames) ? data[idx + 1] : s0;
                                double val = s0 + frac * (s1 - s0);
                                dst[i * 2] += val * clipGain * fade;
                                dst[i * 2 + 1] += val * clipGain * fade;
                                phase += ratio;
                            }
                            continue;
                        }
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
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                           static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                           static_cast<double>(fadeLen)));
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
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                           static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                           static_cast<double>(fadeLen)));
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
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                           static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                           static_cast<double>(fadeLen)));
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
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                           static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                           static_cast<double>(fadeLen)));
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
                                    fade = std::min(fade, (static_cast<double>(projectSample - clip.startSample) /
                                                           static_cast<double>(fadeLen)));
                                }
                                if (projectSample + fadeLen > clip.endSample) {
                                    fade = std::min(fade, (static_cast<double>(clip.endSample - projectSample) /
                                                           static_cast<double>(fadeLen)));
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

                    float* outputs[2] = {m_scratchL.data(), m_scratchR.data()};

                    // Process Plugin
                    unit.plugin->process(nullptr, outputs, 0, 2, numFrames, midiBuf, nullptr);

                    // Mix to Track Buffer (Double Precision)
                    double* dDst = buffer.data();
                    for (uint32_t k = 0; k < numFrames; ++k) {
                        dDst[k * 2] += static_cast<double>(outputs[0][k]);
                        dDst[k * 2 + 1] += static_cast<double>(outputs[1][k]);
                    }
                }
            }
        }

        // === Plugin Processing (EffectChain) ===
        float trackSidechainPeak = 0.0f;
        if (track.effectChain && track.effectChain->getActiveSlotCount() > 0) {
            // Check if scratches are large enough (should be from setBufferConfig)
            if (m_scratchL.size() >= numFrames && m_scratchR.size() >= numFrames &&
                m_sidechainScratchL.size() >= numFrames && m_sidechainScratchR.size() >= numFrames) {
                // 1. De-interleave Double -> Float
                const double* dBuf = buffer.data();
                const double* dScBuf = m_trackSidechainBuffersD[trackIdx].data();
                float* fL = m_scratchL.data();
                float* fR = m_scratchR.data();
                float* scL = m_sidechainScratchL.data();
                float* scR = m_sidechainScratchR.data();
                // Allow vectorization
                for (uint32_t k = 0; k < numFrames; ++k) {
                    fL[k] = static_cast<float>(dBuf[k * 2]);
                    fR[k] = static_cast<float>(dBuf[k * 2 + 1]);
                    scL[k] = static_cast<float>(dScBuf[k * 2]);
                    scR[k] = static_cast<float>(dScBuf[k * 2 + 1]);
                    trackSidechainPeak =
                        std::max(trackSidechainPeak, std::max(std::abs(scL[k]), std::abs(scR[k])));
                }

                // 2. Process
                float* channels[2] = {fL, fR};
                const float* sidechainChannels[2] = {scL, scR};
                track.effectChain->process(channels, 2, numFrames, sidechainChannels, 2);

                // 3. Re-interleave Float -> Double
                double* dOut = buffer.data();
                for (uint32_t k = 0; k < numFrames; ++k) {
                    dOut[k * 2] = static_cast<double>(fL[k]);
                    dOut[k * 2 + 1] = static_cast<double>(fR[k]);
                }
            }
        }

        // Route post-fader output to the selected main destination and any audible sends.
        double tL, tR;
        fastPanGainsD(panTarget, volTarget, tL, tR);
        state.gainL.setTarget(tL);
        state.gainR.setTarget(tR);

        const double* trackData = buffer.data();
        double peakTrackL = 0.0;
        double peakTrackR = 0.0;
        double rmsAccTrackL = 0.0;
        double rmsAccTrackR = 0.0;
        double lowAccTrackL = 0.0;
        double lowAccTrackR = 0.0;
        double sumLRTrack = 0.0; // Correlation accumulator

        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        const bool publishTrackSnapshot = (snaps && slot != ChannelSlotMap::INVALID_SLOT);
        if (publishTrackSnapshot) {
            snaps->writeSidechainPeak(slot, trackSidechainPeak);
        }
        double* lfStateL = publishTrackSnapshot ? &m_meterLfStateL[slot] : nullptr;
        double* lfStateR = publishTrackSnapshot ? &m_meterLfStateR[slot] : nullptr;

        const bool hasPreFaderSend = std::any_of(
            track.sends.begin(),
            track.sends.end(),
            [](const auto& send) { return !send.mute && !send.postFader; });
        if (hasPreFaderSend) {
            state.preFaderBuffer.resize(static_cast<size_t>(numFrames) * 2u);
            std::memcpy(state.preFaderBuffer.data(),
                        trackData,
                        static_cast<size_t>(numFrames) * 2u * sizeof(double));
        } else {
            state.preFaderBuffer.clear();
        }

        for (uint32_t i = 0; i < numFrames; ++i) {
            // Apply smoothed gain
            const double leftGain = state.gainL.next();
            const double rightGain = state.gainR.next();

            const double preL = trackData[i * 2];
            const double preR = trackData[i * 2 + 1];
            const double outL = preL * leftGain;
            const double outR = preR * rightGain;

            buffer[i * 2] = outL;
            buffer[i * 2 + 1] = outR;

            if (!muted) {
                if (audibleEligible && track.mainOutputId == 0xFFFFFFFFu) {
                    masterBuf[i * 2] += outL;
                    masterBuf[i * 2 + 1] += outR;
                } else if (audibleEligible && slotMap) {
                    const uint32_t destSlot = slotMap->getSlotIndex(track.mainOutputId);
                    if (destSlot != ChannelSlotMap::INVALID_SLOT && destSlot < availableTracks && destSlot != trackIdx &&
                        (!anySolo || m_rtAudibleEligible[destSlot])) {
                        auto& destBuffer = m_trackBuffersD[destSlot];
                        destBuffer[i * 2] += outL;
                        destBuffer[i * 2 + 1] += outR;
                    }
                }

            }

            const double absL = (outL >= 0.0) ? outL : -outL;
            const double absR = (outR >= 0.0) ? outR : -outR;
            if (absL > peakTrackL)
                peakTrackL = absL;
            if (absR > peakTrackR)
                peakTrackR = absR;

            if (publishTrackSnapshot) {
                rmsAccTrackL += outL * outL;
                rmsAccTrackR += outR * outR;

                const double lpL = *lfStateL + m_meterLfCoeff * (outL - *lfStateL);
                const double lpR = *lfStateR + m_meterLfCoeff * (outR - *lfStateR);
                *lfStateL = lpL;
                *lfStateR = lpR;
                lowAccTrackL += lpL * lpL;
                lowAccTrackR += lpR * lpR;
                sumLRTrack += outL * outR;
            }
        }

        if (!muted) {
            // Use pre-allocated member scratch buffer (no heap allocation in RT path).
            m_preparedRoutesScratch.clear();
            for (size_t sendIndex = 0; sendIndex < track.sends.size(); ++sendIndex) {
                const auto& send = track.sends[sendIndex];
                if (send.mute) {
                    continue;
                }

                PreparedSendRoute route;
                route.source = send.postFader ? buffer.data() : state.preFaderBuffer.data();
                route.gainL = &state.sendGainL[sendIndex];
                route.gainR = &state.sendGainR[sendIndex];

                if (send.sidechainOnly && send.targetChannelId != 0xFFFFFFFFu && slotMap) {
                    const uint32_t scDestSlot = slotMap->getSlotIndex(send.targetChannelId);
                    if (scDestSlot != ChannelSlotMap::INVALID_SLOT &&
                        scDestSlot < availableTracks &&
                        scDestSlot != trackIdx) {
                        route.dest = m_trackSidechainBuffersD[scDestSlot].data();
                        m_preparedRoutesScratch.push_back(route);
                    }
                    continue;
                }

                if (!audibleEligible) {
                    continue;
                }

                if (send.targetChannelId == 0xFFFFFFFFu) {
                    route.dest = masterBuf;
                    m_preparedRoutesScratch.push_back(route);
                    continue;
                }

                if (!slotMap) {
                    continue;
                }

                const uint32_t sendDestSlot = slotMap->getSlotIndex(send.targetChannelId);
                if (sendDestSlot != ChannelSlotMap::INVALID_SLOT &&
                    sendDestSlot < availableTracks &&
                    sendDestSlot != trackIdx &&
                    (!anySolo || m_rtAudibleEligible[sendDestSlot])) {
                    route.dest = m_trackBuffersD[sendDestSlot].data();
                    m_preparedRoutesScratch.push_back(route);
                }
            }

            for (const auto& route : m_preparedRoutesScratch) {
                for (uint32_t i = 0; i < numFrames; ++i) {
                    const double sendGainL = route.gainL->next();
                    const double sendGainR = route.gainR->next();
                    route.dest[i * 2] += route.source[i * 2] * sendGainL;
                    route.dest[i * 2 + 1] += route.source[i * 2 + 1] * sendGainR;
                }
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
        for (size_t sendIndex = 0; sendIndex < state.sendGainL.size(); ++sendIndex) {
            state.sendGainL[sendIndex].snap();
            state.sendGainR[sendIndex].snap();
        }
    }

    if (unitSnapshot) {
        for (const auto& unit : unitSnapshot->units) {
            if (unit.routeId >= 0 || !unit.enabled || !unit.plugin) {
                continue;
            }

            MidiBuffer* midiBuf = nullptr;
            for (size_t r = 0; r < unitMidiRouteCount; ++r) {
                if (unitMidiRoutes[r].unitId == unit.id) {
                    midiBuf = unitMidiRoutes[r].midiBuffer;
                    break;
                }
            }

            if (m_scratchL.size() < numFrames || m_scratchR.size() < numFrames) {
                continue;
            }

            float* outputs[2] = {m_scratchL.data(), m_scratchR.data()};
            unit.plugin->process(nullptr, outputs, 0, 2, numFrames, midiBuf, nullptr);

            for (uint32_t i = 0; i < numFrames; ++i) {
                masterBuf[i * 2] += static_cast<double>(outputs[0][i]);
                masterBuf[i * 2 + 1] += static_cast<double>(outputs[1][i]);
            }
        }
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
        endBeat = startBeat + 4.0; // Default to 1 bar (4 beats)
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
    if (!slotMap)
        return;

    targetOrder.reserve(slotMap->getChannelCount());

    // Access the current graph snapshot
    const auto& graph = m_state.activeGraph(); // Fixed method name

    // Iterate Tracks directly from the graph snapshot
    for (const auto& tr : graph.tracks) {
        const uint32_t idx = tr.trackIndex;

        // Safety Check
        if (idx >= m_trackBuffersD.size())
            continue;

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
        } else {
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
            if (send.mute || send.sidechainOnly)
                continue;

            double sendGainL = 0.0;
            double sendGainR = 0.0;
            fastPanGainsD(
                clampD(static_cast<double>(send.pan), -1.0, 1.0),
                static_cast<double>(send.gain),
                sendGainL,
                sendGainR);

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
    if (pe)
        pe->flush();
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

void AudioEngine::processArsenalUnits(uint32_t numFrames, uint32_t bufferOffset, uint64_t startFrame,
                                     double* targetBuffer, int32_t isolatedTrackIndex) {
    // [FIX] Only process Arsenal units in pattern/Arsenal mode
    // In Timeline mode, skip entirely to prevent audio bleed with wrong sample rate
    if (!m_patternPlaybackMode.load(std::memory_order_relaxed)) {
        return;
    }

    // Get dependencies (RT-safe)
    auto* patternEngine = m_patternEngine.load(std::memory_order_acquire);
    auto* unitManager = m_unitManager.load(std::memory_order_acquire);

    if (!patternEngine || !unitManager)
        return;

    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (sampleRate == 0)
        return;

    // Sync logic moved after snapshot retrieval

    // Continue rendering even when transport is stopped (one-shot/tails behavior).
    // But do not schedule new MIDI when stopped.
    const bool transportPlaying = m_transportPlaying.load(std::memory_order_relaxed);

    const uint64_t currentFrame = startFrame;

    // Get Arsenal snapshot for RT-safe unit iteration
    auto snapshot = unitManager->getAudioSnapshot();
    if (!snapshot || snapshot->units.empty()) {
        return;
    }

    syncCachedSamplerSampleRatesRt(sampleRate);

    const size_t requiredStereoSamples = static_cast<size_t>(numFrames) * 2;
    // Buffers must be pre-sized in setBufferConfig()
    if (m_unitBufferD.size() < requiredStereoSamples || m_pluginBufferF.size() < requiredStereoSamples ||
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
        if (unitMidiRouteCount >= unitMidiRoutes.size())
            break;
        if (bufIdx >= m_unitMidiBuffers.size())
            break;
        m_unitMidiBuffers[bufIdx].clear();
        unitMidiRoutes[unitMidiRouteCount++] =
            PatternPlaybackEngine::UnitMidiRoute{static_cast<UnitID>(unit.id), &m_unitMidiBuffers[bufIdx]};
        ++bufIdx;
    }

    // Refill and process pattern MIDI events while playing, or during offline bounce
    if (transportPlaying || targetBuffer != nullptr) {
        constexpr int LOOKAHEAD_SAMPLES = 2048; // ~40ms at 48kHz
        patternEngine->refillWindow(currentFrame, static_cast<int>(sampleRate), LOOKAHEAD_SAMPLES);
        patternEngine->processAudio(currentFrame, static_cast<int>(numFrames), unitMidiRoutes.data(),
                                    unitMidiRouteCount);
    }

    injectPendingUnitAudition(unitMidiRoutes.data(), unitMidiRouteCount, numFrames);

    // Process each unit plugin
    bufIdx = 0;

    // Inputs (Stereo Silence)
    const float* inputs[2] = {m_silentBufferF.data(), m_silentBufferF.data()};

    for (const auto& unit : snapshot->units) {
        if (!unit.enabled || !unit.plugin) {
            bufIdx++;
            continue;
        }

        // During bounce (targetBuffer set), skip PreviewToMaster units.
        // renderBlock handles PreviewToMaster via AudioRenderer::processArsenalUnits.
        // During live processBlock, all units process through m_masterBufferD.
        if (targetBuffer != nullptr && unit.getRouteMode() == ArsenalRouteMode::PreviewToMaster) {
            bufIdx++;
            continue;
        }

        // During isolated-track bounce, skip track-routed units not assigned
        // to the isolated track. routeId maps directly to the track index.
        if (isolatedTrackIndex >= 0 && unit.routeId >= 0 && unit.routeId != isolatedTrackIndex) {
            bufIdx++;
            continue;
        }

        // Clear plugin output buffer
        std::fill(m_pluginBufferF.begin(), m_pluginBufferF.begin() + requiredStereoSamples, 0.0f);

        // Output Pointers (Non-Interleaved)
        float* outputs[2] = {m_pluginBufferF.data(), m_pluginBufferF.data() + numFrames};

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
        double* masterD = (targetBuffer ? targetBuffer : m_masterBufferD.data()) + static_cast<size_t>(bufferOffset) * 2;
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
    if (endBeat <= startBeat)
        return false;

    // 1. Calculate length
    double sampleRate = (double)m_sampleRate.load(std::memory_order_relaxed);
    float bpm = m_metronomeEngine.getBPM();
    double samplesPerBeat = (sampleRate * 60.0) / std::max(static_cast<double>(bpm), 1.0);

    uint64_t startSample = static_cast<uint64_t>(startBeat * samplesPerBeat);
    uint64_t endSample = static_cast<uint64_t>(endBeat * samplesPerBeat);
    uint64_t totalFrames = endSample - startSample;

    if (totalFrames == 0)
        return false;

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
        if (wasPlaying)
            m_transportPlaying.store(true, std::memory_order_relaxed);
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

        // Process Arsenal pattern playback (MIDI buffer pop + unit render).
        // renderBlock handles PreviewToMaster; this call processes track-routed
        // Arsenal units that need MIDI buffer population for pattern playback.
        processArsenalUnits(framesThisBlock, 0, currentFrame, blockBuffer.data(), trackId);

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
    if (wasPlaying)
        setTransportPlaying(true);

    Aestra::Log::info("[AudioEngine] Bounce complete.");
    return true;
}

bool AudioEngine::initialize() {
    // Initialize the engine for headless/offline rendering
    // This is a lightweight init that doesn't start a real audio driver

    // Ensure buffers are allocated
    if (m_maxBufferFrames.load() == 0) {
        setBufferConfig(4096, 2); // Default: 4096 frames, stereo
    }

    // Reset state
    m_globalSamplePos.store(0, std::memory_order_relaxed);
    m_transportPlaying.store(false, std::memory_order_relaxed);

    // Clear command queue
    AudioQueueCommand cmd;
    while (m_commandQueue.pop(cmd)) {
        // Drain any pending commands
    }

    Aestra::Log::info("[AudioEngine] Initialized for headless rendering.");
    return true;
}

} // namespace Audio
} // namespace Aestra
