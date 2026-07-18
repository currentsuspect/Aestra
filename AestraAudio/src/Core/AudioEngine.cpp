// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"

#include "../../AestraCore/include/AestraLog.h"
#include "../../AestraCore/include/AestraMath.h"
#include "AuditionEngine.h"
#include "DSP/PanLaw.h"
#include "EffectChain.h" // [NEW]
#include "GarbageCollector.h"
#include "IO/AudioExporter.h"
#include "PathUtils.h" // [NEW] For robust path conversion
#include "PatternPlaybackEngine.h"
#include "Playback/PreviewEngine.h"
#include "Plugin/SamplerPlugin.h" // [NEW]
#include "PluginHost.h"
#include "RealtimeThreadGuard.h"
#include "UnitManager.h"
#include "miniaudio.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h> // ALLOW_PLATFORM_INCLUDE
#endif

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#if defined(__x86_64__) || (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h> // AVX/SSE for high-performance mixing
#endif
#if defined(_M_ARM64) || defined(_M_ARM64EC)
#include <intrin.h>
#endif
#include <map>
#include <unordered_map>

// Denormal protection macros
#if (defined(_M_ARM64) || defined(_M_ARM64EC)) && defined(ARM64_FPCR)
// MSVC ARM64/ARM64EC: use the toolchain-defined FPCR register selector, never a raw literal.
#define DISABLE_DENORMALS                           \
    uint64_t oldFPCR = _ReadStatusReg(ARM64_FPCR);  \
    _WriteStatusReg(ARM64_FPCR, oldFPCR | (1ULL << 24));

#define RESTORE_DENORMALS _WriteStatusReg(ARM64_FPCR, oldFPCR);
#elif defined(_M_ARM64) || defined(_M_ARM64EC)
// Older MSVC ARM64 toolsets without ARM64_FPCR: avoid unsafe raw status-register literals.
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define DISABLE_DENORMALS        \
    int oldMXCSR = _mm_getcsr(); \
    _mm_setcsr(oldMXCSR | 0x8040); // Set DAZ and FTZ flags

#define RESTORE_DENORMALS _mm_setcsr(oldMXCSR);
#elif defined(__aarch64__) && !defined(_MSC_VER)
// ARM64 on GCC/Clang-style compilers: set FPCR.FZ (bit 24) to flush denormals to zero
#define DISABLE_DENORMALS                             \
    uint64_t oldFPCR;                                 \
    __asm__ volatile("mrs %0, fpcr" : "=r"(oldFPCR)); \
    __asm__ volatile("msr fpcr, %0" ::"r"(oldFPCR | (1ULL << 24)));

#define RESTORE_DENORMALS __asm__ volatile("msr fpcr, %0" ::"r"(oldFPCR));
#else
// Other architectures: no denormal control
#define DISABLE_DENORMALS
#define RESTORE_DENORMALS
#endif

namespace Aestra {
namespace Audio {

void AudioEngine::setPreviewDuckingAttenuationDb(float attenuationDb) {
    if (!std::isfinite(attenuationDb) || attenuationDb <= 0.0f) {
        m_previewDuckingAttenuationDb.store(0.0f, std::memory_order_relaxed);
        m_previewDuckTargetGain.store(1.0f, std::memory_order_relaxed);
        return;
    }

    attenuationDb = std::clamp(attenuationDb, 0.0f, 24.0f);
    const float targetGain = std::pow(10.0f, -attenuationDb / 20.0f);
    m_previewDuckingAttenuationDb.store(attenuationDb, std::memory_order_relaxed);
    m_previewDuckTargetGain.store(targetGain, std::memory_order_relaxed);
}

float AudioEngine::getPreviewDuckingAttenuationDb() const {
    return m_previewDuckingAttenuationDb.load(std::memory_order_relaxed);
}

bool AudioEngine::isPreviewDuckingEnabled() const {
    return m_previewDuckingAttenuationDb.load(std::memory_order_relaxed) > 0.0f;
}

namespace {
std::atomic<uint64_t> g_rtMisuseCount{0};
std::atomic<uint64_t> g_rtMisuseReportedCount{0};
std::atomic<const char*> g_rtMisuseLastApi{nullptr};

void recordRealtimeMisuse(const char* apiName) noexcept {
    g_rtMisuseLastApi.store(apiName, std::memory_order_relaxed);
    g_rtMisuseCount.fetch_add(1, std::memory_order_relaxed);
}

void installRealtimeMisuseHandler() noexcept {
    static std::atomic<bool> installed{false};
    bool expected = false;
    if (installed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        setRealtimeMisuseHandler(&recordRealtimeMisuse);
    }
}

inline double clampD(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

inline double dbToLinearD(double db) {
    // UI uses -90 dB as "silence"
    if (db <= -90.0)
        return 0.0;
    return std::pow(10.0, db / 20.0);
}

inline void fastPanGainsD(double pan, double vol, double& gainL, double& gainR) {
    PanLaw::equalPower(pan, vol, gainL, gainR);
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
    const uint64_t samplesPerBeat = static_cast<uint64_t>((static_cast<double>(sampleRate) * 60.0) / bpm);
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
            // Claim a slot from the pool: a free one when possible, otherwise
            // the one closest to its note-off. A chord stamp sends one command
            // per pitch, so simultaneous triad tones each get their own slot.
            UnitAuditionState* slot = nullptr;
            for (auto& s : m_unitAuditionStates) {
                if (!s.active) {
                    slot = &s;
                    break;
                }
            }
            if (!slot) {
                slot = &m_unitAuditionStates[0];
                for (auto& s : m_unitAuditionStates) {
                    if (s.noteOffSamplesRemaining < slot->noteOffSamplesRemaining) {
                        slot = &s;
                    }
                }
            }
            slot->unitId = static_cast<UnitID>(cmd.trackIndex);
            slot->note = static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value1), 0, 127));
            slot->velocity =
                static_cast<uint8_t>(std::clamp(static_cast<int>(cmd.value2 * 127.0f), 1, 127));
            slot->noteOffSamplesRemaining =
                std::max<uint32_t>(1, m_sampleRate.load(std::memory_order_relaxed) / 8);
            slot->noteOnPending = true;
            slot->active = true;
            // Wake the master out of the post-stop Silent fast path so the
            // audition is actually rendered. After the transport stops the fade
            // settles to Silent, whose early-return drops everything before the
            // unit-audition MIDI is injected — mirrors the metronome count-in
            // recovery, and matches the pre-first-play (None) state.
            if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent) {
                m_fadeState.store(FadeState::None, std::memory_order_relaxed);
            }
            break;
        }
        // MUSE-WIRING: LoadProjectState / UpdateClipState / StartPreview / StopPreview
        // must never be handled on the RT audio thread — they involve I/O, allocation,
        // thread joins, or async decode. They are intentionally dropped here; route them
        // to the UI/main thread before reaching the audio queue. Do NOT add logging here
        // (AGENTS.md §10 forbids logging in the RT path).
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
        } else if (transportPlaying) {
            auto state = m_fadeState.load(std::memory_order_relaxed);
            if (state == FadeState::Silent || state == FadeState::FadingOut) {
                uint32_t fadeProgress = FADE_OUT_SAMPLES - m_fadeSamplesRemaining;
                m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
                m_fadeSamplesRemaining = std::min(fadeProgress, FADE_IN_SAMPLES);
            } else if (state == FadeState::None) {
                m_fadeState.store(FadeState::FadingIn, std::memory_order_relaxed);
                m_fadeSamplesRemaining = FADE_IN_SAMPLES;
            }
        }
    }
}

void AudioEngine::injectPendingUnitAudition(PatternPlaybackEngine::UnitMidiRoute* routes, size_t routeCount,
                                            uint32_t numFrames) noexcept {
    if (!routes || routeCount == 0 || numFrames == 0) {
        return;
    }

    for (auto& slot : m_unitAuditionStates) {
        if (!slot.active) {
            continue;
        }

        MidiBuffer* target = nullptr;
        for (size_t i = 0; i < routeCount; ++i) {
            if (routes[i].unitId == slot.unitId) {
                target = routes[i].midiBuffer;
                break;
            }
        }

        if (!target) {
            continue;
        }

        if (slot.noteOnPending) {
            target->addNoteOn(1, slot.note, slot.velocity, 0);
            slot.noteOnPending = false;
        }

        if (slot.noteOffSamplesRemaining <= numFrames) {
            const uint32_t noteOffOffset = std::min(numFrames - 1, slot.noteOffSamplesRemaining - 1);
            target->addNoteOff(1, slot.note, 0, noteOffOffset);
            slot.noteOffSamplesRemaining = 0;
            slot.active = false;
        } else {
            slot.noteOffSamplesRemaining -= numFrames;
        }
    }
}

void AudioEngine::drainLiveMidi(PatternPlaybackEngine::UnitMidiRoute* routes, size_t routeCount) noexcept {
    // Drains both live-input queues (UI keyboard + hardware MIDI thread) — each
    // is SPSC with this function as its sole consumer.
    //
    // Bounded work: a producer pushing concurrently can keep pop() succeeding
    // past the queue's snapshot size, so every drain loop is capped at
    // kCapacity events per queue per block. Overflow policy: events beyond the
    // cap stay queued for the next block, and once a queue is full the
    // producer's push() rejects — late live notes are dropped at the source,
    // never accumulated (for live input, late is worse than lost).
    LiveMidiQueue* queues[2] = {&m_liveMidiQueue, &m_hardwareMidiQueue};
    LiveMidiQueue::Event ev;
    if (!routes || routeCount == 0) {
        // No routable units this block: drain and drop so the queues can never
        // build a backlog of stale notes that would all fire at once later.
        for (auto* queue : queues) {
            for (uint32_t n = 0; n < LiveMidiQueue::kCapacity && queue->pop(ev); ++n) {}
        }
        return;
    }
    // MidiBuffer::addEvent itself caps at kMaxEvents. No allocation, no locks.
    for (auto* queue : queues) {
        for (uint32_t n = 0; n < LiveMidiQueue::kCapacity && queue->pop(ev); ++n) {
            for (size_t i = 0; i < routeCount; ++i) {
                if (routes[i].unitId == static_cast<UnitID>(ev.unitId) && routes[i].midiBuffer != nullptr) {
                    const uint8_t data[3] = {ev.status, ev.data1, ev.data2};
                    // Offset 0: live events sound at the start of the block they
                    // were drained in (worst-case latency = one block).
                    routes[i].midiBuffer->addEvent(0, data, 3);
                    break;
                }
            }
        }
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

void AudioEngine::setMeterSnapshots(std::shared_ptr<MeterSnapshotBuffer> snapshots) {
    if (reportRealtimeMisuse("AudioEngine::setMeterSnapshots")) {
        return;
    }

    if (m_meterSnapshotsOwned.get() == snapshots.get()) {
        return;
    }

    auto retired = std::move(m_meterSnapshotsOwned);
    m_meterSnapshotsOwned = std::move(snapshots);
    m_meterSnapshotsRaw.store(m_meterSnapshotsOwned.get(), std::memory_order_release);
    GarbageCollector::instance().release(std::move(retired), "AudioEngine::MeterSnapshotBuffer");
}

void AudioEngine::setContinuousParams(std::shared_ptr<ContinuousParamBuffer> params) {
    if (reportRealtimeMisuse("AudioEngine::setContinuousParams")) {
        return;
    }

    if (m_continuousParamsOwned.get() == params.get()) {
        return;
    }

    auto retired = std::move(m_continuousParamsOwned);
    m_continuousParamsOwned = std::move(params);
    m_continuousParamsRaw.store(m_continuousParamsOwned.get(), std::memory_order_release);
    GarbageCollector::instance().release(std::move(retired), "AudioEngine::ContinuousParamBuffer");
}

void AudioEngine::setChannelSlotMap(std::shared_ptr<const ChannelSlotMap> slotMap) {
    if (reportRealtimeMisuse("AudioEngine::setChannelSlotMap")) {
        return;
    }

    if (m_channelSlotMapOwned.get() == slotMap.get()) {
        return;
    }

    auto retired = std::move(m_channelSlotMapOwned);
    m_channelSlotMapOwned = std::move(slotMap);
    m_channelSlotMapRaw.store(m_channelSlotMapOwned.get(), std::memory_order_release);
    GarbageCollector::instance().release(std::move(retired), "AudioEngine::ChannelSlotMap");
}

void AudioEngine::refreshSamplerCache() {
    auto* unitMgr = m_unitManager.load(std::memory_order_acquire);
    if (unitMgr) {
        auto newCache = std::make_shared<SamplerCacheData>();
        refreshSamplerCacheToSnapshot(*unitMgr, *newCache);

        // Double-buffer swap: atomically publish new cache
        auto retired = std::move(m_samplerCacheOwned);
        m_samplerCacheOwned = std::move(newCache);
        m_samplerCacheRaw.store(m_samplerCacheOwned.get(), std::memory_order_release);
        m_cachedSamplerCount.store(m_samplerCacheOwned->count, std::memory_order_release);

        // Retire old cache via garbage collector
        GarbageCollector::instance().release(std::move(retired), "AudioEngine::SamplerCache");
    }
}

void AudioEngine::refreshSamplerCacheToSnapshot(UnitManager& mgr, SamplerCacheData& cache) {
    auto snapshotObj = mgr.getAudioSnapshot();
    if (snapshotObj) {
        for (const auto& unitState : snapshotObj->units) {
            if (cache.count >= cache.samplers.size()) {
                break;
            }
            auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unitState.plugin);
            if (sampler) {
                cache.owners[cache.count] = sampler;
                cache.samplers[cache.count++] = sampler.get();
            }
        }
    }
}

void AudioEngine::refreshSamplerCacheToSnapshot(UnitManager& mgr, SamplerCacheSnapshot& snapshot) {
    auto snapshotObj = mgr.getAudioSnapshot();
    if (snapshotObj) {
        for (const auto& unitState : snapshotObj->units) {
            if (snapshot.count >= snapshot.samplers.size()) {
                break;
            }
            auto sampler = std::dynamic_pointer_cast<Plugins::SamplerPlugin>(unitState.plugin);
            if (sampler) {
                snapshot.owners[snapshot.count] = sampler;
                snapshot.samplers[snapshot.count++] = sampler.get();
            }
        }
    }
}

void AudioEngine::performNonRealtimeMaintenance() {
    if (reportRealtimeMisuse("AudioEngine::performNonRealtimeMaintenance")) {
        return;
    }

    if (auto* preview = m_previewEngine.load(std::memory_order_acquire)) {
        preview->handleDeferredCompletion();
    }

    const uint64_t rtMisuseCount = g_rtMisuseCount.load(std::memory_order_relaxed);
    const uint64_t reportedCount = g_rtMisuseReportedCount.load(std::memory_order_relaxed);
    if (rtMisuseCount != reportedCount) {
        g_rtMisuseReportedCount.store(rtMisuseCount, std::memory_order_relaxed);
        const char* apiName = g_rtMisuseLastApi.load(std::memory_order_relaxed);
        Aestra::Log::warning(
            "[RTGuard] Non-real-time API reached audio thread: " + std::string(apiName ? apiName : "unknown") +
            " (count=" + std::to_string(rtMisuseCount) + ")");
    }

    // Pattern engine lookahead refill (non-RT, runs every main loop iteration)
    auto* patEng = m_patternEngine.load(std::memory_order_acquire);
    if (patEng && m_transportPlaying.load(std::memory_order_relaxed)) {
        constexpr int LOOKAHEAD_SAMPLES = 4096; // ~85ms at 48kHz — covers main loop jitter
        uint64_t currentFrame = m_globalSamplePos.load(std::memory_order_relaxed);
        uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        if (sr > 0) {
            patEng->refillWindow(currentFrame, static_cast<int>(sr), LOOKAHEAD_SAMPLES);
        }
    }

    constexpr uint64_t kCollectionIntervalNs = 500000000ull;
    const uint64_t nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());

    uint64_t lastNs = m_lastDeferredResourceCollectionNs.load(std::memory_order_relaxed);
    if (lastNs != 0 && nowNs - lastNs < kCollectionIntervalNs) {
        return;
    }

    if (!m_lastDeferredResourceCollectionNs.compare_exchange_strong(lastNs, nowNs, std::memory_order_relaxed)) {
        return;
    }

    GarbageCollector::instance().collect();
}

void AudioEngine::drainDeferredResourcesForShutdown() {
    if (reportRealtimeMisuse("AudioEngine::drainDeferredResourcesForShutdown")) {
        return;
    }

    GarbageCollector::instance().drainUntilStable(8);
}

void AudioEngine::resetCachedSamplerVoicesRt() noexcept {
    auto* cache = m_samplerCacheRaw.load(std::memory_order_acquire);
    if (!cache)
        return;
    const size_t cachedCount = cache->count;
    for (size_t i = 0; i < cachedCount && i < cache->samplers.size(); ++i) {
        if (cache->samplers[i]) {
            cache->samplers[i]->requestHardResetVoices();
        }
    }
}

void AudioEngine::syncCachedSamplerSampleRatesRt(uint32_t sampleRate) noexcept {
    if (m_lastSyncedArsenalSampleRate == sampleRate) {
        return;
    }
    auto* cache = m_samplerCacheRaw.load(std::memory_order_acquire);
    if (!cache)
        return;
    const size_t cachedCount = cache->count;
    for (size_t i = 0; i < cachedCount && i < cache->samplers.size(); ++i) {
        if (cache->samplers[i]) {
            cache->samplers[i]->setSampleRate(static_cast<double>(sampleRate));
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
    ScopedRealtimeAudioThread realtimeScope;
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
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * numOutputChannels * sizeof(float));
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
        if (m_fadeState.load(std::memory_order_relaxed) == FadeState::Silent ||
            (!wasPlaying && m_fadeState.load(std::memory_order_relaxed) != FadeState::FadingIn)) {
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
                const size_t frameBase = static_cast<size_t>(i) * numOutputChannels;
                float l = std::abs(outputBuffer[frameBase]);
                float r = numOutputChannels > 1 ? std::abs(outputBuffer[frameBase + 1]) : l;
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
        std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * numOutputChannels * sizeof(float));
        // Clear meters so UI doesn't freeze on the last loud block.
        m_peakL.store(0.0f, std::memory_order_relaxed);
        m_peakR.store(0.0f, std::memory_order_relaxed);
        m_rmsL.store(0.0f, std::memory_order_relaxed);
        m_rmsR.store(0.0f, std::memory_order_relaxed);
        auto* snaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
        if (snaps) {
            snaps->writeLevels(ChannelSlotMap::MASTER_SLOT_INDEX, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -144.0f);
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
    auto graphRead = m_state.activeGraphRead();
    const AudioGraph& graph = graphRead.get();

    // Loop & Position Logic Loop Calculation
    bool playing = isPlaying;
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
                (static_cast<double>(currentSampleRate) * 60.0) / std::max(static_cast<double>(bpm), 1.0);
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
                      m_masterBufferD.begin() + static_cast<size_t>(numFrames) * kInternalRenderChannels, 0.0);
        }
    } else {
        // Zero the double buffer
        std::fill(m_masterBufferD.begin(),
                  m_masterBufferD.begin() + static_cast<size_t>(numFrames) * kInternalRenderChannels, 0.0);
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
        const double sampleRate = static_cast<double>(currentSampleRate);
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
    // Duck transport only while preview has produced audible output. PreviewEngine
    // may exist, decode, or be selected without being audible.
    bool previewIsAudible = false;
    auto* preview = m_previewEngine.load(std::memory_order_relaxed);
    if (preview) {
        previewIsAudible = preview->isAudiblyPlaying();
    }

    constexpr double duckAttackSeconds = 0.05; // 50ms linear attack
    constexpr double duckReleaseSeconds = 0.12;
    constexpr float duckHoldSeconds = 0.10f;
    const float blockSeconds = static_cast<float>(numFrames) / static_cast<float>(std::max(currentSampleRate, 1u));
    if (previewIsAudible && isPlaying) {
        m_previewDuckHoldSecondsRemaining = duckHoldSeconds;
    } else {
        m_previewDuckHoldSecondsRemaining = std::max(0.0f, m_previewDuckHoldSecondsRemaining - blockSeconds);
    }

    const bool duckingEnabled = isPreviewDuckingEnabled();
    const bool shouldDuckForPreview = duckingEnabled && isPlaying && m_previewDuckHoldSecondsRemaining > 0.0f;
    const double targetDuckGain =
        shouldDuckForPreview ? static_cast<double>(m_previewDuckTargetGain.load(std::memory_order_relaxed)) : 1.0;
    const double fadeSeconds = targetDuckGain < static_cast<double>(m_smoothedPreviewDuckGain) ? duckAttackSeconds
                                                                                               : duckReleaseSeconds;
    const double duckFadeSamples = static_cast<double>(currentSampleRate) * fadeSeconds;
    const double duckFadeDelta = static_cast<double>(numFrames) / std::max(duckFadeSamples, 1.0);

    // Smooth the duck gain transition
    double duckGain = m_smoothedPreviewDuckGain;
    if (duckGain < targetDuckGain) {
        duckGain += duckFadeDelta;
        if (duckGain > targetDuckGain)
            duckGain = targetDuckGain;
    } else if (duckGain > targetDuckGain) {
        duckGain -= duckFadeDelta;
        if (duckGain < targetDuckGain)
            duckGain = targetDuckGain;
    }
    m_smoothedPreviewDuckGain = duckGain;

    // Publish smoothed gain for external queries
    m_previewDuckGain.store(static_cast<float>(duckGain), std::memory_order_relaxed);
    m_previewDuckSource.store(static_cast<uint8_t>((shouldDuckForPreview || duckGain < 0.995)
                                                       ? PreviewDuckSource::BrowserPreview
                                                       : PreviewDuckSource::None),
                              std::memory_order_relaxed);

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

    // Master-bus DC removal. Clear the blocker state on the off->on transition so
    // enabling never plays back a stale-state step from a previous engagement.
    const bool dcRemovalOn = m_dcRemovalEnabled.load(std::memory_order_relaxed);
    if (dcRemovalOn && !m_dcRemovalPrevOn) {
        m_dcBlockerL.reset();
        m_dcBlockerR.reset();
    }
    m_dcRemovalPrevOn = dcRemovalOn;

    // Signal integrity counters (local, then atomic update at end)
    uint32_t nanCount = 0;
    uint32_t clipCount = 0;

    // Optimized output loop - minimal branches
    for (uint32_t i = 0; i < numFrames; ++i) {
        // Read from double buffer (apply master gain and preview ducking)
        double L = src[i * 2] * gain * duckGain;
        double R = src[i * 2 + 1] * gain * duckGain;

        // Sanitize NaN/Inf BEFORE limiter (prevents state corruption)
        if (std::isnan(L) || std::isinf(L)) {
            L = 0.0;
            nanCount++;
#ifdef AESTRA_DEBUG
            assert(false && "NaN/Inf detected in left channel output");
#endif
        }
        if (std::isnan(R) || std::isinf(R)) {
            R = 0.0;
            nanCount++;
#ifdef AESTRA_DEBUG
            assert(false && "NaN/Inf detected in right channel output");
#endif
        }

        // DC removal runs before the limiter and metering so the offset is gone
        // from peak/RMS/LUFS and the limiter acts on the corrected signal.
        if (dcRemovalOn) {
            L = m_dcBlockerL.process(L);
            R = m_dcBlockerR.process(R);
        }

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

        // --- LUFS Filtering (Per-Sample) ---
        // Load active K-weight slot with acquire (published by setSampleRate with release)
        const uint32_t kwIdx = m_activeKWeightIndex.load(std::memory_order_acquire);
        const BiquadCoeff& kwPre = m_kWeightPreFilterSlots[kwIdx];
        const BiquadCoeff& kwRlb = m_kWeightRlbSlots[kwIdx];

        // Stage 1 (High Shelf)
        double f1L = m_loudnessState.f1L.process(L, kwPre);
        double f1R = m_loudnessState.f1R.process(R, kwPre);

        // Stage 2 (RLB High Pass)
        double f2L = m_loudnessState.f2L.process(f1L, kwRlb);
        double f2R = m_loudnessState.f2R.process(f1R, kwRlb);

        // Accumulate Energy
        m_loudnessState.blockEnergySum += (f2L * f2L) + (f2R * f2R);
        // -----------------------------------

        // Track output-boundary clips without mutating the limiter-disabled float path.
        if (absL >= 0.999 || absR >= 0.999) {
            clipCount++;
        }

        if (limiterOn) {
            L = std::clamp(L, -1.0, 1.0);
            R = std::clamp(R, -1.0, 1.0);
        }

        const size_t frameBase = static_cast<size_t>(i) * numOutputChannels;
        if (numOutputChannels == 1) {
            outputBuffer[frameBase] = static_cast<float>((L + R) * 0.5);
        } else {
            outputBuffer[frameBase] = static_cast<float>(L);
            outputBuffer[frameBase + 1] = static_cast<float>(R);
            for (uint32_t ch = 2; ch < numOutputChannels; ++ch) {
                outputBuffer[frameBase + ch] = 0.0f;
            }
        }

        gain += gainDelta;
    }

    // Update atomic counters (once per block, not per sample)
    if (nanCount > 0) {
        m_nanCount.fetch_add(nanCount, std::memory_order_relaxed);
    }
    if (clipCount > 0) {
        m_clipCount.fetch_add(clipCount, std::memory_order_relaxed);
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
            const size_t frameBase = static_cast<size_t>(i) * numOutputChannels;
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                outputBuffer[frameBase + ch] *= static_cast<float>(fadeGain);
            }
            --m_fadeSamplesRemaining;
        }
    } else if (m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
        const double fadeTotal = static_cast<double>(FADE_OUT_SAMPLES);
        for (uint32_t i = 0; i < numFrames; ++i) {
            if (m_fadeSamplesRemaining == 0) {
                std::memset(outputBuffer + static_cast<size_t>(i) * numOutputChannels, 0,
                            static_cast<size_t>(numFrames - i) * numOutputChannels * sizeof(float));
                m_fadeState.store(FadeState::Silent, std::memory_order_relaxed);
                break;
            }
            const double t = static_cast<double>(m_fadeSamplesRemaining) / fadeTotal;
            const double fadeGain = t * t * (3.0 - 2.0 * t); // Smoothstep

            const size_t frameBase = static_cast<size_t>(i) * numOutputChannels;
            for (uint32_t ch = 0; ch < numOutputChannels; ++ch) {
                outputBuffer[frameBase + ch] *= static_cast<float>(fadeGain);
            }
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
    if (m_transportPlaying.load(std::memory_order_relaxed) ||
        m_fadeState.load(std::memory_order_relaxed) == FadeState::FadingOut) {
        m_globalSamplePos.store(nextGlobalPos, std::memory_order_relaxed);

        // Handle Loop Metronome Reset
        if (loopSplitFrame < numFrames) {
            m_metronomeEngine.reset(nextGlobalPos, static_cast<uint32_t>(m_sampleRate.load(std::memory_order_relaxed)));
        }
    }

    // === True Peak Metering (Phase 2) ===
    // Run on the FINAL master output buffer, after fades + metronome + all
    // processing. The meter is single-writer (audio thread only); UI reads
    // the published atomics. Only enabled for stereo output (which is the
    // only case the meter has been validated for).
    if (m_truePeakMeteringEnabled.load(std::memory_order_relaxed) && numOutputChannels == 2 && numFrames > 0) {
        m_truePeakMeter.processStereo(outputBuffer, numFrames);
        m_truePeakLAtomic.store(m_truePeakMeter.getTruePeakL(), std::memory_order_relaxed);
        m_truePeakRAtomic.store(m_truePeakMeter.getTruePeakR(), std::memory_order_relaxed);
    } else {
        m_truePeakLAtomic.store(0.0f, std::memory_order_relaxed);
        m_truePeakRAtomic.store(0.0f, std::memory_order_relaxed);
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
    const uint32_t outputChannels = std::max<uint32_t>(1, numChannels);
    m_outputChannels.store(outputChannels, std::memory_order_relaxed);
    if (maxFrames > m_maxBufferFrames.load(std::memory_order_relaxed)) {
        m_maxBufferFrames.store(maxFrames, std::memory_order_relaxed);
    }
    if (m_channelPrepareConfig) {
        m_channelPrepareConfig->sampleRate.store(m_sampleRate.load(std::memory_order_relaxed),
                                                 std::memory_order_relaxed);
        m_channelPrepareConfig->maxBlockSize.store(m_maxBufferFrames.load(std::memory_order_relaxed),
                                                   std::memory_order_relaxed);
    }

    const size_t requiredSize =
        static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed)) * kInternalRenderChannels;
    const bool needAlloc = m_masterBufferD.size() < requiredSize;

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
        // Dry buffer for EffectChainSnapshot dry/wet mixing (stereo)
        size_t dryBufferSize = static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed)) * 2;
        if (m_dryBuffer.size() < dryBufferSize)
            m_dryBuffer.resize(dryBufferSize);

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

        if (m_trackBuffersD.capacity() < kMaxTracks) {
            m_trackBuffersD.reserve(kMaxTracks);
        }
        if (m_trackSidechainBuffersD.capacity() < kMaxTracks) {
            m_trackSidechainBuffersD.reserve(kMaxTracks);
        }
        if (m_trackState.capacity() < kMaxTracks) {
            // Reserve vector storage without constructing every TrackRTState.
            // TrackRTState owns large compensation buffers, so constructing all
            // kMaxTracks at startup is expensive on low-memory systems.
            m_trackState.reserve(kMaxTracks);
        }

        // Pre-allocate all RT graph scratch vectors to avoid heap allocation in renderGraph.
        // Outer vectors sized to kMaxTracks; inner vectors pre-sized to kMaxEdgesPerTrack.
        m_rtTrackIndexById.assign(kMaxTracks, kMaxTracks); // sentinel = "not present"
        m_rtAudibleDownstream.resize(kMaxTracks);
        m_rtAudibleIncoming.resize(kMaxTracks);
        m_rtSidechainIncoming.resize(kMaxTracks);
        m_rtSidechainReceiverFlags.assign(kMaxTracks, 0);
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

        // Pre-allocate prepared-routes scratch (max sends per track).
        m_preparedRoutesScratch.reserve(kMaxSendsPerTrack);

        auto graphRead = m_state.activeGraphRead();
        prepareTrackStateForGraph(graphRead.get());

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
    const size_t historyRequired =
        static_cast<size_t>(m_waveformHistoryFrames.load(std::memory_order_relaxed)) * outputChannels;
    if (m_waveformHistory.size() < historyRequired) {
        m_waveformHistory.assign(historyRequired, 0.0f);
        m_waveformWriteIndex.store(0, std::memory_order_relaxed);
    }

    // Note: SmoothedParamD uses beginRamp(samples), not a coeff field.
    // Send gain smoothers are initialized via beginRamp() at the point of use (line ~2126).

    // Critical: Buffers may have moved after resize. Re-swizzle the pointers.
    if (needAlloc) {
        compileGraph();
    }

    // Prepare insert chains at a safe point (may allocate). This prevents RT prepare() calls.
    // Snapshot publication still depends on EffectChain::prepare(), so this remains required.
    const double sampleRate = static_cast<double>(m_sampleRate.load(std::memory_order_relaxed));
    const uint32_t maxBlockSize = m_maxBufferFrames.load(std::memory_order_relaxed);
    if (auto trackMgr = m_trackManager.lock()) {
        const size_t channelCount = trackMgr->getChannelCount();
        for (size_t i = 0; i < channelCount; ++i) {
            if (auto* channel = trackMgr->getChannel(i)) {
                channel->prepareProcessingBuffers(maxBlockSize);
                channel->getEffectChain().prepare(sampleRate, maxBlockSize);
            }
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
// Sample-rate-aware LUFS K-weighting coefficients via bilinear transform
// ITU-R BS.1770-4 specification
AudioEngine::BiquadCoeff AudioEngine::computeKWeightPreFilter(double sampleRate) {
    // ITU-R BS.1770 K-weighting pre-filter analog prototype
    // Reference design parameters chosen so the 48 kHz digital coefficients match:
    // b0=1.53512485958697, b1=-2.69169618940638, b2=1.19839281085285,
    // a1=-1.69065929318241, a2=0.73248077421585
    const double fs = sampleRate;
    const double f0 = 1681.974450955533;
    const double Q = 0.7071752369554196;
    const double gainDb = 4.0; // 4.0 dB per BS.1770

    // RBJ high-shelf form using bilinear transform of the BS.1770 prototype.
    // A = 10^(gain/40) per the standard (not linear gain)
    const double K = std::tan(PI_D * f0 / fs);
    const double K2 = K * K;
    const double A = std::pow(10.0, gainDb / 40.0);
    const double norm = 1.0 + K / Q + K2;

    const double b0 = (A + K / Q + K2) / norm;
    const double b1 = 2.0 * (K2 - A) / norm;
    const double b2 = (A - K / Q + K2) / norm;
    const double a1 = 2.0 * (K2 - 1.0) / norm;
    const double a2 = (1.0 - K / Q + K2) / norm;

    // Validate that the computed 48 kHz coefficients reproduce the existing reference
    // values within tolerance; fall back to the reference constants if they do not.
    if (std::abs(fs - 48000.0) < 1.0e-9) {
        constexpr double tolerance = 1.0e-9;
        if (std::abs(b0 - kKWeightPreFilter.b0) > tolerance || std::abs(b1 - kKWeightPreFilter.b1) > tolerance ||
            std::abs(b2 - kKWeightPreFilter.b2) > tolerance || std::abs(a1 - kKWeightPreFilter.a1) > tolerance ||
            std::abs(a2 - kKWeightPreFilter.a2) > tolerance) {
            return kKWeightPreFilter;
        }
    }

    return {b0, b1, b2, a1, a2};
}

AudioEngine::BiquadCoeff AudioEngine::computeKWeightRLB(double sampleRate) {
    // BS.1770 RLB high-pass filter.
    //
    // The standard 48 kHz reference coefficients already used by this engine are:
    //   b = { 1.0, -2.0, 1.0 }
    //   a = { -1.99004745483398, 0.99007225036621 }
    //
    // To preserve that behavior across sample rates, compute the denominator from the
    // BS.1770 analog prototype parameters and keep the existing numerator convention.
    double fs = sampleRate;
    double f0 = 38.13547087602444;
    double Q = 0.5;

    // Bilinear transform pre-warping
    double K = std::tan(PI_D * f0 / fs);
    double K2 = K * K;
    double norm = 1.0 + K / Q + K2;

    double a1 = 2.0 * (K2 - 1.0) / norm;
    double a2 = (1.0 - K / Q + K2) / norm;
    AudioEngine::BiquadCoeff coeff{1.0, -2.0, 1.0, a1, a2};

    // Validate that the computed 48 kHz coefficients reproduce the existing reference
    // values within tolerance; fall back to the reference constants if they do not.
    if (std::abs(fs - 48000.0) < 1.0e-9) {
        constexpr double tolerance = 1.0e-9;
        if (std::abs(coeff.b0 - kKWeightRLB.b0) > tolerance || std::abs(coeff.b1 - kKWeightRLB.b1) > tolerance ||
            std::abs(coeff.b2 - kKWeightRLB.b2) > tolerance || std::abs(coeff.a1 - kKWeightRLB.a1) > tolerance ||
            std::abs(coeff.a2 - kKWeightRLB.a2) > tolerance) {
            return kKWeightRLB;
        }
    }

    return coeff;
}

// Fallback to 48 kHz coefficients for compatibility
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
    installRealtimeMisuseHandler();
    Aestra::Log::info("[AudioEngine] Created (Original Ctor). Ptr: " +
                      std::to_string(reinterpret_cast<uintptr_t>(this)));

    // Initialize default buffer config
    m_outputChannels.store(2);
    m_maxBufferFrames.store(4096);

    // Initialize telemetry
    m_telemetry.cycleHz.store(0);

    // Initialize sample-rate-aware LUFS coefficients at default 48 kHz
    // Both slots get the same initial values; index 0 is active
    m_kWeightPreFilterSlots[0] = m_kWeightPreFilterSlots[1] = computeKWeightPreFilter(48000.0);
    m_kWeightRlbSlots[0] = m_kWeightRlbSlots[1] = computeKWeightRLB(48000.0);
    m_activeKWeightIndex.store(0, std::memory_order_relaxed);

    m_loudnessState.integratedLufs.store(-144.0f); // Force silence init
    m_loudnessState.momentaryLufs.store(-144.0f);

    // Metronome sounds generated in MetronomeEngine constructor
    startLoudnessWorker();
}

AudioEngine::~AudioEngine() {
    if (auto trackMgr = m_trackManager.lock()) {
        trackMgr->setChannelPrepareCallback(nullptr);
        for (size_t i = 0; i < trackMgr->getChannelCount(); ++i) {
            if (auto* ch = trackMgr->getChannel(i)) {
                ch->setEffectChainLatencyCallback(nullptr);
            }
        }
    }
    stopLoudnessWorker();
    delete m_unitManagerSnapshot.load(std::memory_order_relaxed);
    m_unitManagerSnapshot.store(nullptr, std::memory_order_relaxed);
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
    constexpr uint32_t numChannels = kInternalRenderChannels;

    // Guard
    if (numFrames > m_maxBufferFrames.load(std::memory_order_relaxed)) {
        m_telemetry.incrementUnderruns();
        return;
    }

    // Debug assertion: graph must not exceed pre-allocated RT vector capacity.
    // If this fires, the RT topo/edge vectors will overflow → memory corruption.
    assert(graph.tracks.size() <= kMaxTracks && "renderGraph: track count exceeds kMaxTracks — RT vector overflow");

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

    // Solo state can still change through RT track state, while routing topology
    // is compiled into AudioGraph before publication.
    bool anySolo = graph.anySolo;
    const size_t numTracks = graph.tracks.size();
    std::fill(m_rtAudibleEligible.begin(), m_rtAudibleEligible.begin() + availableTracks, false);
    std::fill(m_rtProcessActive.begin(), m_rtProcessActive.begin() + availableTracks, false);

    for (size_t i = 0; i < graph.tracks.size(); ++i) {
        const auto& tr = graph.tracks[i];
        auto& state = ensureTrackState(tr.trackIndex);
        if (tr.solo || state.solo) {
            anySolo = true;
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
            if (static_cast<size_t>(tr.trackIndex) < availableTracks && !m_rtAudibleEligible[tr.trackIndex]) {
                m_rtAudibleEligible[tr.trackIndex] = true;
                m_rtIndexQueue.push_back(i);
            }
        }

        while (audibleQueueRead < m_rtIndexQueue.size()) {
            const size_t index = m_rtIndexQueue[audibleQueueRead++];
            m_rtSoloProcessQueue.push_back(index);
            for (const size_t destIndex : graph.audibleDownstream[index]) {
                const uint32_t destTrackIndex = graph.tracks[destIndex].trackIndex;
                if (static_cast<size_t>(destTrackIndex) >= availableTracks || m_rtAudibleEligible[destTrackIndex]) {
                    continue;
                }
                m_rtAudibleEligible[destTrackIndex] = true;
                m_rtIndexQueue.push_back(destIndex);
            }
        }

        while (processQueueRead < m_rtSoloProcessQueue.size()) {
            const size_t index = m_rtSoloProcessQueue[processQueueRead++];
            const uint32_t processTrackIndex = graph.tracks[index].trackIndex;
            if (static_cast<size_t>(processTrackIndex) < availableTracks && !m_rtProcessActive[processTrackIndex]) {
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

            enqueueUpstream(graph.audibleIncoming[index]);
            enqueueUpstream(graph.sidechainIncoming[index]);
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
                patEng->processAudio(blockStart, static_cast<int>(numFrames), unitMidiRoutes.data(),
                                     unitMidiRouteCount);
            }

            injectPendingUnitAudition(unitMidiRoutes.data(), unitMidiRouteCount, numFrames);

            // Live note input in Timeline mode. Gated on !patternPlaybackMode so
            // exactly one path drains the queue per block — Arsenal mode drains
            // inside processArsenalUnits (which self-gates on the same flag).
            if (!m_patternPlaybackMode.load(std::memory_order_relaxed)) {
                drainLiveMidi(unitMidiRoutes.data(), unitMidiRouteCount);
            }
        } else if (!m_patternPlaybackMode.load(std::memory_order_relaxed)) {
            // No routable units in Timeline mode: drain-and-drop so stale live
            // events cannot pile up and land as a burst when units appear.
            drainLiveMidi(nullptr, 0);
        }
    } else if (!m_patternPlaybackMode.load(std::memory_order_relaxed)) {
        drainLiveMidi(nullptr, 0);
    }

    // Clear all per-track buffers for this block up front so routed audio can
    // accumulate into destination tracks before they render/process.
    // Sidechain buffers: only clear for tracks that receive sidechain input this
    // block or received it last block.  Tracks that never participate in
    // sidechain routing skip the memset, saving memory bandwidth.
    for (size_t gi = 0; gi < graph.tracks.size(); ++gi) {
        const auto& track = graph.tracks[gi];
        const uint32_t trackIdx = track.trackIndex;
        if (static_cast<size_t>(trackIdx) >= availableTracks) {
            continue;
        }
        auto& buffer = m_trackBuffersD[trackIdx];
        std::memset(buffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));

        const bool receivesSidechainThisBlock = !graph.sidechainIncoming[gi].empty();
        const bool receivedSidechainLastBlock = m_rtSidechainReceiverFlags[trackIdx] != 0;
        if (receivesSidechainThisBlock || receivedSidechainLastBlock) {
            auto& sidechainBuffer = m_trackSidechainBuffersD[trackIdx];
            std::memset(sidechainBuffer.data(), 0, static_cast<size_t>(numFrames) * 2 * sizeof(double));
        }
        m_rtSidechainReceiverFlags[trackIdx] = receivesSidechainThisBlock ? 1 : 0;
    }

    // Process tracks in the precompiled topological order so routed upstream
    // content reaches destinations before inserts/fader/metering.
    m_loggedRoutingCycleWarning.store(graph.hasRoutingCycle, std::memory_order_relaxed);

    // Cache loop-invariant atomics before the per-track loop to avoid
    // redundant loads (12 loads x N tracks per audio block).
    // These values are stable for the duration of a single renderGraph() call.
    const uint32_t cachedSampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (cachedSampleRate == 0) {
        m_telemetry.incrementUnderruns();
        return;
    }
    auto* cachedSlotMap = m_channelSlotMapRaw.load(std::memory_order_acquire);
    auto* cachedParams = m_continuousParamsRaw.load(std::memory_order_acquire);
    auto* cachedSnaps = m_meterSnapshotsRaw.load(std::memory_order_relaxed);
    const bool cachedPatternMode = m_patternPlaybackMode.load(std::memory_order_relaxed);
    const auto cachedInterpQuality = m_interpQuality.load(std::memory_order_relaxed);

    for (const size_t orderedIndex : graph.topologicalOrder) {
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

        auto* slotMap = cachedSlotMap;
        if (slotMap) {
            slot = slotMap->getSlotIndex(track.trackId);
            auto* params = cachedParams;
            if (slot != ChannelSlotMap::INVALID_SLOT && params) {
                params->read(slot, faderDb, panParam, trimDb);
            }
        }

        const double faderDbClamped = clampD(static_cast<double>(faderDb), -90.0, 6.0);
        const double trimDbClamped = clampD(static_cast<double>(trimDb), -24.0, 24.0);
        const double gain = dbToLinearD(faderDbClamped) * dbToLinearD(trimDbClamped);

        double volTarget = static_cast<double>(track.volume) * gain;
        double panTarget = clampD(static_cast<double>(track.pan) + static_cast<double>(panParam), -1.0, 1.0);

        // Apply Automation Override (v3.1). Beat-domain evaluation: the curve
        // interpolates on point beats directly, so tempo changes and UI point
        // drags (which edit beats) stay musically aligned.
        if (!track.automationCurves.empty() && cachedSampleRate > 0) {
            uint64_t globalPos = m_globalSamplePos.load(std::memory_order_relaxed);
            double currentBeat = (static_cast<double>(globalPos) / cachedSampleRate) * (graph.bpm / 60.0);
            for (const auto& curve : track.automationCurves) {
                if (curve.getAutomationTarget() == AutomationTarget::Volume) {
                    volTarget = curve.getValueAtBeat(currentBeat);
                } else if (curve.getAutomationTarget() == AutomationTarget::Pan) {
                    panTarget = clampD(curve.getValueAtBeat(currentBeat), -1.0, 1.0);
                } else if (curve.getAutomationTarget() == AutomationTarget::Custom) {
                    // Plugin-parameter automation. Internal-format plugins only:
                    // their parameter storage is atomic, so a per-block
                    // setParameter from this thread is lock-free and RT-safe.
                    // Third-party formats are skipped until host param queues
                    // exist (#467). Applied before the effect chain processes
                    // this block, so the value is in effect for these frames.
                    //
                    // Smoothing policy: the *plugin* owns parameter smoothing.
                    // setParameter only stores a target; each internal plugin's
                    // process() ramps toward it — the same contract used for UI
                    // knob moves. We deliberately hand the raw target over once
                    // per block instead of ramping engine-side, so automation
                    // and manual edits share one smoother and never cascade into
                    // double-smoothing. Every internal effect honors this (Drift
                    // was the last to gain per-sample Mix/Pitch smoothing).
                    if (track.effectChainSnapshot && curve.effectSlot < EffectChainSnapshot::MAX_SLOTS) {
                        const auto& slot = track.effectChainSnapshot->slot(curve.effectSlot);
                        if (slot.plugin && slot.plugin->getInfo().format == PluginFormat::Internal) {
                            const float value = curve.getValueAtBeat(currentBeat);
                            if (std::isfinite(value)) {
                                slot.plugin->setParameter(curve.paramId, value);
                            }
                        }
                    }
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

        // Initialize send gain smoothers when send count changes.
        // Vectors are pre-sized to kMaxSendsPerTrack in setBufferConfig(),
        // so we never resize here — only update the active entries.
        const size_t sendCount = std::min(track.sends.size(), static_cast<size_t>(kMaxSendsPerTrack));
        if (state.lastActiveSendCount != sendCount) {
            state.lastActiveSendCount = sendCount;
            for (size_t sendIndex = 0; sendIndex < sendCount; ++sendIndex) {
                double targetL = 0.0;
                double targetR = 0.0;
                fastPanGainsD(clampD(static_cast<double>(track.sends[sendIndex].pan), -1.0, 1.0),
                              static_cast<double>(track.sends[sendIndex].gain), targetL, targetR);
                state.sendGainL[sendIndex].current = targetL;
                state.sendGainL[sendIndex].target = targetL;
                state.sendGainR[sendIndex].current = targetR;
                state.sendGainR[sendIndex].target = targetR;
            }
        }

        if (!muted) {
            for (size_t sendIndex = 0; sendIndex < sendCount; ++sendIndex) {
                if (track.sends[sendIndex].mute) {
                    continue;
                }
                double targetL = 0.0;
                double targetR = 0.0;
                fastPanGainsD(clampD(static_cast<double>(track.sends[sendIndex].pan), -1.0, 1.0),
                              static_cast<double>(track.sends[sendIndex].gain), targetL, targetR);
                state.sendGainL[sendIndex].setTarget(targetL);
                state.sendGainR[sendIndex].setTarget(targetR);
                state.sendGainL[sendIndex].beginRamp(numFrames);
                state.sendGainR[sendIndex].beginRamp(numFrames);
            }
        }

        if (!processActive) {
            auto* snaps = cachedSnaps;
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
        bool patternMode = cachedPatternMode;

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
                const double outputRate = static_cast<double>(cachedSampleRate);
                const double srcRate = clip.sourceSampleRate > 0.0 ? clip.sourceSampleRate : outputRate;
                const double ratio = srcRate / outputRate;

                // Source position
                const double outputFrameOffset = static_cast<double>(start - clip.startSample);
                double phase = clip.sampleOffset + outputFrameOffset * ratio;

                // Bounds
                const int64_t totalFrames = static_cast<int64_t>(clip.totalFrames);
                const uint64_t totalFrameCount = static_cast<uint64_t>(totalFrames);
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
                        switch (cachedInterpQuality) {
                        case Interpolators::InterpolationQuality::Cubic:
                            for (uint32_t i = 0; i < framesToRender && phase < phaseEnd; ++i) {
                                double fade = 1.0;
                                const uint64_t projectSample = start + i;
                                if (fadeLen > 0) {
                                    if (projectSample < clip.startSample + fadeLen)
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) /
                                                                  static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) /
                                                                  static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                float sample = 0.0f;
                                uint64_t idx = static_cast<uint64_t>(phase);
                                double frac = phase - static_cast<double>(idx);
                                // Catmull-Rom 4-point on mono data
                                float s0 = (idx > 0) ? data[idx - 1] : data[idx];
                                float s1 = data[idx];
                                float s2 = (idx + 1 < totalFrameCount) ? data[idx + 1] : data[idx];
                                float s3 = (idx + 2 < totalFrameCount) ? data[idx + 2] : s2;
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
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) /
                                                                  static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) /
                                                                  static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                double val =
                                    Interpolators::sincInterpolateMono(data, totalFrames, phase, cachedInterpQuality);
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
                                        fade = std::min(fade, static_cast<double>(projectSample - clip.startSample) /
                                                                  static_cast<double>(fadeLen));
                                    if (projectSample + fadeLen > clip.endSample)
                                        fade = std::min(fade, static_cast<double>(clip.endSample - projectSample) /
                                                                  static_cast<double>(fadeLen));
                                }
                                double clipGain = static_cast<double>(clip.gain);
                                uint64_t idx = static_cast<uint64_t>(phase);
                                double frac = phase - static_cast<double>(idx);
                                float s0 = data[idx];
                                float s1 = (idx + 1 < static_cast<uint64_t>(totalFrames)) ? data[idx + 1] : s0;
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
                        if (unitMidiRoutes[r].unitId == static_cast<UnitID>(unit.id)) {
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
                    const double unitGain = static_cast<double>(unit.gain);
                    double* dDst = buffer.data();
                    for (uint32_t k = 0; k < numFrames; ++k) {
                        dDst[k * 2] += static_cast<double>(outputs[0][k]) * unitGain;
                        dDst[k * 2 + 1] += static_cast<double>(outputs[1][k]) * unitGain;
                    }
                }
            }
        }

        // === Plugin Processing (EffectChain Snapshot) ===
        float trackSidechainPeak = 0.0f;
        if (track.effectChainSnapshot && track.effectChainSnapshot->getActiveSlotCount() > 0) {
            // Check if scratches are large enough (should be from setBufferConfig)
            if (m_scratchL.size() >= numFrames && m_scratchR.size() >= numFrames &&
                m_sidechainScratchL.size() >= numFrames && m_sidechainScratchR.size() >= numFrames &&
                m_dryBuffer.size() >= static_cast<size_t>(numFrames) * 2) {
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
                    trackSidechainPeak = std::max(trackSidechainPeak, std::max(std::abs(scL[k]), std::abs(scR[k])));
                }

                // 2. Process using snapshot (RT-safe)
                float* channels[2] = {fL, fR};
                const float* sidechainChannels[2] = {scL, scR};
                track.effectChainSnapshot->process(channels, 2, numFrames, sidechainChannels, 2, m_dryBuffer.data());

                // 3. Re-interleave Float -> Double
                double* dOut = buffer.data();
                for (uint32_t k = 0; k < numFrames; ++k) {
                    dOut[k * 2] = static_cast<double>(fL[k]);
                    dOut[k * 2 + 1] = static_cast<double>(fR[k]);
                }
            }
        }

        // === Apply Plugin Delay Compensation ===
        // Compensation must apply to dry tracks too; those are often the tracks
        // delayed to align with a parallel latent path elsewhere in the graph.
        if (state.compensationEnabled && state.compensationDelaySamples > 0) {
            const uint32_t delay = state.compensationDelaySamples;
            const uint32_t capacity = static_cast<uint32_t>(state.compensationBuffer.size() / 2);
            if (delay < capacity) {
                double* dOut = buffer.data();
                for (uint32_t k = 0; k < numFrames; ++k) {
                    const uint32_t writePos = state.compensationWritePos;
                    const uint32_t readPos = (writePos + capacity - delay) % capacity;

                    state.compensationBuffer[writePos * 2] = static_cast<float>(dOut[k * 2]);
                    state.compensationBuffer[writePos * 2 + 1] = static_cast<float>(dOut[k * 2 + 1]);

                    dOut[k * 2] = static_cast<double>(state.compensationBuffer[readPos * 2]);
                    dOut[k * 2 + 1] = static_cast<double>(state.compensationBuffer[readPos * 2 + 1]);

                    state.compensationWritePos = (writePos + 1) % capacity;
                }
            }
        }

        // Route post-fader output to the selected main destination and any audible sends.
        double tL, tR;
        fastPanGainsD(panTarget, volTarget, tL, tR);
        state.gainL.setTarget(tL);
        state.gainR.setTarget(tR);
        state.gainL.beginRamp(numFrames);
        state.gainR.beginRamp(numFrames);

        const double* trackData = buffer.data();
        double peakTrackL = 0.0;
        double peakTrackR = 0.0;
        double rmsAccTrackL = 0.0;
        double rmsAccTrackR = 0.0;
        double lowAccTrackL = 0.0;
        double lowAccTrackR = 0.0;
        double sumLRTrack = 0.0; // Correlation accumulator

        auto* snaps = cachedSnaps;
        const bool publishTrackSnapshot = (snaps && slot != ChannelSlotMap::INVALID_SLOT);
        if (publishTrackSnapshot) {
            snaps->writeSidechainPeak(slot, trackSidechainPeak);
        }
        double* lfStateL = publishTrackSnapshot ? &m_meterLfStateL[slot] : nullptr;
        double* lfStateR = publishTrackSnapshot ? &m_meterLfStateR[slot] : nullptr;

        const bool routesMainToMaster = track.mainOutputId == 0xFFFFFFFFu;
        double* mainDestBuffer = nullptr;
        if (!routesMainToMaster && audibleEligible && slotMap) {
            const uint32_t destSlot = slotMap->getSlotIndex(track.mainOutputId);
            if (destSlot != ChannelSlotMap::INVALID_SLOT && destSlot < availableTracks && destSlot != trackIdx &&
                (!anySolo || m_rtAudibleEligible[destSlot])) {
                mainDestBuffer = m_trackBuffersD[destSlot].data();
            }
        }

        bool hasPreFaderSend = std::any_of(track.sends.begin(), track.sends.end(),
                                           [](const auto& send) { return !send.mute && !send.postFader; });
        const size_t preFaderSize = static_cast<size_t>(numFrames) * 2u;
        // Guard: only resize if capacity is already reserved by setBufferConfig().
        // If the audio thread delivers a larger block than we reserved for,
        // skip pre-fader sends rather than allocate on the RT thread.
        if (hasPreFaderSend && state.preFaderBuffer.capacity() >= preFaderSize) {
            state.preFaderBuffer.resize(preFaderSize);
            std::memcpy(state.preFaderBuffer.data(), trackData, preFaderSize * sizeof(double));
        } else if (hasPreFaderSend) {
            // Capacity insufficient — pre-fader sends will be silenced for this block.
            // This should never happen in normal operation (setBufferConfig reserves
            // to maxBufferFrames). Flag for diagnostics.
            hasPreFaderSend = false;
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
                if (audibleEligible && routesMainToMaster) {
                    masterBuf[i * 2] += outL;
                    masterBuf[i * 2 + 1] += outR;
                } else if (mainDestBuffer) {
                    mainDestBuffer[i * 2] += outL;
                    mainDestBuffer[i * 2 + 1] += outR;
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
            const size_t sendCount = std::min(track.sends.size(), static_cast<size_t>(kMaxSendsPerTrack));
            for (size_t sendIndex = 0; sendIndex < sendCount; ++sendIndex) {
                const auto& send = track.sends[sendIndex];
                if (send.mute) {
                    continue;
                }
                if (!send.postFader && !hasPreFaderSend) {
                    continue;
                }

                PreparedSendRoute route;
                route.source = send.postFader ? buffer.data() : state.preFaderBuffer.data();
                route.gainL = &state.sendGainL[sendIndex];
                route.gainR = &state.sendGainR[sendIndex];
                // PDC v2 (P4b.3): look up the per-edge compensation slot for
                // this send. nullptr means "no compensation"; treated as a fast
                // path in the consume loop below.
                route.edgeDelay =
                    (sendIndex < state.sendEdgeDelays.size()) ? state.sendEdgeDelays[sendIndex].get() : nullptr;

                if (send.sidechainOnly && send.targetChannelId != 0xFFFFFFFFu && slotMap) {
                    const uint32_t scDestSlot = slotMap->getSlotIndex(send.targetChannelId);
                    if (scDestSlot != ChannelSlotMap::INVALID_SLOT && scDestSlot < availableTracks &&
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
                if (sendDestSlot != ChannelSlotMap::INVALID_SLOT && sendDestSlot < availableTracks &&
                    sendDestSlot != trackIdx && (!anySolo || m_rtAudibleEligible[sendDestSlot])) {
                    route.dest = m_trackBuffersD[sendDestSlot].data();
                    m_preparedRoutesScratch.push_back(route);
                }
            }

            for (const auto& route : m_preparedRoutesScratch) {
                // Fast path: no per-edge compensation. v1 behavior.
                EdgeDelayState* const edge = route.edgeDelay;
                const uint32_t edgeComp = edge ? edge->compensationSamples.load(std::memory_order_acquire) : 0u;
                if (!edge || edgeComp == 0u) {
                    for (uint32_t i = 0; i < numFrames; ++i) {
                        const double sendGainL = route.gainL->next();
                        const double sendGainR = route.gainR->next();
                        route.dest[i * 2] += route.source[i * 2] * sendGainL;
                        route.dest[i * 2 + 1] += route.source[i * 2 + 1] * sendGainR;
                    }
                    continue;
                }

                // PDC v2 (P4b.3): per-edge ring-buffer delay path.
                // Acquire the buffer pointer + mask in one block-stable snapshot.
                float* const buf = edge->bufferPtr.load(std::memory_order_acquire);
                const uint32_t mask = edge->capacityMask.load(std::memory_order_acquire);
                if (!buf || mask == 0u) {
                    // Buffer not yet committed; fall back to direct mix this block.
                    for (uint32_t i = 0; i < numFrames; ++i) {
                        const double sendGainL = route.gainL->next();
                        const double sendGainR = route.gainR->next();
                        route.dest[i * 2] += route.source[i * 2] * sendGainL;
                        route.dest[i * 2 + 1] += route.source[i * 2 + 1] * sendGainR;
                    }
                    continue;
                }
                const uint32_t capacityFrames = mask + 1u;
                if (edgeComp >= capacityFrames) {
                    // Defensive: requested delay exceeds buffer (should not
                    // happen — off-RT sizes buffer to delay + headroom). Fall
                    // back to direct mix rather than read garbage.
                    for (uint32_t i = 0; i < numFrames; ++i) {
                        const double sendGainL = route.gainL->next();
                        const double sendGainR = route.gainR->next();
                        route.dest[i * 2] += route.source[i * 2] * sendGainL;
                        route.dest[i * 2 + 1] += route.source[i * 2 + 1] * sendGainR;
                    }
                    continue;
                }
                uint32_t writePos = edge->writePos.load(std::memory_order_relaxed);
                for (uint32_t i = 0; i < numFrames; ++i) {
                    const double sendGainL = route.gainL->next();
                    const double sendGainR = route.gainR->next();
                    const uint32_t w = writePos & mask;
                    buf[w * 2] = static_cast<float>(route.source[i * 2]);
                    buf[w * 2 + 1] = static_cast<float>(route.source[i * 2 + 1]);
                    const uint32_t r = (writePos + capacityFrames - edgeComp) & mask;
                    const double delayedL = static_cast<double>(buf[r * 2]);
                    const double delayedR = static_cast<double>(buf[r * 2 + 1]);
                    route.dest[i * 2] += delayedL * sendGainL;
                    route.dest[i * 2 + 1] += delayedR * sendGainR;
                    ++writePos;
                }
                edge->writePos.store(writePos, std::memory_order_release);
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
    }

    if (unitSnapshot) {
        for (const auto& unit : unitSnapshot->units) {
            if (unit.routeId >= 0 || !unit.enabled || !unit.plugin) {
                continue;
            }

            MidiBuffer* midiBuf = nullptr;
            for (size_t r = 0; r < unitMidiRouteCount; ++r) {
                if (unitMidiRoutes[r].unitId == static_cast<UnitID>(unit.id)) {
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

void AudioEngine::prepareTrackStateForGraph(const AudioGraph& graph) {
    size_t requiredTrackSlots = 0;
    for (const auto& track : graph.tracks) {
        requiredTrackSlots = std::max(requiredTrackSlots, static_cast<size_t>(track.trackIndex) + 1);
    }
    if (requiredTrackSlots == 0) {
        return;
    }

    if (m_trackState.capacity() < requiredTrackSlots) {
        m_trackState.reserve(std::min(kMaxTracks, std::max(requiredTrackSlots, m_trackState.capacity() * 2)));
    }
    if (m_trackState.size() < requiredTrackSlots) {
        m_trackState.resize(requiredTrackSlots);
    }
    if (m_trackBuffersD.size() < requiredTrackSlots) {
        m_trackBuffersD.resize(requiredTrackSlots);
    }
    if (m_trackSidechainBuffersD.size() < requiredTrackSlots) {
        m_trackSidechainBuffersD.resize(requiredTrackSlots);
    }

    const size_t preFaderSize =
        static_cast<size_t>(m_maxBufferFrames.load(std::memory_order_relaxed)) * kInternalRenderChannels;
    const size_t renderBufferSize = preFaderSize;
    for (const auto& track : graph.tracks) {
        if (track.trackIndex >= m_trackState.size()) {
            continue;
        }
        if (m_trackBuffersD[track.trackIndex].size() < renderBufferSize) {
            m_trackBuffersD[track.trackIndex].assign(renderBufferSize, 0.0);
        }
        if (m_trackSidechainBuffersD[track.trackIndex].size() < renderBufferSize) {
            m_trackSidechainBuffersD[track.trackIndex].assign(renderBufferSize, 0.0);
        }

        auto& state = m_trackState[track.trackIndex];
        const size_t sendCount = std::min(track.sends.size(), kMaxSendsPerTrack);
        if (state.sendGainL.size() < sendCount) {
            state.sendGainL.resize(sendCount);
        }
        if (state.sendGainR.size() < sendCount) {
            state.sendGainR.resize(sendCount);
        }
        if (state.preFaderBuffer.capacity() < preFaderSize) {
            state.preFaderBuffer.reserve(preFaderSize);
        }
    }
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
    const int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_relaxed);
    const int inactiveIdx = 1 - activeIdx;
    auto& targetState = m_graphStates[inactiveIdx];
    auto& targetOrder = targetState.renderTracks;
    targetOrder.clear();

    auto* slotMap = m_channelSlotMapRaw.load(std::memory_order_relaxed);
    if (!slotMap)
        return;

    targetOrder.reserve(slotMap->getChannelCount());
    uint32_t maxTrackIndexPlusOne = 0;

    // Access the current graph snapshot
    auto graphRead = m_state.activeGraphRead();
    const auto& graph = graphRead.get(); // Fixed method name

    // Iterate Tracks directly from the graph snapshot
    for (const auto& tr : graph.tracks) {
        const uint32_t idx = tr.trackIndex;

        // Safety Check
        if (idx >= m_trackBuffersD.size())
            continue;
        maxTrackIndexPlusOne = std::max(maxTrackIndexPlusOne, idx + 1);

        RenderTrack rt;
        rt.trackIndex = idx;
        rt.selfBuffer = m_trackBuffersD[idx].data();

        // Ensure trackStates has an entry for this track
        if (idx >= targetState.trackStates.size()) {
            targetState.trackStates.resize(idx + 1);
        }

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
            fastPanGainsD(clampD(static_cast<double>(send.pan), -1.0, 1.0), static_cast<double>(send.gain), sendGainL,
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
    targetState.trackStates.resize(maxTrackIndexPlusOne);

    // Atomic Swap
    m_activeRenderTrackIndex.store(inactiveIdx, std::memory_order_release);
}

void AudioEngine::panic() {
    // 1. Force Silence (stops renderGraph calls and mutes output immediately)
    m_fadeState = FadeState::Silent;

    // 2. Reset all plugins (Main Thread)
    // We lock the graph mutex to ensure we don't access a graph that's being swapped
    std::lock_guard<std::mutex> lock(m_graphMutex);

    auto graphRead = m_state.activeGraphRead();
    const AudioGraph& graph = graphRead.get();

    // Reset effect chains via the original MixerChannels (not the snapshots)
    // We use the trackManager to access the actual channels for panic()
    auto trackMgr = m_trackManager.lock();
    if (trackMgr) {
        for (size_t i = 0; i < graph.tracks.size() && i < trackMgr->getChannelCount(); ++i) {
            auto* channel = trackMgr->getChannel(i);
            if (channel) {
                channel->resetEffectChain();
            }
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

    if (!patternEngine || !unitManager) {
        // Keep the live-queue safety net effective: drop stale live events so
        // they cannot pile up and land as a burst when units appear later.
        // Only the realtime block (targetBuffer == nullptr) may touch the
        // SPSC queue — the offline bounce path runs on another thread.
        if (targetBuffer == nullptr)
            drainLiveMidi(nullptr, 0);
        return;
    }

    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    if (sampleRate == 0) {
        if (targetBuffer == nullptr)
            drainLiveMidi(nullptr, 0);
        return;
    }

    // Sync logic moved after snapshot retrieval

    // Continue rendering even when transport is stopped (one-shot/tails behavior).
    // But do not schedule new MIDI when stopped.
    const bool transportPlaying = m_transportPlaying.load(std::memory_order_relaxed);

    const uint64_t currentFrame = startFrame;

    // Get Arsenal snapshot for RT-safe unit iteration
    auto snapshot = unitManager->getAudioSnapshot();
    if (!snapshot || snapshot->units.empty()) {
        // No routable units: drain-and-drop live events (see note above).
        if (targetBuffer == nullptr)
            drainLiveMidi(nullptr, 0);
        return;
    }

    syncCachedSamplerSampleRatesRt(sampleRate);

    const size_t requiredStereoSamples = static_cast<size_t>(numFrames) * 2;
    // Buffers must be pre-sized in setBufferConfig()
    if (m_unitBufferD.size() < requiredStereoSamples || m_pluginBufferF.size() < requiredStereoSamples ||
        m_silentBufferF.size() < numFrames) {
        if (targetBuffer == nullptr)
            drainLiveMidi(nullptr, 0);
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

    // Process pattern MIDI events while playing, or during offline bounce
    if (transportPlaying || targetBuffer != nullptr) {
        patternEngine->processAudio(currentFrame, static_cast<int>(numFrames), unitMidiRoutes.data(),
                                    unitMidiRouteCount);
    }

    injectPendingUnitAudition(unitMidiRoutes.data(), unitMidiRouteCount, numFrames);

    // Live note input in Arsenal mode (this function early-returns unless
    // patternPlaybackMode, so this is the only drain per block here). Runs with
    // or without the transport — playing an instrument must not require play.
    // Offline isolated-track bounce (targetBuffer set) runs on a non-RT thread
    // while the realtime callback may still be live: it must never touch the
    // single-consumer queue, which also keeps live notes out of bounced files.
    if (targetBuffer == nullptr) {
        drainLiveMidi(unitMidiRoutes.data(), unitMidiRouteCount);
    }

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
        // to the isolated track.
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
            if (unitMidiRoutes[r].unitId == static_cast<UnitID>(unit.id)) {
                midiIn = unitMidiRoutes[r].midiBuffer;
                break;
            }
        }
        MidiBuffer midiOut; // Unused

        unit.plugin->process(inputs, outputs, 2, 2, numFrames, midiIn, &midiOut);

        // Mix plugin output into master buffer (mixing floats into double master)
        double* masterD =
            (targetBuffer ? targetBuffer : m_masterBufferD.data()) + static_cast<size_t>(bufferOffset) * 2;
        const double unitGain = static_cast<double>(unit.gain);
        for (uint32_t i = 0; i < numFrames; ++i) {
            masterD[i * 2 + 0] += static_cast<double>(outputs[0][i]) * unitGain; // Left
            masterD[i * 2 + 1] += static_cast<double>(outputs[1][i]) * unitGain; // Right
        }

        bufIdx++;
    }
}

// =================================================================================================
// Offline Bounce / Export
// =================================================================================================

bool AudioEngine::bounceRangeToWav(double startBeat, double endBeat, const std::string& outputPath, int32_t trackId) {
    if (!std::isfinite(startBeat) || !std::isfinite(endBeat) || endBeat <= startBeat)
        return false;
    m_lastBounceWroteAnyFramesForTests.store(false, std::memory_order_relaxed);

    // Delegate to AudioExporter for master bounce (trackId == -1) to use authoritative offline path
    // with master-stage processing and dithering. Isolated track bounce (trackId >= 0) uses the
    // existing renderBlock path for now until AudioExporter gains isolated track support.
    // Skip delegation when test hook is active — the test hook only exists in the old path.
    const bool forceWriteErrorForTests = m_forceBounceWriteErrorForTests.load(std::memory_order_relaxed);
    if (trackId < 0 && !forceWriteErrorForTests) {
        auto trackMgr = m_trackManager.lock();
        if (!trackMgr) {
            Aestra::Log::error("[AudioEngine] bounceRangeToWav: trackManager not available");
            return false;
        }
        auto result = Audio::AudioExporter::bounceToWav(*this, *trackMgr, startBeat, endBeat, outputPath, -1);
        if (!result.success) {
            Aestra::Log::error("[AudioEngine] bounceRangeToWav failed: " + result.errorMessage);
            return false;
        }
        return true;
    }

    // Isolated track bounce: use existing renderBlock path (TODO: consolidate when AudioExporter adds isolated track
    // support)
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
            setTransportPlaying(true);
        return false;
    }

    // 3. Render Loop
    const uint32_t blockSize = 4096;
    std::vector<double> blockBuffer(blockSize * 2); // Stereo
    std::vector<float> floatBuffer(blockSize * 2);  // For writing

    bool writeError = false;
    uint64_t currentFrame = startSample;
    uint64_t framesRemaining = totalFrames;
    bool forcedWriteErrorTriggered = false;
    bool wroteAnyFrames = false;

    // Playback stopped at start of function
    m_transportPlaying.store(false, std::memory_order_relaxed); // Ensure redundant enforce

    if (wasPlaying) {
        setTransportPlaying(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Allow RT to spin down
    }

    // Compile graph to populate renderTracks before we can search it.
    // This is needed for offline bounce since compileGraph() runs asynchronously during live playback.
    // Must call before locking m_graphMutex to avoid deadlock.
    compileGraph();

    // Lock graph for stability during bounce
    std::lock_guard<std::mutex> lock(m_graphMutex);

    int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_relaxed);
    AudioGraphState& graphState = m_graphStates[activeIdx]; // Use active, but modify copies in renderBlock if needed?
    // Note: renderBlock applies smoothing to the passed state.
    // For off-line bounce, we should PROBABLY copy the state to avoid jumping parameters on the live graph?
    // However, for simplicity and since we are stopped, we use the active graph.
    // Ideally we clone it.

    // trackId parameter is the persistent track lane index.
    // isolatedTrackIndex is used to compare against unit.routeId (which is also the lane index).
    // Use trackId directly when >= 0 for the comparison.
    int32_t isolatedTrackIndex = trackId;

    Aestra::Log::info("[AudioEngine] Starting isolated track bounce: " + std::to_string(totalFrames) + " frames.");

    while (framesRemaining > 0) {
        uint32_t framesThisBlock = (uint32_t)std::min((uint64_t)blockSize, framesRemaining);

        // Zero buffers
        std::fill(blockBuffer.begin(), blockBuffer.end(), 0.0);
        std::fill(m_masterBufferD.begin(), m_masterBufferD.begin() + static_cast<size_t>(framesThisBlock) * 2, 0.0);

        // Setup Context
        AudioRenderer::Context ctx;
        ctx.masterBuffer = blockBuffer.data();
        ctx.numFrames = framesThisBlock;
        ctx.bufferOffset = 0;
        ctx.globalPos = currentFrame;
        ctx.sampleRate = (uint32_t)sampleRate;
        auto graphRead = m_state.activeGraphRead();
        ctx.graph = &graphRead.get();
        ctx.isOffline = true;
        ctx.isolatedTrackIndex = isolatedTrackIndex;

        // Refill pattern engine lookahead (normally done by performNonRealtimeMaintenance)
        auto* patEng = m_patternEngine.load(std::memory_order_acquire);
        if (patEng) {
            patEng->refillWindow(currentFrame, static_cast<int>(sampleRate), static_cast<int>(blockSize));
        }

        // Render
        m_rtRenderer.renderBlock(ctx, graphState, *this);

        // renderBlock routes tracks to m_masterBufferD via activeConnections.
        // Copy the result to blockBuffer before processing Arsenal units.
        for (size_t i = 0; i < static_cast<size_t>(framesThisBlock) * 2; ++i) {
            blockBuffer[i] = m_masterBufferD[i];
        }

        // Process Arsenal pattern playback (MIDI buffer pop + unit render).
        // renderBlock handles PreviewToMaster; this call processes track-routed
        // Arsenal units that need MIDI buffer population for pattern playback.
        processArsenalUnits(framesThisBlock, 0, currentFrame, blockBuffer.data(), isolatedTrackIndex);

        // Buffer Conversion (Double -> Float)
        for (size_t i = 0; i < framesThisBlock * 2; ++i) {
            floatBuffer[i] = static_cast<float>(blockBuffer[i]);
        }

        // Write
        ma_uint64 framesWritten = 0;
        ma_result result = MA_ERROR;
        if (forceWriteErrorForTests && wroteAnyFrames && !forcedWriteErrorTriggered) {
            // Test hook: deterministically fail after one successful write block.
            forcedWriteErrorTriggered = true;
            result = MA_ERROR;
            framesWritten = 0;
        } else {
            result = ma_encoder_write_pcm_frames(&encoder, floatBuffer.data(), framesThisBlock, &framesWritten);
            if (result == MA_SUCCESS && framesWritten == framesThisBlock) {
                wroteAnyFrames = true;
            }
        }
        if (result != MA_SUCCESS || framesWritten != framesThisBlock) {
            Aestra::Log::error("[AudioEngine] Write error during bounce: result=" + std::to_string(result) +
                               ", written=" + std::to_string(framesWritten));
            writeError = true;
            break;
        }

        currentFrame += framesThisBlock;
        framesRemaining -= framesThisBlock;
    }

    ma_encoder_uninit(&encoder);
    m_lastBounceWroteAnyFramesForTests.store(wroteAnyFrames, std::memory_order_relaxed);

    // Restore playback state
    if (wasPlaying)
        setTransportPlaying(true);

    if (writeError) {
        // Clean up partial file on write error
#ifdef _WIN32
        std::wstring widePath = pathStringToWide(outputPath);
        const int removeResult = _wremove(widePath.c_str());
#else
        const int removeResult = std::remove(outputPath.c_str());
#endif
        const int removeErrno = errno;
        if (removeResult == 0) {
            Aestra::Log::error("[AudioEngine] Bounce failed — partial file removed: " + outputPath);
        } else {
            Aestra::Log::error("[AudioEngine] Bounce failed — could not remove partial file: " + outputPath +
                               " (errno=" + std::to_string(removeErrno) + ")");
        }
        return false;
    }

    Aestra::Log::info("[AudioEngine] Isolated track bounce complete: " + outputPath);
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

// ============================================================================
// Plugin Delay Compensation (PDC)
// ============================================================================

void AudioEngine::calculateLatencyCompensation() {
    // PDC v2 (P2): pipeline is
    //   build LatencyGraph (off-RT) -> solveLatency() -> apply to RT state.
    // The solver is a pure function (see AestraAudio/src/Core/LatencyTopology.cpp).
    // P2 produces a flat solution exactly matching v1; graph-aware DFS lands in P4.

    if (!m_latencyCompensationEnabled) {
        m_maxProjectLatency = 0;
        m_latencyDirty = false;
        // Publish a zero topology via the double-buffer flip so off-RT readers
        // observe a consistent disabled state.
        {
            const int prevIdx = m_activeSolvedTopologyIndex.load(std::memory_order_relaxed);
            const int inactiveIdx = 1 - prevIdx;
            SolvedLatencyTopology empty;
            empty.generation = ++m_latencyGraphGeneration;
            m_solvedTopologies[inactiveIdx] = std::move(empty);
            m_activeSolvedTopologyIndex.store(inactiveIdx, std::memory_order_release);
        }
        return;
    }

    auto trackManager = m_trackManager.lock();
    if (!trackManager) {
        m_latencyDirty = false;
        return;
    }

    const size_t trackCount = trackManager->getChannelCount();

    // 1. Build LatencyGraph from current routing state.
    //
    // P4b.1: edges populated from MixerChannel::getSends() and mainOutputId.
    // A synthetic master node is appended so sends/routes to master are
    // represented in the graph. Master intrinsic latency is 0 for P4b.1; P9
    // (G6) will populate it with real master-FX latency.
    //
    // Note: edges are computed off-RT and consumed only by the off-RT solver
    // for now (P4b.1). The RT path still applies only per-node
    // `outputCompensationSamples` to TrackRTState; P4b.2 wires per-edge
    // compensation into processBlock.
    constexpr uint32_t kMasterSentinelId = 0xFFFFFFFFu;

    LatencyGraph graph;
    graph.generation = ++m_latencyGraphGeneration;
    graph.nodes.reserve(trackCount + 1);
    // Parallel: nodeIndex -> trackIndex, so apply pass can map back without a search.
    std::vector<size_t> nodeTrackIdx;
    nodeTrackIdx.reserve(trackCount + 1);
    // Map: channelId -> node index, for translating send targets to graph edges.
    std::unordered_map<uint32_t, size_t> channelIdToNodeIdx;
    channelIdToNodeIdx.reserve(trackCount + 1);

    // Track-local edge bookkeeping for the P4b.2 apply pass: which entries in
    // graph.edges correspond to each track's mainOutputId edge and each of its
    // sends. Sends are tracked by their slot index so they map back to
    // TrackRTState::sendEdgeDelays without re-walking getSends().
    struct TrackEdgeIndices {
        size_t mainOutEdgeIdx = static_cast<size_t>(-1);
        std::vector<std::pair<size_t, size_t>> sendEdgeBySlot; // {sendSlot, edgeIdx}
    };
    std::vector<TrackEdgeIndices> trackEdgeIndices(trackCount);

    for (size_t i = 0; i < trackCount; ++i) {
        auto* channel = trackManager->getChannel(i);
        if (!channel)
            continue;
        LatencyGraph::Node node;
        node.channelId = channel->getChannelId();
        node.intrinsicLatency = channel->getEffectChain().getTotalLatency();
        node.muted = channel->isMuted();
        node.domain = LatencyDomain::FullyCompensated;
        const size_t nodeIdx = graph.nodes.size();
        graph.nodes.push_back(node);
        nodeTrackIdx.push_back(i);
        channelIdToNodeIdx[node.channelId] = nodeIdx;
    }

    // Synthetic master node. NOT mapped to a track index (nodeTrackIdx gets a
    // sentinel for it so the apply pass skips it).
    const size_t masterNodeIdx = graph.nodes.size();
    {
        LatencyGraph::Node masterNode;
        masterNode.channelId = kMasterSentinelId;
        masterNode.intrinsicLatency = 0; // P9 (G6) will populate from master FX.
        masterNode.muted = false;
        masterNode.domain = LatencyDomain::FullyCompensated;
        graph.nodes.push_back(masterNode);
        nodeTrackIdx.push_back(static_cast<size_t>(-1));
        channelIdToNodeIdx[kMasterSentinelId] = masterNodeIdx;
    }

    // Build edges from each track's mainOutputId (primary output) and sends.
    // Muted sends are excluded from the audible routing graph; this matches the
    // engine's render-time behavior in renderGraph() / addTrackEdge().
    for (size_t i = 0; i < trackCount; ++i) {
        auto* channel = trackManager->getChannel(i);
        if (!channel)
            continue;
        const auto srcIt = channelIdToNodeIdx.find(channel->getChannelId());
        if (srcIt == channelIdToNodeIdx.end())
            continue;
        const size_t srcNodeIdx = srcIt->second;

        // Primary output (mainOutputId). Implicit audible edge.
        const uint32_t mainOut = channel->getMainOutputId();
        if (auto mit = channelIdToNodeIdx.find(mainOut); mit != channelIdToNodeIdx.end()) {
            if (mit->second != srcNodeIdx) {
                LatencyGraph::Edge edge;
                edge.srcNodeIdx = static_cast<uint32_t>(srcNodeIdx);
                edge.dstNodeIdx = static_cast<uint32_t>(mit->second);
                edge.sidechainOnly = false;
                trackEdgeIndices[i].mainOutEdgeIdx = graph.edges.size();
                graph.edges.push_back(edge);
            }
        }

        // Explicit sends. Skip muted sends; preserve sidechainOnly flag so
        // the solver can keep sidechain edges out of audio path latency (G2/P5).
        const auto sends = channel->getSends();
        for (size_t sendSlot = 0; sendSlot < sends.size(); ++sendSlot) {
            const auto& send = sends[sendSlot];
            if (send.mute)
                continue;
            const auto dit = channelIdToNodeIdx.find(send.targetChannelId);
            if (dit == channelIdToNodeIdx.end())
                continue;
            if (dit->second == srcNodeIdx)
                continue; // self-loops dropped at the source.
            LatencyGraph::Edge edge;
            edge.srcNodeIdx = static_cast<uint32_t>(srcNodeIdx);
            edge.dstNodeIdx = static_cast<uint32_t>(dit->second);
            edge.sidechainOnly = send.sidechainOnly;
            trackEdgeIndices[i].sendEdgeBySlot.emplace_back(sendSlot, graph.edges.size());
            graph.edges.push_back(edge);
        }
    }

    // 2. Solve. Pure function. Off-RT.
    const SolvedLatencyTopology topology = solveLatency(graph);

    // 3. Apply solution to RT state (preserving existing ring-buffer behavior).
    m_maxProjectLatency = topology.projectAlignmentLatency;

    int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_relaxed);
    auto& rtStates = m_graphStates[activeIdx].trackStates;
    if (rtStates.size() < trackCount) {
        rtStates.resize(trackCount);
    }

    // Also write to m_trackState (the "golden" state that renderGraph() reads
    // via ensureTrackState()), not just the double-buffered m_graphStates slot.
    // renderGraph() reads PDC state from m_trackState, so without this mirror
    // the compensation values written below would be invisible on the RT path.
    if (m_trackState.size() < trackCount) {
        m_trackState.resize(trackCount);
    }

    for (size_t n = 0; n < topology.nodes.size(); ++n) {
        const size_t trackIdx = nodeTrackIdx[n];
        if (trackIdx >= rtStates.size())
            continue;
        const auto& sol = topology.nodes[n];

        auto& rtState = rtStates[trackIdx];
        const uint32_t prevDelay = rtState.compensationDelaySamples;
        rtState.pluginLatencySamples = sol.intrinsicLatency;
        rtState.compensationDelaySamples = sol.outputCompensationSamples;

        // Mirror to m_trackState so renderGraph() (which reads through
        // ensureTrackState() -> m_trackState) observes the same values.
        if (trackIdx < m_trackState.size()) {
            auto& goldenState = m_trackState[trackIdx];
            goldenState.pluginLatencySamples = sol.intrinsicLatency;
            goldenState.compensationDelaySamples = sol.outputCompensationSamples;
        }

        // v1 parity: reset the ring buffer when the delay changes. P6 (G3 smooth
        // recompute) will replace this with a sample-hold / crossfade migration.
        if (sol.outputCompensationSamples != prevDelay && sol.outputCompensationSamples > 0) {
            rtState.compensationBuffer.fill(0.0f);
            rtState.compensationWritePos = 0;
            rtState.compensationReadPos = 0;
        }
    }

    // 3b. P4b.2/P4b.3: Apply per-edge compensation values into TrackRTState's
    //     per-edge slots. RT-safety contract:
    //       * EdgeDelayState contains atomics; publish via release-store.
    //       * Buffer growth: allocate new array, retire old into retiredBuffer
    //         so any RT block in flight can finish reading. RT acquire-loads
    //         the buffer pointer at block entry.
    //       * compensationSamples / capacityMask atomically set after the
    //         buffer pointer so an acquiring reader observes a consistent slot.
    auto ensureEdgeCapacity = [](std::unique_ptr<EdgeDelayState>& slotPtr, uint32_t requiredSamples,
                                 uint32_t extraHeadroom) {
        if (requiredSamples == 0) {
            // Zero out the active comp value; keep any existing buffer alive
            // so a follow-up grow can complete without a fresh allocation.
            if (slotPtr) {
                slotPtr->compensationSamples.store(0, std::memory_order_release);
            }
            return;
        }
        if (!slotPtr) {
            slotPtr = std::make_unique<EdgeDelayState>();
        }
        // Round up to power of two >= required + extraHeadroom to give the RT
        // path room for one block of writes ahead of reads.
        uint32_t needed = requiredSamples + extraHeadroom;
        uint32_t capacity = 1;
        while (capacity < needed)
            capacity <<= 1;
        const size_t bufferFrames = capacity;
        const size_t bufferSamples = bufferFrames * 2u; // stereo interleaved

        const uint32_t currentMask = slotPtr->capacityMask.load(std::memory_order_relaxed);
        const bool needGrowth =
            (currentMask + 1u) < bufferFrames || slotPtr->bufferPtr.load(std::memory_order_relaxed) == nullptr;

        if (needGrowth) {
            // Retire the previous owning array. The single-deep retirement
            // queue is enough: between two off-RT recomputes the RT thread has
            // produced (and consumed) at least one block, so the previously-
            // retired buffer is no longer referenced by any in-flight block.
            slotPtr->retiredBuffer = std::move(slotPtr->ownedBuffer);
            slotPtr->ownedBuffer = std::unique_ptr<float[]>(new float[bufferSamples]());
            slotPtr->writePos.store(0, std::memory_order_release);
            // Publish: write pointer first, then mask (acquire-side reads
            // mask via the same critical section).
            slotPtr->bufferPtr.store(slotPtr->ownedBuffer.get(), std::memory_order_release);
            slotPtr->capacityMask.store(static_cast<uint32_t>(bufferFrames - 1), std::memory_order_release);
        }
        slotPtr->compensationSamples.store(requiredSamples, std::memory_order_release);
    };

    // One block of headroom is enough since the RT path will only ever look up
    // values written within the current or previous block.
    const uint32_t maxBlock = m_maxBufferFrames.load(std::memory_order_relaxed);
    const uint32_t kEdgeHeadroomSamples = maxBlock > 0 ? maxBlock : 1024;

    // P4b.2/P4b.3: Per-edge compensation must be allocated in m_trackState because
    // renderGraph() reads edge delays from ensureTrackState() -> m_trackState, not
    // from the double-buffered m_graphStates. The m_graphStates[activeIdx] copy
    // intentionally does NOT own separate EdgeDelayState objects — they contain a
    // non-atomic writePos that the RT thread updates each block, and duplicating
    // them would cause both the writePos divergence and the ownership/teardown race.
    //
    // getTrackEdgeDelaySnapshot() reads from m_graphStates for diagnostic tooling;
    // it returns the scalar atomic values that are correct in either copy.
    auto applyEdgeDelays = [&](std::vector<TrackRTState>& target) {
        for (size_t i = 0; i < trackCount && i < target.size(); ++i) {
            auto& state = target[i];
            const auto& bookkeeping = trackEdgeIndices[i];

            if (bookkeeping.mainOutEdgeIdx != static_cast<size_t>(-1) &&
                bookkeeping.mainOutEdgeIdx < topology.edges.size()) {
                const auto& edgeSol = topology.edges[bookkeeping.mainOutEdgeIdx];
                ensureEdgeCapacity(state.mainOutEdgeDelay, edgeSol.compensationSamples, kEdgeHeadroomSamples);
            } else {
                ensureEdgeCapacity(state.mainOutEdgeDelay, 0, kEdgeHeadroomSamples);
            }

            size_t maxSendSlot = 0;
            bool anySendSlot = false;
            for (const auto& pair : bookkeeping.sendEdgeBySlot) {
                if (!anySendSlot || pair.first > maxSendSlot)
                    maxSendSlot = pair.first;
                anySendSlot = true;
            }
            if (anySendSlot && state.sendEdgeDelays.size() <= maxSendSlot) {
                state.sendEdgeDelays.resize(maxSendSlot + 1);
            }
            for (auto& slotPtr : state.sendEdgeDelays) {
                if (slotPtr) {
                    slotPtr->compensationSamples.store(0, std::memory_order_release);
                }
            }
            for (const auto& pair : bookkeeping.sendEdgeBySlot) {
                const size_t sendSlot = pair.first;
                const size_t edgeIdx = pair.second;
                if (sendSlot >= state.sendEdgeDelays.size())
                    continue;
                if (edgeIdx >= topology.edges.size())
                    continue;
                const auto& edgeSol = topology.edges[edgeIdx];
                ensureEdgeCapacity(state.sendEdgeDelays[sendSlot], edgeSol.compensationSamples, kEdgeHeadroomSamples);
            }
        }
    };
    // Apply to m_trackState (RT render path reads from here via ensureTrackState()).
    if (m_trackState.size() >= trackCount) {
        applyEdgeDelays(m_trackState);
    }
    // Apply to m_graphStates[activeIdx] (getTrackEdgeDelaySnapshot reads from here).
    applyEdgeDelays(rtStates);

    // 4. Update RT-side graph state metadata (v1 contract preserved).
    m_graphStates[activeIdx].maxProjectLatencySamples = topology.projectAlignmentLatency;
    m_graphStates[activeIdx].latencyCompensationEnabled = m_latencyCompensationEnabled;

    // 5. Publish the solved topology via the lock-free double-buffer flip.
    //    Write to the inactive slot, then store the index with release ordering
    //    so readers acquire a fully-constructed topology.
    {
        const int prevIdx = m_activeSolvedTopologyIndex.load(std::memory_order_relaxed);
        const int inactiveIdx = 1 - prevIdx;
        m_solvedTopologies[inactiveIdx] = topology;
        m_activeSolvedTopologyIndex.store(inactiveIdx, std::memory_order_release);
    }

    m_latencyDirty = false;

    if (topology.projectAlignmentLatency > 0) {
        const uint32_t sr = m_sampleRate.load(std::memory_order_relaxed);
        double latencyMs = (sr > 0) ? ((topology.projectAlignmentLatency * 1000.0) / sr) : 0.0;
        Aestra::Log::info("[PDC] Max latency = " + std::to_string(topology.projectAlignmentLatency) + " samples (" +
                          std::to_string(latencyMs) + " ms)");
    }

    // Off-RT: log any solver warnings once per generation.
    for (const auto& warning : topology.warnings) {
        Aestra::Log::warning("[PDC] " + warning);
    }
}

SolvedLatencyTopology AudioEngine::getLastSolvedLatencyTopology() const {
    // Lock-free read: acquire the active index, copy the slot. The writer
    // released the slot before flipping the index, so the snapshot we see is
    // fully constructed.
    const int idx = m_activeSolvedTopologyIndex.load(std::memory_order_acquire);
    return m_solvedTopologies[idx];
}

AudioEngine::TrackEdgeDelaySnapshot AudioEngine::getTrackEdgeDelaySnapshot(size_t trackIndex) const {
    TrackEdgeDelaySnapshot snap;

    // Read ring-buffer sizing from m_graphStates (off-RT mirror) and
    // per-block writePos from m_trackState (canonical RT writer side).
    const int activeIdx = m_activeRenderTrackIndex.load(std::memory_order_acquire);
    const auto& rtStates = m_graphStates[activeIdx].trackStates;
    if (trackIndex >= rtStates.size()) {
        return snap;
    }
    const auto& state = rtStates[trackIndex];
    snap.valid = true;

    // writePos is updated on m_trackState by the RT thread each block.
    // m_graphStates edge delays do not have double-buffered EdgeDelayState
    // objects (see step 4 notes); their writePos is stale, so we overlay
    // from m_trackState when available.
    const auto* goldenState =
        (trackIndex < m_trackState.size()) ? &m_trackState[trackIndex] : nullptr;

    // Overlay writePos from m_trackState where the RT thread updates it.
    auto fillSlot = [](const std::unique_ptr<EdgeDelayState>& slotPtr) -> TrackEdgeDelaySnapshot::EdgeSlotSnapshot {
        TrackEdgeDelaySnapshot::EdgeSlotSnapshot s;
        if (!slotPtr)
            return s;
        s.compensationSamples = slotPtr->compensationSamples.load(std::memory_order_acquire);
        s.capacityMask = slotPtr->capacityMask.load(std::memory_order_acquire);
        const uint32_t capFrames =
            (s.capacityMask == 0 && slotPtr->bufferPtr.load(std::memory_order_acquire) == nullptr)
                ? 0u
                : (s.capacityMask + 1u);
        s.bufferBytes = static_cast<size_t>(capFrames) * 2u * sizeof(float);
        s.writePos = slotPtr->writePos.load(std::memory_order_acquire);
        return s;
    };

    snap.mainOutEdgeDelay = fillSlot(state.mainOutEdgeDelay);
    // Overlay writePos from goldenState (m_trackState) if available, since the
    // RT thread only updates writePos on the m_trackState-owned EdgeDelayState.
    if (goldenState && goldenState->mainOutEdgeDelay) {
        snap.mainOutEdgeDelay.writePos = goldenState->mainOutEdgeDelay->writePos.load(std::memory_order_acquire);
    }

    snap.sendEdgeDelays.reserve(state.sendEdgeDelays.size());
    for (size_t i = 0; i < state.sendEdgeDelays.size(); ++i) {
        auto s = fillSlot(state.sendEdgeDelays[i]);
        if (goldenState && i < goldenState->sendEdgeDelays.size() && goldenState->sendEdgeDelays[i]) {
            s.writePos = goldenState->sendEdgeDelays[i]->writePos.load(std::memory_order_acquire);
        }
        snap.sendEdgeDelays.push_back(s);
    }
    return snap;
}

void AudioEngine::setLatencyCompensationEnabled(bool enabled) {
    if (m_latencyCompensationEnabled != enabled) {
        m_latencyCompensationEnabled = enabled;
    }
    if (m_latencyDirty && m_latencyCompensationEnabled) {
        calculateLatencyCompensation();
    }
}

bool AudioEngine::isLatencyCompensationEnabled() const {
    return m_latencyCompensationEnabled;
}

uint32_t AudioEngine::getMaxProjectLatency() const {
    return m_maxProjectLatency;
}

void AudioEngine::markLatencyDirty() {
    m_latencyDirty = true;
}

} // namespace Audio
} // namespace Aestra
