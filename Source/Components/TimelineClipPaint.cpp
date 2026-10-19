// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TimelineClipPaint.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Widgets/TrackColorPalette.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Audio {
namespace ClipPaint {

namespace {
// The bed the clips sit on (#0c0b0a). A muted clip is the clip seen through 60 % of it.
const AestraUI::NUIColor kCanvas(12.0f / 255.0f, 11.0f / 255.0f, 10.0f / 255.0f, 1.0f);
} // namespace

AestraUI::NUIColor identityFromARGB(uint32_t argb) {
    const float a = ((argb >> 24) & 0xFF) / 255.0f;
    const AestraUI::NUIColor color(((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
                                   (argb & 0xFF) / 255.0f, a > 0.0f ? a : 1.0f);
    const float mx = std::max({color.r, color.g, color.b});
    const float mn = std::min({color.r, color.g, color.b});
    if (mx - mn < 0.06f) {
        return AestraUI::NUIThemeManager::getInstance().getColor("accentPrimary").withAlpha(1.0f);
    }
    return color.withAlpha(1.0f);
}

AestraUI::NUIColor headerBandColor(const AestraUI::NUIColor& identity) {
    return identity.withAlpha(1.0f);
}

AestraUI::NUIColor headerLabelInk(float alpha) {
    return kCanvas.withAlpha(alpha);
}

void paintBody(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible, const AestraUI::NUIColor& identity,
               bool selected) {
    renderer.fillRect(visible, AestraUI::clipBodyTone(identity, selected));
    // A hairline of the canvas on the left keeps butted clips from merging into one slab.
    renderer.fillRect({visible.x, visible.y, 1.0f, visible.height}, kCanvas.withAlpha(0.55f));
}

AestraUI::NUIRect menuSlot(const AestraUI::NUIRect& visible) {
    if (!showsHeaderBand(visible) || visible.width <= 40.0f) return {visible.x, visible.y, 0.0f, 0.0f};
    const float side = headerBandHeight(visible.height);
    return {std::round(visible.x), std::round(visible.y), side, side};
}

void paintHeaderBand(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible,
                     const AestraUI::NUIColor& identity, const std::string& label, float fontSize) {
    const float bandHeight = headerBandHeight(visible.height);
    const AestraUI::NUIRect band(visible.x, visible.y, visible.width, bandHeight);
    renderer.fillRect(band, headerBandColor(identity));
    if (label.empty()) return;
    const float slot = menuSlot(visible).width;
    renderer.drawText(label, AestraUI::NUIPoint(band.x + (slot > 0.0f ? slot + 2.0f : 4.0f),
                                                renderer.calculateTextY(band, fontSize)),
                      fontSize, headerLabelInk());
}

void paintMenuGlyph(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& slot, bool hot, bool pointed) {
    if (slot.width <= 0.0f) return;
    if (pointed) renderer.fillRect(slot, kCanvas.withAlpha(0.18f));
    // Three 1 px lines, 7 px wide, 3 px apart, snapped to whole pixels and centred in the slot: crisp at every
    // band height, never touching the band's edges.
    const float cx = std::round(slot.x + slot.width * 0.5f);
    const float cy = std::round(slot.y + slot.height * 0.5f);
    const AestraUI::NUIColor ink = headerLabelInk(pointed ? 0.95f : (hot ? 0.8f : 0.55f));
    for (int i = -1; i <= 1; ++i) {
        renderer.fillRect({cx - 3.0f, cy + static_cast<float>(i) * 3.0f - 0.5f, 7.0f, 1.0f}, ink);
    }
}

void paintPatternEnd(AestraUI::NUIRenderer& renderer, float x, float top, float bottom,
                     const AestraUI::NUIColor& ink) {
    constexpr float kDash = 3.0f;
    constexpr float kGap = 3.0f;
    for (float y = top; y < bottom; y += kDash + kGap) {
        renderer.drawLine({x, y}, {x, std::min(bottom, y + kDash)}, 1.0f, ink);
    }
}

void paintSelection(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible) {
    renderer.strokeRect(visible, 1.0f, AestraUI::NUIColor::white().withAlpha(0.92f));
}

void paintMuted(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& visible) {
    renderer.fillRect(visible, kCanvas.withAlpha(0.6f));
}

} // namespace ClipPaint
} // namespace Audio
} // namespace Aestra
