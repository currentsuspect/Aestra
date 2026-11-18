// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/ICommand.h"
#include "Models/PatternManager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Change a pattern's global swing, or the swing override for one/more selected units.
 *
 * A unit target makes the edit local to notes carrying that unitId. An empty target list edits the
 * pattern-wide fallback swing. The command snapshots override presence as well as values so reset/undo
 * does not accidentally convert an inherited value into an explicit override.
 */
class SetPatternSwingCommand final : public ICommand {
public:
    SetPatternSwingCommand(PatternManager& patterns, PatternID patternId, std::vector<uint64_t> unitIds, float amount)
        : m_patterns(patterns), m_patternId(patternId), m_unitIds(std::move(unitIds)),
          m_amount(std::clamp(amount, 0.0f, 1.0f)) {}

    void markExecuted(float oldGlobal, std::vector<std::pair<uint64_t, std::optional<float>>> oldOverrides) {
        m_oldGlobal = oldGlobal;
        m_oldOverrides = std::move(oldOverrides);
        m_executed = true;
    }

    void execute() override {
        if (m_executed) return;
        auto* pattern = m_patterns.getPattern(m_patternId);
        if (!pattern || !pattern->isMidi()) return;

        if (m_unitIds.empty()) {
            m_oldGlobal = pattern->swing;
            pattern->swing = m_amount;
        } else {
            m_oldOverrides.clear();
            m_oldOverrides.reserve(m_unitIds.size());
            for (const uint64_t unitId : m_unitIds) {
                m_oldOverrides.emplace_back(unitId, pattern->unitSwingOverride(unitId));
                pattern->setUnitSwing(unitId, m_amount);
            }
        }
        m_executed = true;
    }

    void undo() override {
        if (!m_executed) return;
        auto* pattern = m_patterns.getPattern(m_patternId);
        if (!pattern) return;

        if (m_unitIds.empty()) {
            pattern->swing = m_oldGlobal;
        } else {
            for (const auto& [unitId, old] : m_oldOverrides)
                pattern->restoreUnitSwingOverride(unitId, old);
        }
        m_executed = false;
    }

    void redo() override { execute(); }

    std::string getName() const override {
        return m_unitIds.empty() ? "Set Pattern Swing" : "Set Unit Swing";
    }
    size_t getSizeInBytes() const override { return sizeof(*this) + m_unitIds.size() * sizeof(uint64_t); }
    bool changesProjectState() const override { return true; }
    bool isUndoable() const override { return m_executed; }

private:
    PatternManager& m_patterns;
    PatternID m_patternId;
    std::vector<uint64_t> m_unitIds;
    float m_amount{0.0f};
    float m_oldGlobal{0.0f};
    std::vector<std::pair<uint64_t, std::optional<float>>> m_oldOverrides;
    bool m_executed{false};
};

} // namespace Audio
} // namespace Aestra
