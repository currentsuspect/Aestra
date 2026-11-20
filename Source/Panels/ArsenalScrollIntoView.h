// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>

namespace Aestra::ArsenalScrollIntoView {

/// The scroll offset that shows [@p top, @p bottom] (px from the top of the content) with @p margin to spare, moving the
/// list as little as possible. A span taller than the view shows its top. Never scrolls past the content.
inline float scrollToShow(float scroll, float viewHeight, float top, float bottom, float margin, float contentHeight) {
    float target = scroll;
    if (bottom + margin - top > viewHeight || top - margin < scroll) target = top - margin;
    else if (bottom + margin > scroll + viewHeight) target = bottom + margin - viewHeight;
    return std::clamp(target, 0.0f, std::max(0.0f, contentHeight - viewHeight));
}

} // namespace Aestra::ArsenalScrollIntoView
