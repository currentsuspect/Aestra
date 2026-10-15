// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Commands/BounceInPlaceCommand.h"

#include "AestraLog.h"
#include "Models/ClipRenderService.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

BounceInPlaceCommand::BounceInPlaceCommand(TrackManager& manager, std::vector<ClipInstanceID> clipIds,
                                           uint32_t mixerChannelId, std::shared_ptr<AudioBufferData> rendered,
                                           double startBeat, double lengthBeats)
    : m_manager(manager), m_clipIds(std::move(clipIds)), m_mixerChannelId(mixerChannelId),
      m_rendered(std::move(rendered)), m_startBeat(startBeat), m_lengthBeats(lengthBeats) {}

size_t BounceInPlaceCommand::getSizeInBytes() const {
    // The rendered buffer is held only until the first execute() has written it.
    return sizeof(*this) + (m_rendered ? m_rendered->numFrames * m_rendered->numChannels * sizeof(float) : 0);
}

void BounceInPlaceCommand::execute() {
    if (m_executed)
        return;
    auto& playlist = m_manager.getPlaylistModel();

    if (m_removed.empty()) {
        for (const auto& id : m_clipIds) {
            const ClipInstance* clip = playlist.getClip(id);
            const PlaylistLaneID lane = playlist.findClipLane(id);
            if (!clip || !lane.isValid())
                return; // a clip vanished since the render: change nothing
            m_removed.push_back({*clip, lane});
        }
        if (m_removed.empty() || !m_rendered || !m_rendered->isValid()) {
            m_removed.clear();
            return;
        }
        ClipRenderService service(m_manager.getSourceManager(), m_manager.getPatternManager());
        const auto result = service.commit(*m_rendered, m_manager.renderRootDirectory(),
                                           (m_removed.front().clip.name.empty() ? std::string("Clip")
                                                                                : m_removed.front().clip.name) +
                                               " Bounced",
                                           m_lengthBeats, m_mixerChannelId);
        if (!result.isValid()) {
            m_removed.clear();
            Log::warning("[BounceInPlaceCommand] Could not write the bounced audio.");
            return;
        }
        m_patternId = result.patternId;
        m_rendered.reset(); // on disk now; redo reinserts the pattern instead of writing again
    } else if (m_detachedPattern && !m_manager.getPatternManager().reinsertPattern(m_detachedPattern)) {
        return;
    }

    for (const auto& removed : m_removed)
        playlist.removeClip(removed.clip.id);

    m_newClipLane = m_removed.front().lane;
    if (!m_newClip.id.isValid()) {
        const ClipInstanceID id = playlist.addClipFromPattern(m_newClipLane, m_patternId, m_startBeat, m_lengthBeats);
        // The render already contains the clips' own gain, fades and rate; a new audio clip's default
        // gain would be applied on top of it.
        playlist.setClipEdits(id, ClipEdits{});
        const ClipInstance* placed = playlist.getClip(id);
        if (!placed) {
            for (const auto& removed : m_removed)
                playlist.addClip(removed.lane, removed.clip);
            return;
        }
        m_newClip = *placed;
    } else {
        playlist.addClip(m_newClipLane, m_newClip);
    }

    bypassInserts();
    muteInsertAutomation();

    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    m_executed = true;
}

void BounceInPlaceCommand::undo() {
    if (!m_executed)
        return;
    auto& playlist = m_manager.getPlaylistModel();

    restoreAutomation();
    restoreInserts();

    playlist.removeClip(m_newClip.id);
    for (const auto& removed : m_removed)
        playlist.addClip(removed.lane, removed.clip);
    // Detach rather than remove: redo reinserts this exact pattern; its file stays on disk.
    m_detachedPattern = m_manager.getPatternManager().detachPattern(m_patternId);

    m_manager.markModified();
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
    m_manager.requestAudioGraphRebuild(GraphDirtyReason::EffectChainChanged);
    m_executed = false;
}

void BounceInPlaceCommand::bypassInserts() {
    m_bypassedByUs.clear();
    if (m_mixerChannelId == MASTER_MIXER_CHANNEL_ID)
        return; // a bounce onto Master was rendered after every track's effects and no longer passes through them
    MixerChannel* channel = m_manager.getChannelById(m_mixerChannelId);
    if (!channel)
        return;
    auto& chain = channel->getEffectChain();
    for (size_t slot = 0; slot < chain.slotCount(); ++slot) {
        // Only what we switch is ours to switch back: a slot the user had already bypassed stays so.
        if (chain.isSlotEmpty(slot) || chain.isSlotBypassed(slot))
            continue;
        chain.setSlotBypassed(slot, true);
        m_bypassedByUs.push_back(slot);
    }
}

void BounceInPlaceCommand::restoreInserts() {
    MixerChannel* channel = m_manager.getChannelById(m_mixerChannelId);
    if (!channel)
        return;
    auto& chain = channel->getEffectChain();
    for (const size_t slot : m_bypassedByUs) {
        if (slot < chain.slotCount())
            chain.setSlotBypassed(slot, false);
    }
}

void BounceInPlaceCommand::muteInsertAutomation() {
    m_mutedLanes.clear();
    if (m_mixerChannelId == MASTER_MIXER_CHANNEL_ID)
        return; // nothing was baked in that would play twice; the Master's own automation is not ours to mute
    auto& playlist = m_manager.getPlaylistModel();
    for (size_t i = 0; i < playlist.getLaneCount(); ++i) {
        auto* lane = playlist.getLane(playlist.getLaneId(i));
        if (!lane)
            continue;
        const auto before = lane->automationCurves;
        bool changed = false;
        for (auto& curve : lane->automationCurves) {
            // Plugin-parameter curves are what the inserts played; volume and pan curves drive the fader,
            // which comes after the bounce and stays live, so they keep running.
            if (curve.mixerChannelId == m_mixerChannelId && curve.getAutomationTarget() == AutomationTarget::Custom &&
                !curve.muted) {
                curve.muted = true;
                changed = true;
            }
        }
        if (changed)
            m_mutedLanes.push_back({lane->id, before});
    }
}

void BounceInPlaceCommand::restoreAutomation() {
    auto& playlist = m_manager.getPlaylistModel();
    for (const auto& entry : m_mutedLanes) {
        if (auto* lane = playlist.getLane(entry.lane))
            lane->automationCurves = entry.before;
    }
    m_mutedLanes.clear();
}

} // namespace Audio
} // namespace Aestra
