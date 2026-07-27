#include "Commands/MuseStubs.h"

#include <algorithm>
#include <string>

namespace Aestra {
namespace Audio {

// === SetBpmCommand ===
SetBpmCommand::SetBpmCommand(AudioEngine& engine, float bpm) : m_engine(engine), m_bpm(bpm) {}

void SetBpmCommand::execute() {
    if (m_executed)
        return;
    m_previousBpm = m_engine.getBPM();
    m_engine.setBPM(m_bpm);
    m_executed = true;
}

void SetBpmCommand::undo() {
    if (!m_executed)
        return;
    m_engine.setBPM(m_previousBpm);
    m_executed = false;
}

void SetBpmCommand::redo() {
    if (m_executed)
        return;
    m_previousBpm = m_engine.getBPM();
    m_engine.setBPM(m_bpm);
    m_executed = true;
}

std::string SetBpmCommand::getName() const {
    return "Set BPM";
}

// === PlayCommand ===
PlayCommand::PlayCommand(AudioEngine& engine) : m_engine(engine) {}

void PlayCommand::execute() {
    if (m_executed)
        return;
    m_wasPlaying = m_engine.isTransportPlaying();
    m_engine.setTransportPlaying(true);
    m_executed = true;
}

void PlayCommand::undo() {
    if (!m_executed)
        return;
    m_engine.setTransportPlaying(m_wasPlaying);
    m_executed = false;
}

void PlayCommand::redo() {
    if (m_executed)
        return;
    m_engine.setTransportPlaying(true);
    m_executed = true;
}

std::string PlayCommand::getName() const {
    return "Play";
}

// === StopCommand ===
StopCommand::StopCommand(AudioEngine& engine) : m_engine(engine) {}

void StopCommand::execute() {
    if (m_executed)
        return;
    m_wasPlaying = m_engine.isTransportPlaying();
    m_engine.setTransportPlaying(false);
    m_executed = true;
}

void StopCommand::undo() {
    if (!m_executed)
        return;
    m_engine.setTransportPlaying(m_wasPlaying);
    m_executed = false;
}

void StopCommand::redo() {
    if (m_executed)
        return;
    m_engine.setTransportPlaying(false);
    m_executed = true;
}

std::string StopCommand::getName() const {
    return "Stop";
}

// === DeleteTrackCommand ===
DeleteTrackCommand::DeleteTrackCommand(TrackManager& manager, int trackIndex)
    : m_manager(manager), m_trackIndex(trackIndex) {}

void DeleteTrackCommand::execute() {
    if (m_executed)
        return;

    MixerChannel* ch = m_manager.getChannel(static_cast<size_t>(m_trackIndex));
    if (!ch)
        return;

    m_deletedName = ch->getName();
    m_deletedId = ch->getChannelId();

    if (!m_routesCaptured) {
        for (UnitID unitId : m_manager.getUnitManager().getAllUnitIDs()) {
            if (m_manager.getUnitManager().getUnitMixerChannel(unitId) == m_deletedId) {
                m_routedUnits.push_back(unitId);
            }
        }
        for (const auto& pattern : m_manager.getPatternManager().getAllPatterns()) {
            if (pattern && pattern->isAudio() && pattern->getMixerChannelId() == m_deletedId) {
                m_routedAudioPatterns.push_back(pattern->id);
            }
        }
        for (const auto& channel : m_manager.getChannelsSnapshot()) {
            if (!channel || channel->getChannelId() == m_deletedId)
                continue;
            const auto sends = channel->getSends();
            const bool routesToDeleted = channel->getMainOutputId() == m_deletedId ||
                                         std::any_of(sends.begin(), sends.end(), [this](const AudioRoute& route) {
                                             return route.targetChannelId == m_deletedId;
                                         });
            if (routesToDeleted) {
                m_mixerRoutes.push_back({channel->getChannelId(), channel->getMainOutputId(), sends});
            }
        }
        m_routesCaptured = true;
    }

    // Detach rather than destroy: this command owns the channel for as long as
    // the delete stands, so undo can put back the same object at the same id
    // and index. See the m_detached comment in the header for why an equivalent
    // channel is not good enough.
    m_detached = m_manager.detachChannelById(m_deletedId, m_detachedIndex);
    if (m_detached) {
        m_manager.getUnitManager().resetMixerChannel(m_deletedId);
        m_manager.getPatternManager().resetMixerChannel(m_deletedId);
        m_manager.resetMixerRoutingDestination(m_deletedId);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        m_executed = true;
    }
}

void DeleteTrackCommand::undo() {
    if (!m_executed || !m_detached)
        return;

    if (m_manager.reinsertChannel(m_detached, m_detachedIndex)) {
        for (UnitID unitId : m_routedUnits) {
            m_manager.getUnitManager().setUnitMixerChannel(unitId, m_deletedId);
        }
        for (PatternID patternId : m_routedAudioPatterns) {
            m_manager.getPatternManager().setPatternMixerChannel(patternId, m_deletedId);
        }
        for (const auto& routeSnapshot : m_mixerRoutes) {
            if (auto* channel = m_manager.getChannelById(routeSnapshot.sourceChannelId)) {
                channel->setMainOutputId(routeSnapshot.mainOutputId);
                channel->replaceSends(routeSnapshot.sends);
            }
        }
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        m_executed = false;
    }
}

void DeleteTrackCommand::redo() {
    if (m_executed)
        return;

    m_detached = m_manager.detachChannelById(m_deletedId, m_detachedIndex);
    if (m_detached) {
        m_manager.getUnitManager().resetMixerChannel(m_deletedId);
        m_manager.getPatternManager().resetMixerChannel(m_deletedId);
        m_manager.resetMixerRoutingDestination(m_deletedId);
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::RoutingChanged);
        m_executed = true;
    }
}

std::string DeleteTrackCommand::getName() const {
    return "Delete Track";
}

// === RenameTrackCommand ===
RenameTrackCommand::RenameTrackCommand(TrackManager& manager, int trackIndex, const std::string& name)
    : m_manager(manager), m_trackIndex(trackIndex), m_newName(name) {}

void RenameTrackCommand::execute() {
    if (m_executed)
        return;

    MixerChannel* ch = m_manager.getChannel(static_cast<size_t>(m_trackIndex));
    if (!ch)
        return;

    m_oldName = ch->getName();
    ch->setName(m_newName);
    m_executed = true;
}

void RenameTrackCommand::undo() {
    if (!m_executed)
        return;

    MixerChannel* ch = m_manager.getChannel(static_cast<size_t>(m_trackIndex));
    if (!ch)
        return;

    ch->setName(m_oldName);
    m_executed = false;
}

void RenameTrackCommand::redo() {
    if (m_executed)
        return;

    MixerChannel* ch = m_manager.getChannel(static_cast<size_t>(m_trackIndex));
    if (!ch)
        return;

    ch->setName(m_newName);
    m_executed = true;
}

std::string RenameTrackCommand::getName() const {
    return "Rename Track";
}

} // namespace Audio
} // namespace Aestra
