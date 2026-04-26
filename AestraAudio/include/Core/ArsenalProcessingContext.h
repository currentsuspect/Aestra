// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Models/UnitManager.h"

#include <memory>

namespace Aestra {
namespace Audio {

class PatternPlaybackEngine;

/**
 * @brief Thin wrapper for current Arsenal processing access.
 *
 * This class intentionally does not introduce a separate DSP engine or graph.
 * It only centralizes snapshot access and explicit route-mode interpretation
 * for the existing behavior-preserving render path.
 */
class ArsenalProcessingContext {
public:
    ArsenalProcessingContext() = default;
    ArsenalProcessingContext(UnitManager* unitManager, PatternPlaybackEngine* patternEngine = nullptr) noexcept;

    void setUnitManager(UnitManager* unitManager) noexcept { m_unitManager = unitManager; }
    void setPatternPlaybackEngine(PatternPlaybackEngine* patternEngine) noexcept { m_patternEngine = patternEngine; }

    UnitManager* unitManager() const noexcept { return m_unitManager; }
    PatternPlaybackEngine* patternPlaybackEngine() const noexcept { return m_patternEngine; }

    std::shared_ptr<const AudioArsenalSnapshot> getSnapshot() const;
    bool hasUnits() const;

    static constexpr ArsenalRouteMode routeModeFromRouteId(int routeId) noexcept {
        return arsenalRouteModeFromRouteId(routeId);
    }

    static constexpr bool routesToTimelineTrack(int routeId) noexcept {
        return routeModeFromRouteId(routeId) == ArsenalRouteMode::RoutedToTimelineTrack;
    }

    static constexpr bool routesToMasterPreview(int routeId) noexcept {
        return routeModeFromRouteId(routeId) == ArsenalRouteMode::PreviewToMaster;
    }

private:
    UnitManager* m_unitManager{nullptr};
    PatternPlaybackEngine* m_patternEngine{nullptr};
};

} // namespace Audio
} // namespace Aestra
