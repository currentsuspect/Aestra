// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/TrackManager.h"
#include "Models/UnitManager.h"
#include "Music/ProjectHarmony.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief What the transport bar's key pill does (V83-KEY3, FD-98). Pure functions on ProjectHarmony, so the rules are
 * tested without a window; the pill only chooses which one to call.
 */

/** @brief One semitone (or more) of global transpose, within the allowed range. A keyless song still moves. */
inline ProjectHarmony steppedTranspose(ProjectHarmony h, int semitones) {
    h.transposeSemitones = std::clamp(h.transposeSemitones + semitones, ProjectHarmony::kMinTranspose,
                                      ProjectHarmony::kMaxTranspose);
    return h;
}

/** @brief "Move them there": the key-following parts turn the shortest way (never more than 6 semitones). */
inline ProjectHarmony movedToKey(ProjectHarmony h, int tonic, bool minor) { return withSoundingKey(h, tonic, minor); }

/**
 * @brief "Just call it that": only the name of the key changes. The notes as written and the transpose stay exactly as
 * they were, so nothing sounds different; the written key is whatever makes written + transpose equal the named key.
 */
inline ProjectHarmony calledKey(ProjectHarmony h, int tonic, bool minor) {
    tonic = ((tonic % 12) + 12) % 12;
    h.writtenTonic = ((tonic - h.transposeSemitones) % 12 + 12) % 12;
    h.minor = minor;
    return h;
}

/** @brief The units that follow the key, and so move when the song does. Their names are what the board tells the producer. */
inline std::vector<UnitID> unitsFollowingKey(const UnitManager& units) {
    std::vector<UnitID> out;
    for (const UnitID id : units.getAllUnitIDs())
        if (const UnitInfo* unit = units.getUnit(id))
            if (unit->followsKey())
                out.push_back(id);
    return out;
}

/** @brief Change the project key and transpose as ONE undoable step; undo puts them back exactly. */
class SetProjectKeyCommand final : public ICommand {
public:
    SetProjectKeyCommand(TrackManager& manager, ProjectHarmony after, std::string name = "Set Key")
        : m_manager(manager), m_after(after), m_name(std::move(name)) {}

    void execute() override {
        if (m_executed)
            return;
        m_before = m_manager.getUnitManager().harmony();
        m_manager.getUnitManager().setHarmony(m_after);
        m_executed = true;
    }
    void undo() override {
        if (!m_executed)
            return;
        m_manager.getUnitManager().setHarmony(m_before);
        m_executed = false;
    }
    void redo() override { execute(); }

    std::string getName() const override { return m_name; }
    size_t getSizeInBytes() const override { return sizeof(*this); }
    bool changesProjectState() const override { return true; }

private:
    TrackManager& m_manager;
    ProjectHarmony m_before;
    ProjectHarmony m_after;
    std::string m_name;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
