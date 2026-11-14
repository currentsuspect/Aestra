// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief A named position on the timeline: a song section (Intro, Hook) or a cue.
 *
 * Anchored to BEATS, never to seconds or pixels: a marker has to stay on bar 17
 * through a tempo change and through any zoom level, exactly like a clip does.
 * Identity is a stable id — undo restores the id, the project file carries it,
 * and navigation never depends on the marker's index in the list.
 */
struct Marker {
    uint64_t id{0};
    std::string name;
    double beat{0.0};
};

/**
 * @brief The project's markers, kept sorted by beat.
 *
 * Sorted storage is what makes "next / previous marker" a lookup instead of a
 * scan, and it means the ruler can walk the list in display order. Two markers
 * may share a beat (a section and a cue on the same bar), so ordering falls back
 * to id for stability.
 */
class MarkerModel {
public:
    static constexpr size_t kMaxMarkers = 4096;  // a hostile project file cannot grow this without bound
    static constexpr size_t kMaxNameBytes = 128; // names are UI labels, not notes
    static constexpr double kBeatEpsilon = 1e-6;

    /** @brief Add a marker. `requestedId` restores an id on undo/load and never mints a new one. */
    uint64_t add(double beat, std::string name = {}, uint64_t requestedId = 0) {
        if (!std::isfinite(beat) || beat < 0.0) {
            return 0;
        }
        if (m_markers.size() >= kMaxMarkers) {
            return 0;
        }

        Marker marker;
        marker.beat = beat;
        if (requestedId != 0) {
            if (find(requestedId) != nullptr) {
                return 0; // identity is unique; a duplicate restore would shadow a live marker
            }
            marker.id = requestedId;
            m_nextId = std::max(m_nextId, requestedId + 1);
        } else {
            while (find(m_nextId) != nullptr) {
                ++m_nextId;
            }
            marker.id = m_nextId++;
        }

        marker.name = boundedName(name);
        if (marker.name.empty()) {
            marker.name = nextDefaultName();
        }
        const uint64_t id = marker.id;
        insertSorted(std::move(marker));
        return id;
    }

    bool remove(uint64_t id) {
        auto it = std::find_if(m_markers.begin(), m_markers.end(), [id](const Marker& m) { return m.id == id; });
        if (it == m_markers.end()) {
            return false;
        }
        m_markers.erase(it);
        return true;
    }

    bool rename(uint64_t id, const std::string& name) {
        const std::string bounded = boundedName(name);
        if (bounded.empty()) {
            return false; // an unnamed marker is a bug, not a state: callers keep the old name
        }
        Marker* marker = mutableFind(id);
        if (marker == nullptr) {
            return false;
        }
        if (marker->name == bounded) {
            return false; // nothing changed: not an edit, so not an undo step
        }
        marker->name = bounded;
        return true;
    }

    bool moveTo(uint64_t id, double beat) {
        if (!std::isfinite(beat) || beat < 0.0) {
            return false;
        }
        Marker* marker = mutableFind(id);
        if (marker == nullptr) {
            return false;
        }
        if (std::abs(marker->beat - beat) <= kBeatEpsilon) {
            return false;
        }
        marker->beat = beat;
        sort();
        return true;
    }

    void clear() {
        m_markers.clear();
        m_nextId = 1;
        m_nextDefaultName = 1;
    }

    /** @brief Markers in display order. */
    const std::vector<Marker>& markers() const { return m_markers; }
    bool empty() const { return m_markers.empty(); }
    size_t count() const { return m_markers.size(); }

    const Marker* find(uint64_t id) const {
        auto it = std::find_if(m_markers.begin(), m_markers.end(), [id](const Marker& m) { return m.id == id; });
        return it == m_markers.end() ? nullptr : &*it;
    }

    /** @brief First marker strictly after `beat`; wraps to the first when `wrap` is set. */
    const Marker* nextAfter(double beat, bool wrap) const {
        for (const auto& marker : m_markers) {
            if (marker.beat > beat + kBeatEpsilon) {
                return &marker;
            }
        }
        return wrap && !m_markers.empty() ? &m_markers.front() : nullptr;
    }

    /** @brief Last marker strictly before `beat`; wraps to the last when `wrap` is set. */
    const Marker* previousBefore(double beat, bool wrap) const {
        for (auto it = m_markers.rbegin(); it != m_markers.rend(); ++it) {
            if (it->beat < beat - kBeatEpsilon) {
                return &*it;
            }
        }
        return wrap && !m_markers.empty() ? &m_markers.back() : nullptr;
    }

    /** @brief Nearest marker within `toleranceBeats`, or null. Used by ruler hit-tests. */
    const Marker* nearest(double beat, double toleranceBeats) const {
        const Marker* best = nullptr;
        double bestDistance = toleranceBeats;
        for (const auto& marker : m_markers) {
            const double distance = std::abs(marker.beat - beat);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = &marker;
            }
        }
        return best;
    }

private:
    Marker* mutableFind(uint64_t id) {
        auto it = std::find_if(m_markers.begin(), m_markers.end(), [id](const Marker& m) { return m.id == id; });
        return it == m_markers.end() ? nullptr : &*it;
    }

    /** @brief Trimmed, at most kMaxNameBytes, cut on a whole UTF-8 character. Empty when only whitespace. */
    static std::string boundedName(const std::string& name) {
        const auto first = name.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        std::string out = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
        if (out.size() > kMaxNameBytes) {
            size_t cut = kMaxNameBytes;
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
                --cut; // never leave half of a multi-byte character
            }
            out.resize(cut);
        }
        return out;
    }

    std::string nextDefaultName() { return "Marker " + std::to_string(m_nextDefaultName++); }

    void insertSorted(Marker marker) {
        const auto at = std::upper_bound(m_markers.begin(), m_markers.end(), marker,
                                         [](const Marker& lhs, const Marker& rhs) { return less(lhs, rhs); });
        m_markers.insert(at, std::move(marker));
    }

    void sort() {
        std::stable_sort(m_markers.begin(), m_markers.end(), [](const Marker& a, const Marker& b) { return less(a, b); });
    }

    static bool less(const Marker& a, const Marker& b) {
        if (std::abs(a.beat - b.beat) > kBeatEpsilon) {
            return a.beat < b.beat;
        }
        return a.id < b.id;
    }

    std::vector<Marker> m_markers;
    uint64_t m_nextId{1};
    uint64_t m_nextDefaultName{1};
};

} // namespace Audio
} // namespace Aestra
