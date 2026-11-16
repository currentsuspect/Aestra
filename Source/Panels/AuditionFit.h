// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Text that has to fit a place. The Audition hub drew titles, chips and readouts at whatever width they came to,
// so a long name ran under the rail beside it and "Compressed stream" ran into the loudness readout (owner,
// 16 Nov 2026). Every string drawn into a bounded place goes through one of these.

#include "NUIRenderer.h"

#include <algorithm>
#include <string>

namespace Aestra {

/// @p text cut at a whole character with "..." so it measures no wider than @p maxWidth at @p fontSize.
inline std::string fitText(AestraUI::NUIRenderer& r, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty() || maxWidth <= 0.0f) return {};
    if (r.measureText(text, fontSize).width <= maxWidth) return text;
    const std::string dots = "...";
    if (r.measureText(dots, fontSize).width > maxWidth) return {}; // not even the dots fit: nothing, not an overrun
    size_t lo = 0, hi = text.size();
    while (lo < hi) { // the longest prefix that still fits with the dots
        const size_t mid = (lo + hi + 1) / 2;
        size_t cut = mid;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut; // never inside a character
        if (r.measureText(text.substr(0, cut) + dots, fontSize).width <= maxWidth) lo = mid; else hi = mid - 1;
    }
    size_t cut = lo;
    while (cut > 0 && cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + dots;
}

/// The largest of @p sizes (biggest first) at which @p text fits @p maxWidth, or the smallest if none does.
template <size_t N>
float fitFontSize(AestraUI::NUIRenderer& r, const std::string& text, const float (&sizes)[N], float maxWidth) {
    for (float size : sizes)
        if (r.measureText(text, size).width <= maxWidth) return size;
    return sizes[N - 1];
}

} // namespace Aestra
