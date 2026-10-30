#include <cmath>
#include <unordered_set>
#include "TrackManager.h"
#include <unordered_map>
#include <algorithm>

#include "ClipPrefilterService.h"
#include "Plugin/SamplerPlugin.h"
#include "DSP/ClipPrefilter.h"

namespace Aestra {
namespace Audio {

// Out-of-line: m_clipPrefilterService is a unique_ptr to a type that is only
// forward-declared in the header. It is the LAST declared member, so it is
// destroyed FIRST here — the worker joins while the graph-dirty atomics its
// completion callback touches are still alive.
TrackManager::~TrackManager() { stopTakeDrainThread(); }

void TrackManager::drainRecordingCaptures() {
    if (reportRealtimeMisuse("TrackManager::drainRecordingCaptures")) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_recordingMutex);
    for (auto& [trackId, capture] : m_recordingCaptures) {
        (void)trackId;
        if (capture && capture->stream) {
            capture->stream->drain(capture->samples.get(), capture->capacity, capture->totalCapturedFrames);
        }
    }
}

void TrackManager::startTakeDrainThread() {
    if (m_takeDrainThread.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_takeDrainWakeMutex);
        m_takeDrainStop = false;
    }
    // The capture ring holds m_maxRecordingSeconds; draining every 50 ms
    // leaves that whole span as headroom for a stalled disk.
    m_takeDrainThread = std::thread([this]() {
        std::unique_lock<std::mutex> lock(m_takeDrainWakeMutex);
        while (!m_takeDrainStop) {
            m_takeDrainWake.wait_for(lock, std::chrono::milliseconds(50), [this]() { return m_takeDrainStop; });
            lock.unlock();
            drainRecordingCaptures();
            lock.lock();
        }
    });
}

void TrackManager::stopTakeDrainThread() {
    {
        std::lock_guard<std::mutex> lock(m_takeDrainWakeMutex);
        m_takeDrainStop = true;
    }
    m_takeDrainWake.notify_all();
    if (m_takeDrainThread.joinable()) {
        m_takeDrainThread.join();
    }
}

void TrackManager::rebuildAndPushSnapshot() {
    // Effect chain snapshots are retrieved during graph building (AudioGraphBuilder::buildFromTrackManager)
    // via getSnapshot(). Dirty state is consumed by PlaybackGraphController::consumePendingGraphRebuild().
    // This method is kept as a post-graph-rebuild hook for existing callers.
}

void TrackManager::ensureClipPrefilters() {
    // Graph-build thread only (see the ClipSource threading contract).
    const double projectRate = m_playlistModel.getProjectSampleRate();
    const uint32_t targetRate = projectRate > 0.0 ? static_cast<uint32_t>(projectRate) : 0;

    // 1. Apply finished results whose key still matches the live state.
    if (m_clipPrefilterService) {
        for (auto& result : m_clipPrefilterService->drainCompleted()) {
            ClipSource* source = m_sourceManager.getSource(ClipSourceID{result.sourceId});
            if (source == nullptr || !result.filtered) {
                continue;
            }
            if (source->getContentRevision() != result.contentRevision ||
                result.targetRate != targetRate ||
                result.specVersion != ClipPrefilter::kSpecVersion) {
                continue; // stale (buffer replaced / rate changed since enqueue) — discard
            }
            source->setFilteredVariant(std::move(result.filtered), result.targetRate,
                                       result.contentRevision, result.specVersion);
        }
    }

    if (targetRate == 0) {
        return;
    }

    // 2. Clear stale variants; queue missing work for downsampled sources THAT A CLIP PLAYS. A filtered
    // copy is a full float32 copy of the source, so one made for a source nothing plays (a browser preview,
    // a deleted clip's leftover) is memory for nothing.
    std::unordered_set<uint64_t> playedSources;
    std::unordered_set<uint64_t> variedSources; // played by some clip at a speed or pitch other than 1
    for (size_t l = 0; l < m_playlistModel.getLaneCount(); ++l) {
        const PlaylistLane* lane = m_playlistModel.getLane(m_playlistModel.getLaneId(l));
        if (lane == nullptr) {
            continue;
        }
        for (const ClipInstance& clip : lane->clips) {
            const PatternSource* pattern = m_patternManager.getPattern(clip.patternId);
            if (pattern != nullptr && pattern->isAudio()) {
                const uint64_t sourceKey = std::get<AudioSlicePayload>(pattern->payload).audioSourceId.value;
                playedSources.insert(sourceKey);
                if (std::fabs(static_cast<double>(clip.edits.effectiveVarispeed()) - 1.0) > 1e-6) {
                    variedSources.insert(sourceKey);
                }
            }
        }
    }
    for (const ClipSourceID id : m_sourceManager.getAllSourceIDs()) {
        ClipSource* source = m_sourceManager.getSource(id);
        if (source == nullptr) {
            continue;
        }
        const auto buffer = source->getBuffer();
        const bool needed = buffer && buffer->isValid() && ClipPrefilter::isNeeded(buffer->sampleRate, targetRate) &&
                            playedSources.count(id.value) > 0;

        if (!needed) {
            if (source->hasFilteredVariant()) {
                source->clearFilteredVariant(); // rate no longer qualifies, or nothing plays it — free the copy
            }
            continue;
        }
        if (source->hasFilteredVariant() && !source->getFilteredBufferFor(targetRate)) {
            source->clearFilteredVariant(); // keyed for an old rate/content — free it
        }
        if (source->filteredVariantSpec() != 0 &&
            source->filteredVariantSpec() != ClipPrefilter::kSpecVersion) {
            source->clearFilteredVariant(); // filter spec changed
        }
        // A whole-number rate ratio can be stored decimated: half the memory and no resampling, and the
        // kernel plays it exactly as it played the full-rate copy (bit for bit) at speed and pitch 1. At any
        // other speed the two differ (the decimated data is interpolated close to its Nyquist), so a source
        // some clip plays at another speed needs the full-rate copy. The full-rate copy is sticky: once a
        // source has it, it keeps it. While an upgrade builds, the decimated copy keeps playing.
        const uint32_t factor = ClipPrefilter::decimationFactor(buffer->sampleRate, targetRate);
        const auto current = source->getFilteredBufferFor(targetRate);
        const bool currentIsFullRate = current && current->sampleRate != targetRate;
        const uint32_t wantDecimation =
            (factor > 1 && variedSources.count(id.value) == 0 && !currentIsFullRate) ? factor : 1;
        if (current && (currentIsFullRate || wantDecimation > 1)) {
            continue; // ready
        }

        if (!m_clipPrefilterService) {
            // Completion callback: atomic-only graph-dirty request (thread-safe from
            // the worker), so the app's graph pump applies the copy on next drain.
            m_clipPrefilterService = std::make_unique<ClipPrefilterService>(
                [this] { requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged); });
        }
        ClipPrefilterService::Job job;
        job.sourceId = id.value;
        job.contentRevision = source->getContentRevision();
        job.targetRate = targetRate;
        job.decimation = wantDecimation;
        job.source = source->getSharedBuffer();
        m_clipPrefilterService->enqueue(std::move(job)); // dedupes internally
    }
}

void TrackManager::waitForClipPrefilters() {
    if (!m_clipPrefilterService) {
        return;
    }
    m_clipPrefilterService->waitIdle();
    ensureClipPrefilters(); // apply the drained results deterministically
}

bool TrackManager::prewarmSamplerKeepLength(bool wait) {
    using Plugins::SamplerPlugin;
    // Pitches each unit's notes use, across every pattern (a unit's notes can live in any pattern).
    std::unordered_map<UnitID, std::vector<int>> pitchesByUnit;
    for (const auto& pattern : m_patternManager.getAllPatterns()) {
        if (!pattern || !pattern->isMidi()) continue;
        for (const auto& note : pattern->getMidiNotes()) {
            auto& pitches = pitchesByUnit[note.unitId];
            if (std::find(pitches.begin(), pitches.end(), note.pitch) == pitches.end()) {
                pitches.push_back(note.pitch);
            }
        }
    }
    bool allReady = true;
    for (UnitID id : m_unitManager.getAllUnitIDs()) {
        auto sampler = std::dynamic_pointer_cast<SamplerPlugin>(m_unitManager.getUnitPlugin(id));
        if (!sampler || sampler->getPitchMode() != SamplerPlugin::PitchMode::KeepLength) continue;
        const auto it = pitchesByUnit.find(id);
        if (it == pitchesByUnit.end()) continue;
        allReady = sampler->prewarmKeepLength(it->second, wait) && allReady;
    }
    return allReady;
}

} // namespace Audio
} // namespace Aestra
