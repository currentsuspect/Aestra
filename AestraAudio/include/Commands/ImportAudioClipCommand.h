// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/ClipInstance.h"
#include "Models/ClipSource.h"
#include "Models/PatternSource.h"

#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class TrackManager;

/**
 * @brief Import a decoded audio file as a playable clip on a lane.
 *
 * Mirrors what the drag-and-drop import does, and for the same reason: a clip
 * that names a file but carries no pattern is a silent, structurally invalid
 * object.
 *
 * The command owns every project mutation. Its factory only decodes and
 * validates, then hands the finished buffer here — so an unreadable or
 * unsupported file is refused before anything is constructed, and nothing
 * touches the project until execute() runs inside the history. Registering the
 * source in the factory instead would mutate state no undo entry covers, which
 * breaks down as soon as batches, previews or dry runs enter the picture.
 *
 * Path ownership follows the UI: the source references the file where it lies
 * (path is SourceManager's dedupe key) and is never copied into the project.
 *
 * Rollback is asymmetric on purpose. If the first execute() fails partway, a
 * source this command introduced is taken back out, because nothing else has
 * seen it. A normal undo keeps both the source and the detached pattern, so
 * redo is instant and never decodes twice.
 */
class ImportAudioClipCommand final : public ICommand {
public:
    ImportAudioClipCommand(TrackManager& manager, PlaylistLaneID laneId, std::string filePath, std::string displayName,
                           std::shared_ptr<AudioBufferData> buffer, double startBeat, double durationSeconds,
                           double durationBeats)
        : m_manager(manager), m_laneId(laneId), m_filePath(std::move(filePath)),
          m_displayName(std::move(displayName)), m_buffer(std::move(buffer)), m_startBeat(startBeat),
          m_durationSeconds(durationSeconds), m_durationBeats(durationBeats) {}

    void execute() override;
    void undo() override;
    void redo() override { execute(); }

    std::string getName() const override { return "Import Audio Clip"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

    /** @brief Id of the placed clip, for reporting back to the caller. */
    ClipInstanceID getClipId() const { return m_clipId; }

private:
    /** @brief Unwind a partial first execute(): no clip, no pattern, no new source. */
    void rollbackFailedFirstExecute();

    TrackManager& m_manager;
    PlaylistLaneID m_laneId;
    std::string m_filePath;
    std::string m_displayName;
    std::shared_ptr<AudioBufferData> m_buffer;
    double m_startBeat{0.0};
    double m_durationSeconds{0.0};
    double m_durationBeats{0.0};

    ClipInstanceID m_clipId;
    ClipSourceID m_sourceId;
    PatternID m_patternId;
    std::unique_ptr<PatternSource> m_detachedPattern;
    /** True when this command is what put m_sourceId into the project. */
    bool m_createdSource{false};
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
