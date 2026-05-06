// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PlaybackGraphController.h"
#include "AudioEngine.h"
#include "TrackManager.h"
#include "AudioGraphBuilder.h"
#include <atomic>

namespace Aestra {
namespace Audio {

bool PlaybackGraphController::isDirty() const {
    return m_trackManager && m_trackManager->hasPendingGraphRebuild();
}

uint64_t PlaybackGraphController::requestGeneration() const {
    return m_trackManager ? m_trackManager->graphRebuildRequestGeneration() : 0;
}

uint64_t PlaybackGraphController::graphGeneration() const {
    return m_graphGeneration.load(std::memory_order_relaxed);
}

PlaybackGraphController::GraphDirtyReason PlaybackGraphController::lastReason() const {
    return m_trackManager ? m_trackManager->lastGraphDirtyReason()
                          : GraphDirtyReason::Unknown;
}

void PlaybackGraphController::setTrackManager(TrackManager* trackManager) {
    m_trackManager = trackManager;
}

void PlaybackGraphController::setAudioEngine(AudioEngine* engine) {
    m_engine = engine;
}

void PlaybackGraphController::requestRebuild(GraphDirtyReason reason) {
    if (m_trackManager) {
        m_trackManager->requestAudioGraphRebuild(reason);
    }
}

bool PlaybackGraphController::drainIfDirty(double sampleRate) {
    if (!m_trackManager || !m_trackManager->consumePendingGraphRebuild()) {
        return false;
    }

    rebuildGraph(sampleRate);
    return true;
}

void PlaybackGraphController::rebuildGraph(double sampleRate) {
    if (!m_trackManager || !m_engine) {
        return;
    }

    auto graph = AudioGraphBuilder::buildFromTrackManager(*m_trackManager, sampleRate);
    m_engine->setGraph(graph);

    if (auto slotMap = m_trackManager->getChannelSlotMapShared()) {
        m_engine->setChannelSlotMap(slotMap);
    }

    m_engine->setContinuousParams(m_trackManager->getContinuousParams());
    m_trackManager->rebuildAndPushSnapshot();

    m_graphGeneration.fetch_add(1, std::memory_order_release);
}

} // namespace Audio
} // namespace Aestra