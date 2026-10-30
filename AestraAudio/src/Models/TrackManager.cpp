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

void TrackManager::commitRecordingTake(uint64_t trackId, const RecordingCapture& capture, double fallbackStartBeat,
                                       bool forcePlacementStartBeat) {
    auto* track = getTrack(trackId);
    if (!track) {
        Log::warning("[TrackManager] Could not resolve Track for recorded take: " + std::to_string(trackId));
        return;
    }
    const uint32_t channelId = track->channelId;
    std::vector<float> capturedSamples = capture.stream ? capture.stream->samples() : copyCaptureSamples(capture);
    if (capture.stream && capture.stream->lostFrames() > 0) {
        Log::warning("[TrackManager] Recorded take on track " + std::to_string(trackId) + " lost " +
                     std::to_string(capture.stream->lostFrames()) +
                     " frame(s) the disk writer could not keep up with; they are silence in the take.");
    }
    if (capturedSamples.empty()) {
        if (capture.stream) {
            capture.stream->discardPartialFile();
        }
        // Silent no-op here made "record produced nothing" undiagnosable
        // (deferred-capture beat unreachable, #845). Say something.
        Log::warning("[TrackManager] Recorded take on track " + std::to_string(trackId) +
                     " captured zero samples — capture start was never reached during the take.");
        return;
    }

    if (!getChannelById(channelId)) {
        Log::warning("[TrackManager] Could not resolve mixer insert for recorded take " +
                     std::to_string(channelId) + " (track " + std::to_string(trackId) + ")");
        return;
    }

    std::vector<float> conditionedSamples = std::move(capturedSamples);
    const float rawPeak = analyzePeak(conditionedSamples);
    conditionRecordedTakeSamples(conditionedSamples);
    const float conditionedPeak = analyzePeak(conditionedSamples);

    const double captureStartBeat = capture.startBeat.load(std::memory_order_acquire);
    double startBeat = forcePlacementStartBeat
                           ? fallbackStartBeat
                           : (captureStartBeat > 0.0 ? captureStartBeat : fallbackStartBeat);
    // T-6: compensate device latency. The captured signal arrived late by
    // the input latency and was performed against backing heard late by
    // the output latency, so the take as placed sounds late by the sum.
    // Shift placement earlier (samples intact); clamp at zero — a take
    // starting at the very top can only keep a small residual lateness.
    // Values are pulled live through the provider so buffer/device/rate
    // reconfigures from any path are reflected at commit time.
    const double bpm = std::max(1.0, m_playlistModel.getBPM());
    double latencyInMs = 0.0, latencyOutMs = 0.0;
    if (m_recordLatencyProvider) {
        m_recordLatencyProvider(latencyInMs, latencyOutMs);
    }
    const double recordLatencyBeats = (std::max(0.0, latencyInMs) + std::max(0.0, latencyOutMs)) / 1000.0 *
                                      bpm / 60.0;
    startBeat = std::max(0.0, startBeat - recordLatencyBeats);
    const double durationBeats = framesToBeats(static_cast<double>(conditionedSamples.size()));
    if (durationBeats <= 0.0) {
        return;
    }
    const double durationSeconds =
        static_cast<double>(conditionedSamples.size()) / std::max(1.0, m_inputSampleRate);
    const float playbackGain = computeRecordedTakeGain(conditionedSamples);

    auto buffer = std::make_shared<AudioBufferData>();
    buffer->sampleRate = static_cast<uint32_t>(std::max(1.0, m_inputSampleRate));
    buffer->numChannels = 2;
    buffer->numFrames = conditionedSamples.size();
    buffer->interleavedData.resize(conditionedSamples.size() * 2);
    for (size_t i = 0; i < conditionedSamples.size(); ++i) {
        const float sample = conditionedSamples[i];
        buffer->interleavedData[i * 2] = sample;
        buffer->interleavedData[i * 2 + 1] = sample;
    }

    std::string takePath = buildRecordingTakePath(channelId);
    if (capture.stream && !capture.stream->partialPath().empty()) {
        const std::string& partial = capture.stream->partialPath();
        takePath = partial.substr(0, partial.size() - std::strlen(RecordingTakeStream::kPartialExtension));
    }
    if (!writeRecordedTakeWav(takePath, *buffer)) {
        // The partial file stays: it is now the only copy of this take.
        Log::error("[TrackManager] Failed to write recorded take: " + takePath);
        return;
    }
    if (capture.stream) {
        capture.stream->discardPartialFile();
    }

    const std::string takeName = std::filesystem::path(takePath).stem().string();
    ClipSourceID sourceId = m_sourceManager.createRecordedSource(takePath, takeName, buffer);
    if (!sourceId.isValid()) {
        Log::error("[TrackManager] Failed to register recorded source for: " + takePath);
        return;
    }

    AudioSlicePayload payload;
    payload.audioSourceId = sourceId;
    payload.durationSeconds = durationSeconds;
    // Populate the sample-domain fields the serializer persists
    // (startSamples/lengthSamples). The old {0.0, numFrames} form set
    // startOffset/duration instead, so the slice saved as start:0 length:0.
    AudioSlice fullSlice;
    fullSlice.startSamples = 0.0;
    fullSlice.lengthSamples = static_cast<double>(buffer->numFrames);
    payload.slices.push_back(fullSlice);

    PatternID patternId = m_patternManager.createAudioPattern(takeName, durationBeats, payload);
    if (!patternId.isValid()) {
        Log::error("[TrackManager] Failed to create audio pattern for recorded take.");
        return;
    }
    m_patternManager.setPatternMixerChannel(patternId, channelId);

    ClipInstance clip;
    clip.id = ClipInstanceID::generate();
    clip.name = takeName;
    clip.startBeat = startBeat;
    clip.durationBeats = durationBeats;
    clip.durationSeconds = durationSeconds;
    clip.patternId = patternId;
    clip.sourceId = patternId.value;
    clip.edits.gainLinear = playbackGain;

    if (recordTakeInPlace(m_playlistModel, m_commandHistory, *track, clip)) { // the armed lane, if the span is free
        requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged); m_modified.store(true, std::memory_order_relaxed);
        Log::info("[TrackManager] Recorded take committed in the track's own lane: " + takePath);
        if (m_onTakeCommitted) m_onTakeCommitted(track->activeLaneId);
        return;
    }
    // Recording destination and Playlist placement are separate: a take lane, the PatternSource keeps the armed insert.
    auto createLane = std::make_shared<CreateLaneCommand>(m_playlistModel, takeName);
    createLane->execute();
    const PlaylistLaneID laneId = createLane->getLaneId();
    if (!laneId.isValid()) {
        m_patternManager.removePattern(patternId);
        Log::error("[TrackManager] Failed to create a Playlist lane for recorded take.");
        return;
    }

    auto addClip = std::make_shared<AddClipCommand>(m_playlistModel, laneId, clip);
    addClip->execute();
    if (!m_playlistModel.getClip(clip.id)) {
        createLane->undo();
        m_patternManager.removePattern(patternId);
        Log::error("[TrackManager] Failed to place recorded take on its Playlist lane.");
        return;
    }

    // The take lane is owned by the recording Track (FD-14), joined in the same undoable step as its creation.
    auto attachLane = std::make_shared<AttachLaneToTrackCommand>(*this, trackId, laneId);
    attachLane->execute();
    auto* ownedLane = m_playlistModel.getLane(laneId);
    if (!ownedLane || ownedLane->trackId != trackId) {
        attachLane->undo();
        addClip->undo();
        createLane->undo();
        m_patternManager.removePattern(patternId);
        Log::error("[TrackManager] Failed to attach recorded take lane to track " + std::to_string(trackId));
        return;
    }

    auto transaction = std::make_shared<CommandTransaction>("Record Take");
    transaction->add(createLane);
    transaction->add(addClip);
    transaction->add(attachLane);
    transaction->markExecuted();
    if (!m_commandHistory.pushExecuted(transaction)) {
        attachLane->undo();
        addClip->undo();
        createLane->undo();
        m_patternManager.removePattern(patternId);
        Log::error("[TrackManager] Failed to add recorded take to command history.");
        return;
    }
    requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged); m_modified.store(true, std::memory_order_relaxed);
    Log::info("[TrackManager] Recorded take committed: " + takePath + " to mixer insert " + std::to_string(channelId) + " at beat " +
              std::to_string(startBeat) + " with raw peak " + std::to_string(rawPeak) + ", conditioned peak " +
              std::to_string(conditionedPeak) + ", clip gain " + std::to_string(playbackGain));

    if (m_onTakeCommitted) m_onTakeCommitted(laneId);
}

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
