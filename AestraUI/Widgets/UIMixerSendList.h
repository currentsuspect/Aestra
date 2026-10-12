// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include <functional>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * @brief The strip's sends by destination and level (V81-M1, FD-28 point 3): "■ Reverb  −18".
 *
 * A coloured square names the destination, the level reads on the right, and an amber diamond marks a
 * sidechain send (which carries no audio, so it has no level). Muted sends are dimmed. Rows that do not fit
 * fold into "+N more". An empty list draws nothing and asks for no height.
 */
class UIMixerSendList : public NUIComponent {
public:
    struct Row {
        std::string name;
        NUIColor color{0.5f, 0.5f, 0.5f, 1.0f};
        float levelDb{0.0f};
        bool sidechain{false};
        bool muted{false};
    };

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setRows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return m_rows; }

    /// Height wanted for @p rowCount rows; 0 for none.
    static float heightForRows(int rowCount);
    static constexpr float kRowH = 16.0f;
    static constexpr int kMaxRows = 3;
    /// "−18", "−6.5", "0": the level the way a strip prints it.
    static std::string formatLevel(float db);

    int visibleRowCapacity() const;
    std::function<void()> onClicked;

private:
    std::vector<Row> m_rows;
    bool m_pressed{false};
};

} // namespace AestraUI
