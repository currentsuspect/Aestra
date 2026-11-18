// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The toast policy, header-only so it is testable in every CI lane. One rule for every message:
//
//   - every toast stays the same time (kToastSeconds); no caller picks its own
//   - up to kMaxToasts show at once, newest at the bottom, older ones stacked above it
//   - a fourth message pushes the oldest off the top at once, rather than waiting its turn
//   - the same text as one already showing moves that one to the bottom and restarts its time,
//     so a repeated action never stacks copies of itself

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace AestraUI {

inline constexpr double kToastSeconds = 3.5;
inline constexpr std::size_t kMaxToasts = 3;

struct ToastEntry {
    std::string text;
    double age = 0.0; ///< seconds on screen
};

class ToastStack {
public:
    /// Oldest first, so the last entry is the newest and sits at the bottom.
    const std::vector<ToastEntry>& entries() const { return m_entries; }
    bool empty() const { return m_entries.empty(); }

    void push(const std::string& text) {
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                       [&](const ToastEntry& e) { return e.text == text; }),
                        m_entries.end());
        m_entries.push_back({text, 0.0});
        if (m_entries.size() > kMaxToasts) m_entries.erase(m_entries.begin(), m_entries.end() - kMaxToasts);
    }

    /// Ages every toast and drops the ones that have had their time. True when the stack changed.
    bool update(double dt) {
        for (auto& e : m_entries) e.age += dt;
        const std::size_t before = m_entries.size();
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                       [](const ToastEntry& e) { return e.age >= kToastSeconds; }),
                        m_entries.end());
        return m_entries.size() != before;
    }

private:
    std::vector<ToastEntry> m_entries;
};

} // namespace AestraUI
