// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PlaylistModel.h"

namespace Aestra {
namespace Audio {

/** Undoable replacement of one Playlist clip instance's playback properties. */
class SetClipEditsCommand final : public ICommand {
public:
    SetClipEditsCommand(PlaylistModel& model, ClipInstanceID clipId, ClipEdits newEdits)
        : m_model(model), m_clipId(clipId), m_newEdits(newEdits) {}

    SetClipEditsCommand(PlaylistModel& model, ClipInstanceID clipId, ClipEdits originalEdits, ClipEdits newEdits,
                        bool alreadyExecuted)
        : m_model(model), m_clipId(clipId), m_originalEdits(originalEdits), m_newEdits(newEdits), m_hasOriginal(true),
          m_executed(alreadyExecuted) {}

    void execute() override {
        if (m_executed)
            return;
        if (!m_hasOriginal) {
            const auto* clip = m_model.getClip(m_clipId);
            if (!clip)
                return;
            m_originalEdits = clip->edits;
            m_hasOriginal = true;
        }
        m_executed = m_model.setClipEdits(m_clipId, m_newEdits);
    }

    void undo() override {
        if (!m_executed || !m_hasOriginal)
            return;
        if (m_model.setClipEdits(m_clipId, m_originalEdits)) {
            m_executed = false;
        }
    }

    void redo() override {
        if (m_executed)
            return;
        m_executed = m_model.setClipEdits(m_clipId, m_newEdits);
    }

    std::string getName() const override { return "Edit Audio Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    PlaylistModel& m_model;
    ClipInstanceID m_clipId;
    ClipEdits m_originalEdits;
    ClipEdits m_newEdits;
    bool m_hasOriginal{false};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
