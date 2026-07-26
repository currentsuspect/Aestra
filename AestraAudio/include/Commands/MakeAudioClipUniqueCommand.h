// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

namespace Aestra {
namespace Audio {

/**
 * Give one Playlist audio clip its own pattern identity while retaining the
 * same underlying audio buffer and current source route.
 */
class MakeAudioClipUniqueCommand final : public ICommand {
public:
    MakeAudioClipUniqueCommand(TrackManager& manager, ClipInstanceID clipId) : m_manager(manager), m_clipId(clipId) {}

    void execute() override {
        if (m_executed)
            return;
        const auto* clip = m_manager.getPlaylistModel().getClip(m_clipId);
        if (!clip || !clip->patternId.isValid())
            return;
        const auto* sourcePattern = m_manager.getPatternManager().getPattern(clip->patternId);
        if (!sourcePattern || !sourcePattern->isAudio())
            return;
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
        m_manager.markModified();
        m_executed = true;
    }

    void undo() override {
        if (!m_executed)
            return;
        if (m_manager.getPlaylistModel().setClipPattern(m_clipId, m_originalPatternId)) {
            m_detachedPattern = m_manager.getPatternManager().detachPattern(m_uniquePatternId);
            m_manager.markModified();
            m_executed = false;
        }
    }

    void redo() override { execute(); }

    std::string getName() const override { return "Make Audio Clip Unique"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

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
