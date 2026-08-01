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
 * object. The decode happens before the command is built, so an unreadable or
 * unsupported file is reported as a precise error and nothing is created.
 *
 * Path ownership follows the UI: the source references the file where it lies
 * (path is SourceManager's dedupe key) and is never copied into the project.
 *
 * execute() creates the audio pattern and places the clip; undo() removes the
 * clip and detaches the pattern, so redo reuses it rather than re-decoding.
 */
class ImportAudioClipCommand final : public ICommand {
public:
    ImportAudioClipCommand(TrackManager& manager, PlaylistLaneID laneId, ClipSourceID sourceId, std::string displayName,
                           double startBeat, double durationSeconds, double durationBeats, uint64_t sourceFrames)
        : m_manager(manager), m_laneId(laneId), m_sourceId(sourceId), m_displayName(std::move(displayName)),
          m_startBeat(startBeat), m_durationSeconds(durationSeconds), m_durationBeats(durationBeats),
          m_sourceFrames(sourceFrames) {}

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
    TrackManager& m_manager;
    PlaylistLaneID m_laneId;
    ClipSourceID m_sourceId;
    std::string m_displayName;
    double m_startBeat{0.0};
    double m_durationSeconds{0.0};
    double m_durationBeats{0.0};
    uint64_t m_sourceFrames{0};

    ClipInstanceID m_clipId;
    PatternID m_patternId;
    std::unique_ptr<PatternSource> m_detachedPattern;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
