// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Core/ArsenalProcessingContext.h"

namespace Aestra {
namespace Audio {

ArsenalProcessingContext::ArsenalProcessingContext(UnitManager* unitManager,
                                                   PatternPlaybackEngine* patternEngine) noexcept
    : m_unitManager(unitManager), m_patternEngine(patternEngine) {}

std::shared_ptr<const AudioArsenalSnapshot> ArsenalProcessingContext::getSnapshot() const {
    if (!m_unitManager) {
        return {};
    }
    return m_unitManager->getAudioSnapshot();
}

bool ArsenalProcessingContext::hasUnits() const {
    auto snapshot = getSnapshot();
    return snapshot && !snapshot->units.empty();
}

ArsenalRouteMode ArsenalProcessingContext::resolveRouteMode(const UnitInfo& unit) const noexcept {
    return routeModeFromRouteId(unit.targetMixerRoute);
}

ArsenalRouteMode ArsenalProcessingContext::resolveRouteMode(const UnitState& unit) const noexcept {
    return routeModeFromRouteId(unit.routeId);
}

bool ArsenalProcessingContext::shouldRenderToTimelineTrack(const UnitState& unit, uint32_t trackIndex) const noexcept {
    return resolveRouteMode(unit) == ArsenalRouteMode::RoutedToTimelineTrack && unit.routeId == static_cast<int>(trackIndex);
}

bool ArsenalProcessingContext::shouldRenderToMasterPreview(const UnitState& unit) const noexcept {
    return resolveRouteMode(unit) == ArsenalRouteMode::PreviewToMaster;
}

bool ArsenalProcessingContext::isFutureDraftMode(const UnitState& unit) const noexcept {
    return unit.getRouteMode() == ArsenalRouteMode::Draft;
}

} // namespace Audio
} // namespace Aestra
