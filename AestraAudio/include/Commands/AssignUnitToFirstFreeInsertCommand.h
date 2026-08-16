// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/AssignUnitToFirstFreeMixerChannelCommand.h"

namespace Aestra {
namespace Audio {

// Compatibility aliases for callers that predate the mixer-channel terminology cleanup.
// New code must use AssignUnitToFirstFreeMixerChannelCommand / assignUnitToFirstFreeMixerChannel.
using AssignUnitToFirstFreeInsertCommand = AssignUnitToFirstFreeMixerChannelCommand;

inline bool assignUnitToFirstFreeInsert(TrackManager& manager, UnitID unitId, const std::string& destinationName,
                                        uint32_t color) {
    return assignUnitToFirstFreeMixerChannel(manager, unitId, destinationName, color);
}

} // namespace Audio
} // namespace Aestra
