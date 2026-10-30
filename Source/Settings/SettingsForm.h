// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The one layout every Settings page uses (V82-P12, canvas-2 board 2): small-caps section captions, then rows of
// label | control | hint, a hairline between rows, and the controls lined up in one column. Pages describe their
// rows in order; the form places the controls, paints the captions, labels and lines, and scrolls when a page is
// taller than the dialog.

#include "../AestraUI/Core/NUIComponent.h"

#include <memory>
#include <string>
#include <vector>

namespace Aestra {

class SettingsForm {
public:
    static constexpr float kRowHeight = 34.0f;
    static constexpr float kLabelWidth = 170.0f;
    static constexpr float kControlHeight = 26.0f;
    static constexpr float kControlWidth = 220.0f;
    static constexpr float kSidePadding = 22.0f;

    void clear();
    void section(std::string caption);
    /// A row. control may be null (a label-only row); hint is drawn muted to the right of the control.
    void row(std::string label, std::shared_ptr<AestraUI::NUIComponent> control, std::string hint = {},
             float controlWidth = kControlWidth);
    /// A full-width block (a status line, a list) of the given height, under the current section. component may be
    /// null for a block the page draws itself; boundsOf() gives its rect after layout().
    size_t block(std::shared_ptr<AestraUI::NUIComponent> component, float height);
    /// The laid-out rect of the item block() or row() returned, scroll applied.
    AestraUI::NUIRect boundsOf(size_t item) const {
        return item < m_items.size() ? m_items[item].bounds : AestraUI::NUIRect();
    }
    /// Index the next row() or block() will get.
    size_t nextIndex() const { return m_items.size(); }

    /// Places every control inside area (offset by the scroll). Controls of rows outside it are hidden.
    void layout(const AestraUI::NUIRect& area);
    /// Captions, labels, hints and lines. Call before rendering the page's children.
    void paint(AestraUI::NUIRenderer& renderer) const;
    /// Wheel over the area scrolls a page taller than it. Returns true when it scrolled.
    bool onMouseWheel(const AestraUI::NUIMouseEvent& event);

    /// Updates the hint of the row holding control (a latency readout, "was 512").
    void setHint(const AestraUI::NUIComponent* control, std::string hint);
    const AestraUI::NUIRect& area() const { return m_area; }
    float contentHeight() const;

private:
    enum class Kind { Section, Row, Block };
    struct Item {
        Kind kind = Kind::Row;
        std::string text; // caption or label
        std::string hint;
        std::shared_ptr<AestraUI::NUIComponent> control;
        float controlWidth = kControlWidth;
        float height = kRowHeight;
        AestraUI::NUIRect bounds; // laid out, scroll applied
        AestraUI::NUIRect controlBounds;
    };
    std::vector<Item> m_items;
    AestraUI::NUIRect m_area;
    float m_scroll = 0.0f;
};

} // namespace Aestra
