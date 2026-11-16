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
}

} // namespace AestraUI
