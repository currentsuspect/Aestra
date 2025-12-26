// ¶¸ 2025 Nomad Studios ƒ?" All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"

#include <functional>
#include <string>

namespace NomadUI {

enum class UIMixerKnobType { Trim, Pan, Send };

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

    bool isDragging() const { return m_dragging; }

    std::function<void(float)> onValueChanged;

private:
    UIMixerKnobType m_type;
    float m_value{0.0f};

    bool m_dragging{false};
    NUIPoint m_dragStartPos{};
    float m_dragStartValue{0.0f};

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
    void renderTooltip(NUIRenderer& renderer, const NUIRect& knobBounds) const;
};

} // namespace NomadUI

