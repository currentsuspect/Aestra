// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include <string>

namespace AestraUI {

/**
 * @brief A named, coloured destination ("→ Drum Bus"), the kit's answer to a channel number (V81-K1, FD-28).
 *
 * Draws a pill: the destination's colour as a dot, then its name. It only displays; the owner decides what a
 * click does through @ref onClicked, so the same chip serves the mixer strip and the Arsenal row.
 */
class UIOutputChip : public NUIComponent {
public:
    UIOutputChip() = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setLabel(std::string label);
    void setColor(const NUIColor& color);
    /// A dimmed chip is a route that exists but is not audible (muted send, bus-only bypass).
    void setDimmed(bool dimmed);

    const std::string& label() const { return m_label; }
    const NUIColor& color() const { return m_color; }
    bool isDimmed() const { return m_dimmed; }

    std::function<void()> onClicked;

private:
    std::string m_label;
    NUIColor m_color{0.5f, 0.5f, 0.5f, 1.0f};
    bool m_dimmed{false};
    bool m_hovered{false};
    bool m_pressed{false};
};

} // namespace AestraUI
