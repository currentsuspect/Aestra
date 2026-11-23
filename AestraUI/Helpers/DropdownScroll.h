// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Pure scroll math for a clipped, vertically scrolling list (the mixer's "Add an effect" plugin finder).
// The dropdown had no scroll offset at all, so with a few hundred scanned plugins most rows were unreachable.
// Everything here is header-only and free of UI types so it can be table-tested without a window.
//
// Coordinates: a span's `y` is in CONTENT space (0 = top of the first row). The viewport is the visible window
// onto that content; `offset` is how far the content has slid up under it.

#pragma once

#include <algorithm>
#include <vector>

namespace AestraUI::DropdownScroll {

struct Span {
    float y{0.0f};
    float h{0.0f};
};

/// Furthest the list can scroll; 0 when everything already fits.
inline float maxOffset(float contentHeight, float viewportHeight) {
    return std::max(0.0f, contentHeight - viewportHeight);
}

inline float clampOffset(float offset, float contentHeight, float viewportHeight) {
    return std::clamp(offset, 0.0f, maxOffset(contentHeight, viewportHeight));
}

/// Wheel convention: positive wheelDelta (wheel up) scrolls toward the top, so the offset shrinks.
inline float applyWheel(float offset, float wheelDelta, float notchPx, float contentHeight, float viewportHeight) {
    return clampOffset(offset - wheelDelta * notchPx, contentHeight, viewportHeight);
}

/// Smallest move that brings [rowTop, rowTop + rowH) fully into the viewport.
inline float ensureVisible(float offset, float rowTop, float rowH, float contentHeight, float viewportHeight) {
    if (rowTop < offset) {
        offset = rowTop;
    } else if (rowTop + rowH > offset + viewportHeight) {
        offset = rowTop + rowH - viewportHeight;
    }
    return clampOffset(offset, contentHeight, viewportHeight);
}

/// Row under a pointer. `pointerY` is relative to the top of the viewport. Returns -1 outside the viewport
/// (clipped rows are not hit-testable) or when no span is under the pointer.
inline int hitTest(const std::vector<Span>& spans, float pointerY, float offset, float viewportHeight) {
    if (pointerY < 0.0f || pointerY >= viewportHeight) {
        return -1;
    }
    const float contentY = pointerY + offset;
    for (size_t i = 0; i < spans.size(); ++i) {
        if (contentY >= spans[i].y && contentY < spans[i].y + spans[i].h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// Slim indicator thumb inside a track of `trackH`: {top, height}; height 0 when nothing scrolls.
struct Thumb {
    float top{0.0f};
    float height{0.0f};
};
inline Thumb thumb(float offset, float contentHeight, float viewportHeight, float trackH, float minThumbH = 24.0f) {
    const float maxOff = maxOffset(contentHeight, viewportHeight);
    if (maxOff <= 0.0f || contentHeight <= 0.0f) {
        return {};
    }
    const float h = std::min(trackH, std::max(minThumbH, trackH * (viewportHeight / contentHeight)));
    const float travel = std::max(0.0f, trackH - h);
    return {travel * std::clamp(offset / maxOff, 0.0f, 1.0f), h};
}

} // namespace AestraUI::DropdownScroll
