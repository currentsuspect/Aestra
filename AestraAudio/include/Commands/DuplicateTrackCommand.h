// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/AddChannelCommand.h"
#include "Commands/ICommand.h"
#include "Models/TrackManager.h"
#include "Plugin/PluginManager.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Duplicate a track with its chain and clips, right under it, as one undo step (DAW Essentials Checklist).
 *
 * The copy gets its own mixer channel: the source's level, pan, width, trim, colour, output, sends and its whole
 * plugin chain (saved and loaded as a project does, so every plugin keeps its settings). Audio clips play through
 * their pattern's channel, not the lane's, so each audio pattern is cloned and pointed at the new channel: the copy
 * sounds through its own strip and editing it never edits the original. Pattern (MIDI / Arsenal) clips keep their
 * pattern, as linked copies. Undo removes the copy; redo puts it back with the same ids.
 */
class DuplicateTrackCommand : public ICommand {
public:
    DuplicateTrackCommand(TrackManager& tm, PlaylistLaneID rowLane) : m_tm(tm), m_rowLane(rowLane) {}

    void execute() override {
        if (m_done) return;
        if (m_built) {
            redoBuilt();
            return;
        }
        auto& pl = m_tm.getPlaylistModel();
        const auto* row = pl.getLane(m_rowLane);
        const Track* src = row && row->trackId != 0 ? m_tm.getTrack(row->trackId) : nullptr;
        if (!src || src->laneIds.empty()) return;
        const Track source = *src;

        // The channel: the mixer's own undoable add, then the source's settings and chain.
        const auto* srcCh = m_tm.getChannelById(source.channelId);
        uint32_t channelId = source.channelId; // the master (or a missing channel) is shared, not copied
        if (srcCh && source.channelId != 0) {
            m_channelCmd = std::make_shared<AddChannelCommand>(m_tm, srcCh->getName() + " copy");
            m_channelCmd->execute();
            channelId = m_channelCmd->getCreatedChannelId();
            if (auto* ch = m_tm.getChannelById(channelId)) {
                ch->setVolume(srcCh->getVolume());
                ch->setPan(srcCh->getPan());
                ch->setWidth(srcCh->getWidth());
                ch->setTrimDb(srcCh->getTrimDb());
                ch->setColor(srcCh->getColor());
                ch->setMainOutputId(srcCh->getMainOutputId());
                for (auto send : srcCh->getSends()) {
                    send.sendId = 0; // the copy's own send identity
                    ch->addSend(send);
                }
                auto& pm = PluginManager::getInstance();
                ch->getEffectChain().prepare(pm.getDefaultSampleRate(), pm.getDefaultBlockSize());
                const auto chain = srcCh->getEffectChain().saveState();
                if (!chain.empty()) m_chainCopied = ch->getEffectChain().loadState(chain, pm, nullptr);
            }
        }

        // Audio patterns: one clone each, playing through the new channel.
        auto& patterns = m_tm.getPatternManager();
        std::unordered_map<uint64_t, PatternID> cloned;
        for (const auto& laneId : source.laneIds) {
            const auto* lane = pl.getLane(laneId);
            if (!lane) continue;
            for (const auto& clip : lane->clips) {
                const auto* p = patterns.getPattern(clip.patternId);
                if (!p || !p->isAudio() || cloned.count(clip.patternId.value)) continue;
                const PatternID copy = patterns.clonePattern(clip.patternId);
                if (copy.isValid()) {
                    patterns.setPatternMixerChannel(copy, channelId);
                    cloned[clip.patternId.value] = copy;
                }
            }
        }

        // Lanes under the source track, in its order; clips copied with fresh ids.
        int insertAt = -1;
        for (const auto& laneId : source.laneIds) insertAt = std::max(insertAt, pl.getLaneIndex(laneId));
        std::vector<PlaylistLaneID> newLanes;
        for (const auto& laneId : source.laneIds) {
            const auto* lane = pl.getLane(laneId);
            if (!lane) continue;
            const PlaylistLane srcLane = *lane;
            const PlaylistLaneID id = pl.createLane(srcLane.name + (newLanes.empty() ? " copy" : ""));
            if (auto* dst = pl.getLane(id)) {
                dst->volume = srcLane.volume;
                dst->pan = srcLane.pan;
                dst->muted = srcLane.muted;
                dst->colorRGBA = srcLane.colorRGBA;
                dst->automationCurves = srcLane.automationCurves;
                for (auto& curve : dst->automationCurves)
                    if (curve.mixerChannelId == source.channelId) curve.mixerChannelId = channelId;
            }
            for (auto clip : srcLane.clips) {
                if (const auto it = cloned.find(clip.patternId.value); it != cloned.end()) {
                    clip.patternId = it->second;
                    clip.sourceId = it->second.value;
                }
                clip.id = ClipInstanceID::generate();
                pl.addClip(id, clip);
            }
            pl.moveLaneToIndex(id, static_cast<size_t>(++insertAt));
            newLanes.push_back(id);
        }
        if (newLanes.empty()) return;
        m_trackId = m_tm.createTrack(newLanes.front(), source.name + " copy", channelId);
        for (size_t i = 1; i < newLanes.size(); ++i) m_tm.attachLaneToTrack(m_trackId, newLanes[i]);
        m_newHead = newLanes.front();
        m_built = true;
        m_done = true;
        m_tm.requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
    }

    void undo() override {
        if (!m_done) return;
        auto& pl = m_tm.getPlaylistModel();
        if (const Track* t = m_tm.getTrack(m_trackId)) {
            m_savedTrack = *t;
            m_savedLanes.clear();
            for (const auto& id : t->laneIds)
                if (const auto* l = pl.getLane(id)) m_savedLanes.push_back({*l, pl.getLaneIndex(id)});
            std::sort(m_savedLanes.begin(), m_savedLanes.end(), [](const Saved& a, const Saved& b) { return a.index < b.index; });
        }
        m_tm.removeTrack(m_trackId);
        for (const auto& saved : m_savedLanes) pl.removeLane(saved.lane.id);
        if (m_channelCmd) m_channelCmd->undo();
        m_tm.requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_done = false;
    }

    void redo() override { execute(); }
    std::string getName() const override { return "Duplicate Track"; }
    bool changesProjectState() const override { return m_built; }
    PlaylistLaneID newHeadLane() const { return m_newHead; }
    uint64_t newTrackId() const { return m_trackId; }
    bool chainCopied() const { return m_chainCopied; }

private:
    struct Saved {
        PlaylistLane lane;
        int index = -1;
    };

    void redoBuilt() {
        if (m_channelCmd) m_channelCmd->redo();
        auto& pl = m_tm.getPlaylistModel();
        for (const auto& saved : m_savedLanes) {
            const PlaylistLaneID id = pl.createLaneWithId(saved.lane.id, saved.lane.name);
            if (auto* lane = pl.getLane(id)) {
                const int index = lane->index;
                *lane = saved.lane;
                lane->index = index;
                lane->trackId = 0;
            }
            if (saved.index >= 0) pl.moveLaneToIndex(id, static_cast<size_t>(saved.index));
        }
        m_tm.restoreTrack(m_savedTrack);
        m_tm.requestAudioGraphRebuild(GraphDirtyReason::TrackStructureChanged);
        m_done = true;
    }

    TrackManager& m_tm;
    PlaylistLaneID m_rowLane;
    std::shared_ptr<AddChannelCommand> m_channelCmd;
    uint64_t m_trackId = 0;
    PlaylistLaneID m_newHead;
    Track m_savedTrack;
    std::vector<Saved> m_savedLanes;
    bool m_built = false;
    bool m_done = false;
    bool m_chainCopied = false;
};

} // namespace Audio
} // namespace Aestra
