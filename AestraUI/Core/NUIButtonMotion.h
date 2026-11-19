// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// How a control answers the pointer, in one place. Every button belongs to a group, and the group sets its motion:
//
//   Action   pills that do a thing (Save, Export, Reset peaks, + Channel): colour pop on hover, a small dip on
//            press and one soft bounce back
//   Icon     small glyph buttons (transport, toolbar tools): the same, quicker and a little livelier, because a
//            small target needs a bigger gesture to read
//   Toggle   switches and latched buttons: as Action, and the latch itself bounces when it flips
//   Row      menu items, dropdown rows, list rows: colour pop only. Rows sit edge to edge, so nothing moves
//
// A group never changes by surface: the same kind of control moves the same way everywhere. No glow, no coloured
// shadow, no ripple (owner, 13 Nov): colour and a little scale are the whole vocabulary.
//
// Header-only and free of UI types, so the maths is testable in every CI lane. A control owns one ButtonMotion,
// calls update() each frame with what the pointer is doing, and reads hover() and scale() when it draws.

#pragma once

#include <algorithm>
#include <cmath>

namespace AestraUI {

enum class ButtonGroup { Action, Icon, Toggle, Row };

struct ButtonFeel {
    float hoverSeconds; ///< time for the colour pop to go 0 -> 1
    float pressScale;   ///< scale while held
    float bounce;       ///< scale kick on release / latch (added to 1), 0 = none
};

inline ButtonFeel buttonFeel(ButtonGroup group) {
    switch (group) {
    case ButtonGroup::Icon:   return {0.10f, 0.90f, 0.08f};
    case ButtonGroup::Toggle: return {0.12f, 0.95f, 0.05f};
    case ButtonGroup::Row:    return {0.08f, 1.00f, 0.00f};
    case ButtonGroup::Action:
    default:                  return {0.12f, 0.96f, 0.04f};
    }
}

class ButtonMotion {
public:
    explicit ButtonMotion(ButtonGroup group = ButtonGroup::Action) : m_group(group) {}

    void setGroup(ButtonGroup group) { m_group = group; }
    ButtonGroup group() const { return m_group; }

    /// 0 at rest, 1 fully hovered. Eased, for the colour pop.
    float hover() const {
        const float t = std::clamp(m_hover, 0.0f, 1.0f);
        return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    }
    /// 1 at rest; below 1 while held; overshoots a little above 1 as it springs back.
    float scale() const { return m_scale; }
    /// True while anything is still moving: the owner repaints until it settles.
    bool animating() const { return m_animating; }

    /// The pointer moved to a different row of the same list: the new row's colour pops in from rest.
    void restartHover() { m_hover = 0.0f; m_animating = true; }

    /// A latch flipped (toggle on/off): kick the scale the way a release does.
    void kick() {
        const ButtonFeel feel = buttonFeel(m_group);
        m_velocity += feel.bounce * kKickGain;
        m_animating = true;
    }

    void update(float dt, bool hovered, bool pressed) {
        const ButtonFeel feel = buttonFeel(m_group);
        dt = std::min(dt, 1.0f / 20.0f); // a stalled frame must not throw the spring

        const float step = dt / feel.hoverSeconds;
        m_hover = hovered ? std::min(1.0f, m_hover + step) : std::max(0.0f, m_hover - step);

        if (pressed && !m_wasPressed) m_target = feel.pressScale;
        if (!pressed && m_wasPressed) {
            m_target = 1.0f;
            m_velocity += feel.bounce * kKickGain; // let go: spring past 1 once, then settle
        }
        m_wasPressed = pressed;
        if (!pressed && m_target != 1.0f) m_target = 1.0f;

        // A damped spring toward the target (k = 520, damping ratio ~0.5: one visible overshoot, then still).
        constexpr float k = 520.0f, c = 23.0f;
        const float accel = -k * (m_scale - m_target) - c * m_velocity;
        m_velocity += accel * dt;
        m_scale += m_velocity * dt;

        const bool settled = std::fabs(m_scale - m_target) < 0.0005f && std::fabs(m_velocity) < 0.01f;
        if (settled) {
            m_scale = m_target;
            m_velocity = 0.0f;
        }
        m_animating = !settled || (m_hover > 0.0f && m_hover < 1.0f);
    }

private:
    static constexpr float kKickGain = 12.0f; ///< scale/second per unit of bounce: ~ the bounce as a peak overshoot
    ButtonGroup m_group;
    float m_hover = 0.0f;
    float m_scale = 1.0f;
    float m_velocity = 0.0f;
    float m_target = 1.0f;
    bool m_wasPressed = false;
    bool m_animating = false;
};

} // namespace AestraUI
