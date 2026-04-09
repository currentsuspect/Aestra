// ¶¸ 2025 Aestra Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"

#include <functional>
#include <string>

namespace AestraUI {


enum class UIMixerKnobType { Trim, Pan, Width, Send };

/**
 * @brief Compact trim/pan knob for the modern mixer UI.
 *
 * - Trim range: -24..+24 dB (default 0 dB)
 * - Pan range: -1..+1 (displayed as -100..+100, default 0)
 * - Shift drag: fine mode
 * - Double-click: reset to default
 * - Tooltip shown while dragging
 */
class UIMixerKnob : public NUIComponent {
public:
    explicit UIMixerKnob(UIMixerKnobType type);

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setValue(float value);
    float getValue() const { return m_value; }
    void setAccentColor(const NUIColor& color) { m_indicator = color; repaint(); }

    bool isDragging() const { return m_dragging; }

    std::function<void(float)> onValueChanged;

private:
    UIMixerKnobType m_type;
    float m_value{0.0f};
    enum class DragAxis { Undecided, Vertical, Horizontal };

    bool m_dragging{false};
    NUIPoint m_dragStartPos{};
    float m_dragStartValue{0.0f};
    DragAxis m_dragAxis{DragAxis::Undecided};

    // Cached formatted value string (tooltip)
    float m_cachedValue{1234567.0f};
    std::string m_cachedText;

    // Cached theme colors
    NUIColor m_bg;
    NUIColor m_bgHover;
    NUIColor m_ring;
    NUIColor m_ringHover;
    NUIColor m_indicator;
    NUIColor m_text;
    NUIColor m_textSecondary;
    NUIColor m_tooltipBg;
    NUIColor m_tooltipText;

    void cacheThemeColors();
    float clampValue(float value) const;
    float minValue() const;
    float maxValue() const;
    float defaultValue() const;
    void updateCachedText();
    const char* label() const;
    void updateGlobalTooltip() const;
};

} // namespace AestraUI
