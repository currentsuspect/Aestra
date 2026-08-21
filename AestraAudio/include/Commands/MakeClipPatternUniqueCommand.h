// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Give one Playlist clip its own pattern identity, preserving contents.
 *
 * Works for MIDI and audio patterns alike: the pattern data is cloned under a
 * new id ("... copy" naming follows PatternManager::clonePattern) and the clip
 * is repointed at the clone. Other clips keep referencing the original.
 */
class MakeClipPatternUniqueCommand final : public ICommand {
public:
    MakeClipPatternUniqueCommand(TrackManager& manager, ClipInstanceID clipId)
        : m_manager(manager), m_clipId(clipId) {}

    void execute() override {
        if (m_executed)
            return;
        const auto* clip = m_manager.getPlaylistModel().getClip(m_clipId);
        if (!clip || !clip->patternId.isValid())
            return;
        const auto* sourcePattern = m_manager.getPatternManager().getPattern(clip->patternId);
        if (!sourcePattern)
            return;

        // Capture before cloning: the source pointer can be invalidated by the
        // clone's store mutation.
        const uint32_t sourceMixerChannelId = sourcePattern->getMixerChannelId();

        if (!m_originalPatternId.isValid()) {
            m_originalPatternId = clip->patternId;
        }
        if (m_detachedPattern) {
            if (!m_manager.getPatternManager().reinsertPattern(m_detachedPattern))
                return;
        } else {
            m_uniquePatternId = m_manager.getPatternManager().clonePattern(m_originalPatternId);
            if (!m_uniquePatternId.isValid())
                return;
        }
        if (!m_manager.getPlaylistModel().setClipPattern(m_clipId, m_uniquePatternId)) {
            m_detachedPattern = m_manager.getPatternManager().detachPattern(m_uniquePatternId);
            return;
        }

        // Preserve per-pattern mixer routing when the source had one.
        if (sourceMixerChannelId != 0) {
            m_manager.getPatternManager().setPatternMixerChannel(m_uniquePatternId, sourceMixerChannelId);
        }
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
        // Live playback: scheduled instances still hold the old PatternID —
        // refresh them so the clip plays its own (unique) pattern immediately.
        m_manager.refreshTimelinePatternInstances();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        auto* clip = m_manager.getPlaylistModel().getClip(m_clipId);
        if (clip && clip->patternId == m_uniquePatternId) {
            if (!m_manager.getPlaylistModel().setClipPattern(m_clipId, m_originalPatternId)) {
                return;
            }
            m_detachedPattern = m_manager.getPatternManager().detachPattern(m_uniquePatternId);
        }
        m_manager.requestAudioGraphRebuild(GraphDirtyReason::TimelineChanged);
        m_manager.refreshTimelinePatternInstances();
        m_executed = false;
    }

    void redo() override {
        // execute() owns the executed-state transition; a failed redo (clip
        // gone, pattern missing) must not mark the command executed.
        execute();
    }

    std::string getName() const override { return "Make Pattern Unique"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    /** @brief The cloned pattern id (valid after execute). */
    PatternID uniquePatternId() const { return m_uniquePatternId; }

private:
    TrackManager& m_manager;
    ClipInstanceID m_clipId;
    PatternID m_originalPatternId;
    PatternID m_uniquePatternId;
    std::unique_ptr<PatternSource> m_detachedPattern;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
