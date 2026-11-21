// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// The "no sound yet" notice (Arsenal coherence, mock q2 option C). Pure and header-only so it is testable in every lane.
//
//   - isEmpty: what counts as empty, per unit type
//   - message: "Sampler 4 has no sound yet." / "Samplers 4, 5 and 6 have no sound yet."
//   - Tracker: once per unit per app session, and a wait while recording
//   - bottomInset: the room the list gives up so the toast never covers the last row or the add line

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Aestra::ArsenalEmptyNotice {

enum class Kind { Sampler, PitchedSampler, Instrument, Audio };

/// What the notice needs to know about a unit. hasSample: a sample is loaded. pluginLoaded: a plugin instance is live
/// (a missing or failed plugin is not).
struct Facts {
    Kind kind;
    bool hasSample;
    bool pluginLoaded;
};

/// True when the unit makes no sound at all. A loaded plugin with no preset still makes sound, so it is not empty.
/// Audio units are not subject to the notice.
inline bool isEmpty(const Facts& f) {
    switch (f.kind) {
    case Kind::Sampler:
    case Kind::PitchedSampler:
        return !f.hasSample && !f.pluginLoaded;
    case Kind::Instrument:
        return !f.pluginLoaded;
    case Kind::Audio:
        return false;
    }
    return false;
}

namespace detail {
// "Sampler 4" -> {"Sampler", "4"}; false when the name does not end in " <digits>".
inline bool splitNumbered(const std::string& name, std::string& prefix, std::string& number) {
    const auto space = name.find_last_of(' ');
    if (space == std::string::npos || space == 0 || space + 1 >= name.size()) return false;
    const std::string tail = name.substr(space + 1);
    if (tail.find_first_not_of("0123456789") != std::string::npos) return false;
    prefix = name.substr(0, space);
    number = tail;
    return true;
}

inline std::string joinWithAnd(const std::vector<std::string>& parts, size_t total) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += (i + 1 == parts.size() && total == parts.size()) ? " and " : ", ";
        out += parts[i];
    }
    if (total > parts.size()) out += " and " + std::to_string(total - parts.size()) + " more";
    return out;
}
} // namespace detail

/// The sentence for one or several empty units, in plain words. Several units named "<Word> <n>" read as one group.
inline std::string message(const std::vector<std::string>& names) {
    constexpr size_t kMaxListed = 4;
    if (names.empty()) return {};
    if (names.size() == 1) return names[0] + " has no sound yet.";
    const size_t listed = std::min(names.size(), kMaxListed);

    bool grouped = true;
    std::string prefix, number;
    std::vector<std::string> numbers;
    for (size_t i = 0; i < names.size() && grouped; ++i) {
        std::string p, n;
        if (!detail::splitNumbered(names[i], p, n) || (i > 0 && p != prefix)) { grouped = false; break; }
        prefix = p;
        if (i < listed) numbers.push_back(n);
    }
    if (grouped) return prefix + "s " + detail::joinWithAnd(numbers, names.size()) + " have no sound yet.";
    return detail::joinWithAnd(std::vector<std::string>(names.begin(), names.begin() + static_cast<std::ptrdiff_t>(listed)), names.size()) +
           " have no sound yet.";
}

/// Once per unit per app session, and never while recording. Closing the toast counts (a unit is used up the moment
/// it is shown); a unit that got a sound while it waited is dropped without being used up.
class Tracker {
public:
    /// Queues a unit for the next toast, unless it was already told about this session.
    void note(uint64_t unit) {
        if (wasShown(unit) || std::find(m_pending.begin(), m_pending.end(), unit) != m_pending.end()) return;
        m_pending.push_back(unit);
    }
    bool hasPending() const { return !m_pending.empty(); }
    bool wasShown(uint64_t unit) const { return std::find(m_shown.begin(), m_shown.end(), unit) != m_shown.end(); }

    /// The units to tell about now, in the order they were noted. While recording nothing comes out and everything keeps waiting.
    std::vector<uint64_t> take(bool recording, const std::function<bool(uint64_t)>& stillEmpty) {
        std::vector<uint64_t> out;
        if (recording) return out;
        for (const uint64_t unit : m_pending) {
            if (!stillEmpty(unit)) continue;
            out.push_back(unit);
            m_shown.push_back(unit);
        }
        m_pending.clear();
        return out;
    }

private:
    std::vector<uint64_t> m_pending, m_shown;
};

/// How far the list's bottom edge must rise so the toast (top at @p toastTop) covers none of it. @p panelBottom is the
/// list viewport's bottom; @p overlapsHorizontally is false when the toast sits beside the panel.
inline float bottomInset(float panelBottom, float toastTop, bool overlapsHorizontally, float gap) {
    if (!overlapsHorizontally || panelBottom < toastTop) return 0.0f;
    return (panelBottom - toastTop) + gap;
}

} // namespace Aestra::ArsenalEmptyNotice
