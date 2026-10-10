// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// What the toast does with a message that arrives while another is showing (V8-C14 ranked
// gap 4: "toasts overwrite each other"). Header-only, no UI dependency, so the policy is
// testable in every CI lane.
//
//   - nothing showing         -> show it now
//   - the same text showing   -> show it now (restarts its timer; nothing is lost)
//   - something else showing  -> wait its turn, unless it repeats the last queued message
//   - more than kMaxQueued waiting -> the oldest waiting message is dropped

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace AestraUI {

struct ToastMessage {
    std::string text;
    double seconds = 0.0;
};

class ToastQueue {
public:
    static constexpr std::size_t kMaxQueued = 3;

    /// @p showing is the text on screen, or nullptr. Returns the message to show now, if any.
    std::optional<ToastMessage> offer(ToastMessage message, const std::string* showing) {
        if (!showing || message.text == *showing) {
            return message;
        }
        if (!m_waiting.empty() && m_waiting.back().text == message.text) {
            return std::nullopt;
        }
        if (m_waiting.size() >= kMaxQueued) {
            m_waiting.pop_front();
        }
        m_waiting.push_back(std::move(message));
        return std::nullopt;
    }

    /// The next waiting message, once the one on screen has run its time.
    std::optional<ToastMessage> next() {
        if (m_waiting.empty()) {
            return std::nullopt;
        }
        ToastMessage message = std::move(m_waiting.front());
        m_waiting.pop_front();
        return message;
    }

    std::size_t waiting() const { return m_waiting.size(); }

private:
    std::deque<ToastMessage> m_waiting;
};

} // namespace AestraUI
