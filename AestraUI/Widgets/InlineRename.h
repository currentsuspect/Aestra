// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Base/NUITextInput.h"

#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace AestraUI {

/**
 * @brief Rename in place: a text field laid over a name (track header, mixer strip), as Arsenal's unit names do.
 *
 * The host owns one, forwards mouse events and updates to it, and paints it last (it is not a child, so a cached
 * host keeps its cache). Enter or a click elsewhere commits; Escape cancels. The field takes keyboard focus, so
 * typing reaches it before the host's shortcuts. A field is never destroyed inside its own callback: the one that
 * ended is kept until the next update.
 */
/**
 * @brief A double-click seen from presses: the platform's flag, or two left presses within 400 ms a few pixels apart
 *        (the flag does not reach every widget on every platform; Arsenal's name label times its own the same way).
 */
class DoubleClickTimer {
public:
    bool press(const NUIPoint& at, bool platformFlag) {
        const auto now = std::chrono::steady_clock::now();
        const bool quick = m_hasLast && now - m_last < std::chrono::milliseconds(400) &&
                           std::abs(at.x - m_lastAt.x) <= 4.0f && std::abs(at.y - m_lastAt.y) <= 4.0f;
        const bool isDouble = platformFlag || quick;
        m_hasLast = !isDouble; // a third press starts a new pair
        m_last = now;
        m_lastAt = at;
        return isDouble;
    }

private:
    std::chrono::steady_clock::time_point m_last{};
    NUIPoint m_lastAt{};
    bool m_hasLast = false;
};

class InlineRename {
public:
    void begin(const NUIRect& rect, const std::string& text, std::function<void(const std::string&)> onCommit,
               std::function<void()> onChanged = {}) {
        end(false);
        m_onCommit = std::move(onCommit);
        m_onChanged = std::move(onChanged);
        m_original = text;
        auto input = std::make_shared<NUITextInput>(text);
        input->setBounds(rect);
        input->setOnReturnKey([this]() { end(true); });
        input->setOnEscapeKey([this]() { end(false); });
        input->setOnFocusLost([this]() { end(true); });
        input->setOnTextChange([this](const std::string&) { if (m_onChanged) m_onChanged(); });
        m_input = input;
        m_input->setFocused(true);
        m_input->selectAll();
        if (m_onChanged) m_onChanged();
    }

    bool active() const { return m_input != nullptr; }
    void setBounds(const NUIRect& rect) { if (m_input) m_input->setBounds(rect); }

    /// True when the event was the field's. A press outside it commits and is left for the host.
    bool onMouseEvent(const NUIMouseEvent& event) {
        if (!m_input) return false;
        if (m_input->getBounds().contains(event.position)) {
            m_input->onMouseEvent(event);
            return true;
        }
        if (event.pressed) end(true);
        return false;
    }

    void onUpdate(double dt) {
        m_retired.reset();
        if (m_input) m_input->onUpdate(dt);
    }

    void render(NUIRenderer& renderer) {
        if (m_input) m_input->onRender(renderer);
    }

    /// Finish: @p commit hands the text to the owner when it changed.
    void end(bool commit) {
        if (!m_input) return;
        auto input = std::move(m_input);
        const std::string text = input->getText();
        input->setFocused(false);
        m_retired = input;
        auto onCommit = std::move(m_onCommit);
        auto onChanged = std::move(m_onChanged);
        if (commit && text != m_original && onCommit) onCommit(text);
        if (onChanged) onChanged();
    }

private:
    std::shared_ptr<NUITextInput> m_input;
    std::shared_ptr<NUITextInput> m_retired;
    std::function<void(const std::string&)> m_onCommit;
    std::function<void()> m_onChanged;
    std::string m_original;
};

} // namespace AestraUI
