// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUIDoubleClick.h"
#include "NUITypes.h"

#include <functional>
#include <string>

namespace AestraUI {

/**
 * @brief A value you drag along a bar (V81-K1, FD-28): the kit's replacement for a knob where the value must
 *        read at a glance. Two looks:
 *
 *  - Row: "Trim ............ +0.0 dB", a label, a bar and the value text (the inspector).
 *  - Bar: just the bar with a marker; the owner draws the value under it (the strip's pan).
 *
 * Dragging is relative: pulling across the whole bar sweeps the whole range, so a stray click never jumps the
 * value. Shift drags finely. A double click resets to the default. A bipolar bar fills outward from its
 * centre tick.
 */
class UIParamBar : public NUIComponent {
public:
    enum class Look { Row, Bar };

    explicit UIParamBar(Look look = Look::Row) : m_look(look) {}

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onMouseLeave() override;

    void setRange(float minValue, float maxValue, float defaultValue);
    void setBipolar(bool bipolar);
    void setLabel(std::string label);
    void setFormatter(std::function<std::string(float)> formatter);
    /// Set from the model (ignored while the user is dragging).
    void setValue(float value);
    float value() const { return m_value; }
    bool isDragging() const { return m_dragging; }

    /// Fired with the new value on every drag step and on reset.
    std::function<void(float)> onValueChanged;
    /// Fired once when a drag ends, so the owner can commit one undo step.
    std::function<void(float)> onValueCommitted;

private:
    float clampValue(float v) const;
    void applyUserValue(float v);
    NUIRect barRect() const;

    Look m_look;
    float m_min{0.0f};
    float m_max{1.0f};
    float m_default{0.0f};
    float m_value{0.0f};
    bool m_bipolar{false};
    std::string m_label;
    std::function<std::string(float)> m_formatter;

    bool m_hovered{false};
    bool m_dragging{false};
    float m_dragStartX{0.0f};
    float m_dragStartValue{0.0f};
    NUIDoubleClickTracker m_clickTracker;
};

} // namespace AestraUI
