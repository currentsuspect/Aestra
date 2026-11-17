// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/MusicalRole.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"

#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief Set what a sound is in the song, as the user's own choice (undoable). An unknown role clears the choice: the
 * role goes back to whatever the name says. The user's choice outranks every name (MusicalRole.h).
 */
class SetUnitRoleCommand : public ICommand {
public:
    SetUnitRoleCommand(UnitManager& units, UnitID unit, const MusicalRole& role) : m_units(units), m_unit(unit), m_role(role) {}

    void execute() override {
        if (m_executed || !m_units.getUnit(m_unit)) return;
        m_before = m_units.unitRole(m_unit);
        m_units.setUnitRole(m_unit, m_role);
        m_executed = true;
    }
    void undo() override {
        if (!m_executed) return;
        if (auto* u = m_units.getUnit(m_unit)) u->role = m_before; // exactly what it was, source included
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Set Category"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    UnitManager& m_units;
    UnitID m_unit;
    MusicalRole m_role;
    MusicalRole m_before;
    bool m_executed = false;
};

/** @brief The same for a track. */
class SetTrackRoleCommand : public ICommand {
public:
    SetTrackRoleCommand(TrackManager& tracks, uint64_t trackId, const MusicalRole& role)
        : m_tracks(tracks), m_track(trackId), m_role(role) {}

    void execute() override {
        auto* t = m_tracks.getTrack(m_track);
        if (m_executed || !t) return;
        m_before = t->role;
        t->role = m_role.known() ? userRole(m_role.category, m_role.sub) : roleFromName(t->name);
        m_executed = true;
    }
    void undo() override {
        if (!m_executed) return;
        if (auto* t = m_tracks.getTrack(m_track)) t->role = m_before;
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return "Set Category"; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_tracks;
    uint64_t m_track;
    MusicalRole m_role;
    MusicalRole m_before;
    bool m_executed = false;
};

} // namespace Audio
} // namespace Aestra
