// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Markers (song sections and cues): add, delete, rename as undoable steps.
//
// Identity rule: every execute()/undo() pair restores the SAME marker id. A fresh id after undo would leave the UI's
// selection, and anything holding a marker id, pointing at a marker that no longer exists — the same trap
// CreateTrackWithLaneCommand documents for lanes.

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"

#include <string>

namespace Aestra {
namespace Audio {

class AddMarkerCommand : public ICommand {
public:
    AddMarkerCommand(TrackManager& tracks, double beat, std::string name = {})
        : m_tracks(tracks), m_beat(beat), m_name(std::move(name)) {}

    void execute() override {
        if (m_executed) return;
        m_id = m_tracks.markers().add(m_beat, m_name, m_id);
        m_executed = m_id != 0;
    }

    void undo() override {
        if (!m_executed) return;
        m_tracks.markers().remove(m_id);
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override { return m_name.empty() ? "Add Marker" : "Add Marker \"" + m_name + "\""; }
    bool isUndoable() const override { return m_id != 0; }

    uint64_t markerId() const { return m_id; }

private:
    TrackManager& m_tracks;
    double m_beat;
    std::string m_name;
    uint64_t m_id{0};
    bool m_executed{false};
};

class DeleteMarkerCommand : public ICommand {
public:
    DeleteMarkerCommand(TrackManager& tracks, uint64_t markerId) : m_tracks(tracks), m_markerId(markerId) {}

    void execute() override {
        if (m_executed) return;
        const Marker* marker = m_tracks.markers().find(m_markerId);
        if (marker == nullptr) return;
        m_removed = *marker; // the whole marker, so undo puts it back exactly where it was
        m_executed = m_tracks.markers().remove(m_markerId);
    }

    void undo() override {
        if (!m_executed) return;
        m_tracks.markers().add(m_removed.beat, m_removed.name, m_removed.id);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        m_executed = m_tracks.markers().remove(m_markerId);
    }

    std::string getName() const override { return "Delete Marker"; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_tracks;
    uint64_t m_markerId;
    Marker m_removed;
    bool m_executed{false};
};

class MoveMarkerCommand : public ICommand {
public:
    MoveMarkerCommand(TrackManager& tracks, uint64_t markerId, double newBeat)
        : m_tracks(tracks), m_markerId(markerId), m_newBeat(newBeat) {}

    void execute() override {
        if (m_executed) return;
        const Marker* marker = m_tracks.markers().find(m_markerId);
        if (marker == nullptr) return;
        m_oldBeat = marker->beat;
        m_executed = m_tracks.markers().moveTo(m_markerId, m_newBeat);
    }

    void undo() override {
        if (!m_executed) return;
        m_tracks.markers().moveTo(m_markerId, m_oldBeat);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        m_executed = m_tracks.markers().moveTo(m_markerId, m_newBeat);
    }

    std::string getName() const override { return "Move Marker"; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_tracks;
    uint64_t m_markerId;
    double m_newBeat;
    double m_oldBeat{0.0};
    bool m_executed{false};
};

class RenameMarkerCommand : public ICommand {
public:
    RenameMarkerCommand(TrackManager& tracks, uint64_t markerId, std::string newName)
        : m_tracks(tracks), m_markerId(markerId), m_newName(std::move(newName)) {}

    void execute() override {
        if (m_executed) return;
        const Marker* marker = m_tracks.markers().find(m_markerId);
        if (marker == nullptr || m_newName.empty()) return;
        m_oldName = marker->name;
        m_executed = m_tracks.markers().rename(m_markerId, m_newName);
    }

    void undo() override {
        if (!m_executed) return;
        m_tracks.markers().rename(m_markerId, m_oldName);
        m_executed = false;
    }

    void redo() override {
        if (m_executed) return;
        m_executed = m_tracks.markers().rename(m_markerId, m_newName);
    }

    std::string getName() const override { return "Rename Marker"; }
    bool isUndoable() const override { return m_executed; }

private:
    TrackManager& m_tracks;
    uint64_t m_markerId;
    std::string m_newName;
    std::string m_oldName;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
