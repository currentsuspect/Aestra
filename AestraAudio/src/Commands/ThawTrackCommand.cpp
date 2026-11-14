// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/ThawTrackCommand.h"

#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

namespace {

void setClipMuted(PlaylistModel& playlist, const ClipInstanceID& id, bool muted) {
    if (const ClipInstance* clip = playlist.getClip(id)) {
        ClipEdits edits = clip->edits;
        edits.muted = muted;
        playlist.setClipEdits(id, edits);
    }
}

void setCurvesMuted(PlaylistModel& playlist, const FrozenTrackState& state, bool muted) {
    for (const auto& key : state.mutedCurves) {
        PlaylistLane* lane = playlist.getLane(key.lane);
        if (!lane)
            continue;
        for (auto& curve : lane->automationCurves)
            if (curve.deviceInstanceId == key.deviceInstanceId && curve.paramId == key.paramId)
                curve.muted = muted;
    }
}

void setSlotsBypassed(MixerChannel& channel, const FrozenTrackState& state, bool bypassed) {
    auto& chain = channel.getEffectChain();
    for (const uint32_t slot : state.bypassedSlots)
        if (slot < chain.slotCount() && !chain.isSlotEmpty(slot))
            chain.setSlotBypassed(slot, bypassed);
}

} // namespace

ThawTrackCommand::ThawTrackCommand(TrackManager& manager, uint32_t mixerChannelId)
    : m_manager(manager), m_channelId(mixerChannelId) {}

void ThawTrackCommand::execute() {
    if (m_executed)
        return;
    MixerChannel* channel = m_manager.getChannelById(m_channelId);
    if (!channel || !channel->frozenTrack().active())
        return; // not frozen: change nothing
    auto& playlist = m_manager.getPlaylistModel();

    m_state = channel->frozenTrack();
    const ClipInstance* frozen = playlist.getClip(m_state.frozenClip);
    m_hadFrozenClip = frozen != nullptr;
    if (frozen) {
        m_frozenClip = *frozen;
        m_frozenLane = playlist.findClipLane(m_state.frozenClip);
        playlist.removeClip(m_state.frozenClip);
        // Detached, not deleted: undo puts this exact pattern back and its file stays on disk.
        m_detachedPattern = m_manager.getPatternManager().detachPattern(m_frozenClip.patternId);
    }

    for (const auto& id : m_state.mutedClips)
        setClipMuted(playlist, id, false);
    setSlotsBypassed(*channel, m_state, false);
    setCurvesMuted(playlist, m_state, false);
    channel->frozenTrack().clear();

    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    m_executed = true;
}

void ThawTrackCommand::undo() {
    if (!m_executed)
        return;
    MixerChannel* channel = m_manager.getChannelById(m_channelId);
    if (!channel)
        return;
    auto& playlist = m_manager.getPlaylistModel();

    if (m_hadFrozenClip) {
        if (m_detachedPattern)
            m_manager.getPatternManager().reinsertPattern(m_detachedPattern);
        playlist.addClip(m_frozenLane, m_frozenClip);
    }
    for (const auto& id : m_state.mutedClips)
        setClipMuted(playlist, id, true);
    setSlotsBypassed(*channel, m_state, true);
    setCurvesMuted(playlist, m_state, true);
    channel->frozenTrack() = m_state;

    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    m_executed = false;
}

} // namespace Audio
} // namespace Aestra
