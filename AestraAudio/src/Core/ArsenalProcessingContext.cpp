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

} // namespace Audio
} // namespace Aestra
