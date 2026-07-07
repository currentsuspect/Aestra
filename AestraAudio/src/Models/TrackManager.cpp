#include "TrackManager.h"

#include "ClipPrefilterService.h"
#include "DSP/ClipPrefilter.h"

namespace Aestra {
namespace Audio {

// Out-of-line: m_clipPrefilterService is a unique_ptr to a type that is only
// forward-declared in the header. It is the LAST declared member, so it is
// destroyed FIRST here — the worker joins while the graph-dirty atomics its
// completion callback touches are still alive.
TrackManager::~TrackManager() = default;

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

    // 2. Clear stale variants; queue missing work for downsampled sources.
    for (const ClipSourceID id : m_sourceManager.getAllSourceIDs()) {
        ClipSource* source = m_sourceManager.getSource(id);
        if (source == nullptr) {
            continue;
        }
        const auto buffer = source->getBuffer();
        const bool needed =
            buffer && buffer->isValid() && ClipPrefilter::isNeeded(buffer->sampleRate, targetRate);

        if (!needed) {
            if (source->hasFilteredVariant()) {
                source->clearFilteredVariant(); // rate no longer qualifies — free the copy
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
        if (source->getFilteredBufferFor(targetRate)) {
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

} // namespace Audio
} // namespace Aestra
