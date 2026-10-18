// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/SetSamplerStateCommand.h"
#include "Models/TrackManager.h"

#include <functional>
#include <memory>

namespace Aestra {

/**
 * @brief Makes the Sample Editor's controls undoable (a v0.8.2 candidate the founder asked for on 2026-10-13).
 *
 * The editor changes the sampler live while a control is dragged and says when the gesture is over. This keeps the
 * state the sampler had when the editor last synced to it (`begin`); at the end of a gesture (`commit`) it saves the
 * unit's plugin state as before and, when something changed, records one undo step from that baseline to now.
 */
class SamplerEditHistory {
public:
    /// The editor opened on (or re-synced to) @p unit: its sampler's current state is the baseline for the next step.
    void begin(Audio::TrackManager& tm, Audio::UnitID unit) {
        m_unit = unit;
        m_has = false;
        if (auto sampler = sampler_(tm, unit)) { m_baseline = Audio::SamplerEditState::capture(*sampler); m_has = true; }
    }

    /// A gesture ended. Returns true when an undo step was recorded.
    bool commit(Audio::TrackManager& tm, std::function<void()> refreshEditor) {
        if (m_unit == 0) return false;
        tm.getUnitManager().captureUnitPluginState(m_unit);
        tm.markModified();
        auto sampler = sampler_(tm, m_unit);
        if (!sampler || !m_has) return false;
        const auto now = Audio::SamplerEditState::capture(*sampler);
        if (now == m_baseline) return false;
        const bool recorded = tm.getCommandHistory().pushExecuted(
            std::make_shared<Audio::SetSamplerStateCommand>(tm.getUnitManager(), m_unit, m_baseline, now, std::move(refreshEditor)));
        m_baseline = now;
        return recorded;
    }

private:
    static std::shared_ptr<Audio::Plugins::SamplerPlugin> sampler_(Audio::TrackManager& tm, Audio::UnitID unit) {
        return std::dynamic_pointer_cast<Audio::Plugins::SamplerPlugin>(tm.getUnitManager().getUnitPlugin(unit));
    }
    Audio::UnitID m_unit{0};
    bool m_has{false};
    Audio::SamplerEditState m_baseline;
};

} // namespace Aestra
