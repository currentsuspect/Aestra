// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// SPEC-028 R1: the invalidation region. What changed on screen this tick, as a small set of window-space rectangles.
//
// Pure and headless (no GL, no widgets): the renderer (R3) reads it, the widgets (NUIComponent::repaint) write it.
// Invariants worth testing: the set stays small (a cap, so fifteen invalidations never become fifteen tree walks),
// overlapping or near rectangles join, and past a fraction of the window the answer is "the whole window".

#include "NUITypes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace AestraUI {

class NUIDirtyRegion {
public:
    struct Config {
        size_t maxRects = 8;       ///< more than this and the cheapest pairs are joined until it fits
        float mergeGap = 8.0f;     ///< rectangles this close (px) are joined: two draw calls cost more than a few pixels
        float fullFraction = 0.5f; ///< resolve() answers "the whole window" at or above this share of its area
    };

    NUIDirtyRegion() = default;
    explicit NUIDirtyRegion(const Config& config) : m_config(config) {}

    /// Invalidate a rectangle (window space). Empty or non-finite rectangles are ignored.
    void add(const NUIRect& rect) {
        if (!finite(rect) || rect.isEmpty()) return;
        NUIRect merged = rect;
        // Joining can make the result touch rectangles it did not touch before: repeat until stable.
        for (bool joined = true; joined;) {
            joined = false;
            for (size_t i = 0; i < m_rects.size(); ++i) {
                if (near(merged, m_rects[i])) {
                    merged = unite(merged, m_rects[i]);
                    m_rects.erase(m_rects.begin() + static_cast<std::ptrdiff_t>(i));
                    joined = true;
                    break;
                }
            }
        }
        m_rects.push_back(merged);
        capCount();
    }

    /// Something moved or resized: both where it was and where it is must repaint, or the old pixels stay behind.
    void addMoved(const NUIRect& before, const NUIRect& after) {
        add(before);
        add(after);
    }

    void clear() { m_rects.clear(); }
    bool empty() const { return m_rects.empty(); }
    const std::vector<NUIRect>& rects() const { return m_rects; }

    float area() const {
        float total = 0.0f;
        for (const auto& r : m_rects) total += r.width * r.height;
        return total;
    }

    /// The rectangles to draw for a window of this size: clipped to it, and the whole window once they cover
    /// fullFraction of it (a few big rectangles cost more to manage than one full redraw saves).
    std::vector<NUIRect> resolve(const NUIRect& window) const {
        std::vector<NUIRect> out;
        float total = 0.0f;
        for (const auto& r : m_rects) {
            const NUIRect clipped = intersection(r, window);
            if (clipped.isEmpty()) continue;
            out.push_back(clipped);
            total += clipped.width * clipped.height;
        }
        const float windowArea = window.width * window.height;
        if (!out.empty() && windowArea > 0.0f && total >= m_config.fullFraction * windowArea) return {window};
        return out;
    }

    /// Share of the window the resolved rectangles cover, 0..1 (for the frame-stats report).
    float fractionOf(const NUIRect& window) const {
        const float windowArea = window.width * window.height;
        if (windowArea <= 0.0f) return 0.0f;
        float total = 0.0f;
        for (const auto& r : resolve(window)) total += r.width * r.height;
        return std::min(1.0f, total / windowArea);
    }

    static NUIRect unite(const NUIRect& a, const NUIRect& b) {
        const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        const float x1 = std::max(a.right(), b.right()), y1 = std::max(a.bottom(), b.bottom());
        return NUIRect(x0, y0, x1 - x0, y1 - y0);
    }

    static NUIRect intersection(const NUIRect& a, const NUIRect& b) {
        const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
        const float x1 = std::min(a.right(), b.right()), y1 = std::min(a.bottom(), b.bottom());
        return x1 > x0 && y1 > y0 ? NUIRect(x0, y0, x1 - x0, y1 - y0) : NUIRect();
    }

private:
    static bool finite(const NUIRect& r) {
        return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height);
    }

    bool near(const NUIRect& a, const NUIRect& b) const {
        const float g = m_config.mergeGap;
        return a.x < b.right() + g && a.right() + g > b.x && a.y < b.bottom() + g && a.bottom() + g > b.y;
    }

    /// Over the cap: join the pair whose union wastes the least area, until the set fits.
    void capCount() {
        while (m_rects.size() > m_config.maxRects && m_rects.size() > 1) {
            size_t bi = 0, bj = 1;
            float best = 1.0e30f;
            for (size_t i = 0; i < m_rects.size(); ++i) {
                for (size_t j = i + 1; j < m_rects.size(); ++j) {
                    const NUIRect u = unite(m_rects[i], m_rects[j]);
                    const float waste = u.width * u.height - m_rects[i].width * m_rects[i].height -
                                        m_rects[j].width * m_rects[j].height;
                    if (waste < best) {
                        best = waste;
                        bi = i;
                        bj = j;
                    }
                }
            }
            m_rects[bi] = unite(m_rects[bi], m_rects[bj]);
            m_rects.erase(m_rects.begin() + static_cast<std::ptrdiff_t>(bj));
        }
    }

    Config m_config;
    std::vector<NUIRect> m_rects;
};

} // namespace AestraUI
