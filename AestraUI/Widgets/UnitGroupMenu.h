// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The unit row's "Move to Synth / Move to Drums" item (#1400, FD-96): the first group is a guess from the sample's
// name, and the user corrects it here, one undo step. Audio units have nothing to move between.

#include "Commands/SetUnitGroupCommand.h"
#include "NUIContextMenu.h"

#include <functional>
#include <memory>

namespace AestraUI {

inline void addUnitGroupMoveItem(NUIContextMenu& menu, Aestra::Audio::TrackManager* tracks, Aestra::Audio::UnitID unitId,
                                 std::function<void(Aestra::Audio::UnitID)> onMoved) {
    using namespace Aestra::Audio;
    const auto* unit = tracks ? tracks->getUnitManager().getUnit(unitId) : nullptr;
    if (!unit || unit->type == UnitType::Audio) return;
    const UnitGroup to = unit->group == UnitGroup::Synth ? UnitGroup::Drums : UnitGroup::Synth;
    menu.addItem(to == UnitGroup::Synth ? "Move to Synth" : "Move to Drums", [tracks, unitId, to, onMoved]() {
        tracks->getCommandHistory().pushAndExecute(std::make_shared<SetUnitGroupCommand>(*tracks, unitId, to));
        if (onMoved) onMoved(unitId);
    });
    // Every sound at once, from its name: for racks from before the first guess, where everything sat under Drums.
    menu.addItem("Regroup all sounds by name", [tracks, onMoved]() {
        auto regroup = std::make_shared<RegroupUnitsByNameCommand>(*tracks);
        tracks->getCommandHistory().pushAndExecute(regroup);
        if (onMoved) onMoved(0);
    });
}

/// A row dropped under another group's header joins that group (Synth / Drums only; audio stays audio), one undo step.
inline bool moveUnitToDroppedGroup(Aestra::Audio::TrackManager& tracks, Aestra::Audio::UnitID unitId, Aestra::Audio::UnitGroup group) {
    using namespace Aestra::Audio;
    const auto* unit = tracks.getUnitManager().getUnit(unitId);
    if (!unit || unit->type == UnitType::Audio || unit->group == group) return false;
    if (group != UnitGroup::Synth && group != UnitGroup::Drums) return false;
    tracks.getCommandHistory().pushAndExecute(std::make_shared<SetUnitGroupCommand>(tracks, unitId, group));
    return true;
}

} // namespace AestraUI
