// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/PatternSource.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Base for destructive clip operations that commit a new audio file.
 *
 * Reverse, commit-edits and (later) consolidate all share one shape: render
 * the clip's current audio through some transform, write it as a new source,
 * and repoint the clip at the result. Undo puts the original pattern back and
 * detaches — rather than destroys — the rendered one, so redo is instant and
 * never re-renders. The rendered file is deliberately left on disk: a detached
 * pattern still references it, and deleting it would break redo.
 */
class RenderAudioClipCommand : public ICommand {
public:
    RenderAudioClipCommand(TrackManager& manager, ClipInstanceID clipId) : m_manager(manager), m_clipId(clipId) {}

    void execute() override;
    void undo() override;
    void redo() override { execute(); }

    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

    /** @brief False when execute() could not render (nothing was changed). */
    bool isUndoable() const override { return m_executed; }

protected:
    /**
     * @brief Produce the replacement audio for this clip.
     * @param clip The clip being rendered, for edit values.
     * @return Null to abort the command without touching project state.
     */
    virtual std::shared_ptr<AudioBufferData> renderBuffer(const ClipInstance& clip) = 0;

    /** @brief Suffix appended to the new pattern/file name, e.g. "Reversed". */
    virtual std::string renderSuffix() const = 0;

    /**
     * @brief Hook for edits that become redundant once baked into the audio.
     * Default keeps the clip's edits untouched.
     */
    virtual void adjustEditsAfterRender(ClipEdits& /*edits*/) const {}

    TrackManager& m_manager;
    ClipInstanceID m_clipId;

private:
    PatternID m_originalPatternId;
    PatternID m_renderedPatternId;
    std::unique_ptr<PatternSource> m_detachedPattern;
    ClipEdits m_originalEdits;
    double m_originalSourceOffset{0.0};
    double m_originalSourceOffsetSeconds{0.0};
    bool m_editsChanged{false};
    bool m_offsetsCleared{false};
    bool m_executed{false};
};

/** @brief Replace a clip's audio with a reversed copy of itself. */
class ReverseAudioClipCommand final : public RenderAudioClipCommand {
public:
    using RenderAudioClipCommand::RenderAudioClipCommand;

    std::string getName() const override { return "Reverse Audio Clip"; }

protected:
    std::shared_ptr<AudioBufferData> renderBuffer(const ClipInstance& clip) override;
    std::string renderSuffix() const override { return "Reversed"; }
};

/**
 * @brief Bake a clip's own gain and fades into a new flat source.
 *
 * The clip sounds the same afterwards but its edits return to unity, so the
 * result can be dragged elsewhere, or handed to a plugin, without carrying
 * invisible state. This is the "commit" half of nondestructive editing.
 *
 * Deliberately NOT called "bounce in place": this renders only clip-local
 * edits. It does not run the track's plugins, automation, sends, routing or
 * PDC. Reserve "Bounce in Place" for the later graph-level operation that
 * does, so the two never collapse into one ambiguous name.
 */
class CommitAudioClipEditsCommand final : public RenderAudioClipCommand {
public:
    using RenderAudioClipCommand::RenderAudioClipCommand;

    std::string getName() const override { return "Commit Clip Edits"; }

protected:
    std::shared_ptr<AudioBufferData> renderBuffer(const ClipInstance& clip) override;
    std::string renderSuffix() const override { return "Committed"; }
    void adjustEditsAfterRender(ClipEdits& edits) const override;
};

} // namespace Audio
} // namespace Aestra
