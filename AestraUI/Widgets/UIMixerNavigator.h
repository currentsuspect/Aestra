// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace AestraUI {

/**
 * @brief The mixer's navigator for sessions that overflow the panel (V81-M1, FD-28 point 7).
 *
 * One tick per channel, in the strips' order and proportion: its colour on top and a live level inside.
 * A bracket marks what is on screen; drag it, or click a tick to centre that channel. Group labels above
 * the ticks jump to a group. A red flag means clipped since reset, a green one soloed, and both stay
 * visible while the channel is off-screen. The panel shows it only when channels overflow.
 *
 * Positions are in strip-row coordinates (0 .. contentWidth), so the navigator maps them linearly.
 */
class UIMixerNavigator : public NUIComponent {
public:
    struct Tick {
        uint32_t channelId{0};
        float x{0.0f}; ///< left edge in strip-row coordinates
        float w{0.0f};
        NUIColor color{0.5f, 0.5f, 0.5f, 1.0f};
        float level{0.0f}; ///< 0..1 height of the live level
        bool clipped{false};
        bool soloed{false};
        bool selected{false};
    };
    struct Group {
        std::string label;
        float x{0.0f};
        float w{0.0f};
    };

    static constexpr float kGroupRowH = 13.0f;
    static constexpr float kTickRowH = 22.0f;
    /// Height the panel gives it: the tick row, plus a label row when there are groups.
    static float heightFor(bool hasGroups) { return kTickRowH + 4.0f + (hasGroups ? kGroupRowH : 0.0f); }

    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;

    void setContent(std::vector<Tick> ticks, std::vector<Group> groups, float contentWidth);
    /// Where the strip row is scrolled to and how much of it shows.
    void setViewport(float scrollX, float viewWidth);

    bool isDragging() const { return m_draggingBracket; }
    const std::vector<Tick>& ticks() const { return m_ticks; }
    const std::vector<Group>& groups() const { return m_groups; }

    /// Geometry, public so a test clicks what a user would.
    NUIRect tickRect(size_t index) const;
    NUIRect groupLabelRect(size_t index) const;
    NUIRect bracketRect() const;

    /// The strip-row scroll that centres [x, x+w] in the viewport (clamped by the owner).
    float scrollToCentre(float x, float w) const;

    std::function<void(float scrollX)> onScrollRequested;
    std::function<void(uint32_t channelId)> onTickClicked;

private:
    float toNavX(float contentX) const;
    float toContentDelta(float navDx) const;
    NUIRect tickRow() const;

    std::vector<Tick> m_ticks;
    std::vector<Group> m_groups;
    float m_contentW{1.0f};
    float m_scrollX{0.0f};
    float m_viewW{1.0f};

    bool m_draggingBracket{false};
    float m_dragStartX{0.0f};
    float m_dragStartScroll{0.0f};
};

} // namespace AestraUI
