// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include <functional>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * @brief The strip's insert chain by name (V81-M1, FD-28 point 3): "Aestra EQ", "Saturator" ...
 *
 * Bypassed inserts stay listed but dim with a strike, so the chain reads as it is. Rows that do not fit
 * collapse into "+N more". An empty chain shows a single quiet "+ insert" invitation. A click anywhere
 * calls @ref onClicked; the owner decides whether that adds or opens the inspector.
 */
class UIMixerInsertList : public NUIComponent {
public:
    struct Row {
        std::string name;
        bool bypassed{false};
    };

    UIMixerInsertList() = default;

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setRows(std::vector<Row> rows);
    /// Compressor gain reduction and similar live status, drawn on the right of the first row.
    void setStatusText(std::string text);

    /// Height needed to show @p rowCount rows (an empty chain needs one row for the invitation).
    static float heightForRows(int rowCount);
    static constexpr int kMaxRows = 8; // Wide shows every name; Normal asks for three
    static constexpr float kRowH = 15.0f;
    static constexpr float kPad = 3.0f;

    const std::vector<Row>& rows() const { return m_rows; }
    /// How many insert rows fit in the current bounds (the rest fold into "+N more").
    int visibleRowCapacity() const;

    std::function<void()> onClicked;
    std::function<void()> onInvalidateRequested;

private:
    std::vector<Row> m_rows;
    std::string m_status;
    bool m_hovered{false};
    bool m_pressed{false};
};

} // namespace AestraUI
