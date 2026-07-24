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

    ArsenalRouteMode resolveRouteMode(const UnitInfo& unit) const noexcept;
    ArsenalRouteMode resolveRouteMode(const UnitState& unit) const noexcept;

    bool shouldRenderToMixerChannel(const UnitState& unit, uint32_t mixerChannelId) const noexcept;
    bool shouldRenderToMaster(const UnitState& unit) const noexcept;

    // Compatibility helpers for legacy Timeline ownership metadata.
    bool shouldRenderToTimelineTrack(const UnitState& unit, uint32_t trackIndex) const noexcept;
    bool shouldRenderToMasterPreview(const UnitState& unit) const noexcept;
    bool isFutureDraftMode(const UnitState& unit) const noexcept;

private:
    UnitManager* m_unitManager{nullptr};
    PatternPlaybackEngine* m_patternEngine{nullptr};
};

} // namespace Audio
} // namespace Aestra
