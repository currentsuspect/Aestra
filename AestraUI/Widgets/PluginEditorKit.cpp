// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginEditorKit.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace AestraUI {
namespace EditorKit {

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color) {
    if (endAngle < startAngle)
        std::swap(startAngle, endAngle);
    if (endAngle - startAngle <= 0.001f)
        return;
    std::array<NUIPoint, 49> pts{};
    const float div = static_cast<float>(pts.size() - 1);
    for (size_t i = 0; i < pts.size(); ++i) {
        const float t = static_cast<float>(i) / div;
        const float a = startAngle + (endAngle - startAngle) * t;
        pts[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()), thickness, color);
}

void drawKnob(NUIRenderer& renderer, const NUIRect& rect, float value, const char* label, const std::string& valueText,
              NUIColor accent, NUIColor insetSurface, const KnobLook& look) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIPoint c = rect.center();
    const float r = rect.width * 0.5f - 4.0f;
    const float angle = kKnobStart + value * kKnobSweep;
    const float arcWidth = look.large ? 4.0f : 3.0f;

    renderer.fillCircle(c, r + 4.0f, insetSurface);
    renderer.strokeCircle(c, r + 4.0f, 1.0f, editorInk(0.060f));

    drawArc(renderer, c, r - 3.0f, kKnobStart, kKnobStart + kKnobSweep, arcWidth, editorNeutral(0.199f, 1.0f));
    if (look.bipolar) {
        // Fill from the top-centre detent toward the value. Always the thin arc:
        // that is what every bipolar knob drew before the kit existed.
        const float centerAngle = kKnobStart + 0.5f * kKnobSweep;
        drawArc(renderer, c, r - 3.0f, std::min(centerAngle, angle), std::max(centerAngle, angle), 3.0f,
                accent.withAlpha(0.92f));
    } else {
        drawArc(renderer, c, r - 3.0f, kKnobStart, angle, arcWidth, accent.withAlpha(0.92f));
    }

    const float needleLen = r - (look.large ? 14.0f : look.smallNeedleInset);
    const NUIPoint tip(c.x + std::cos(angle) * needleLen, c.y + std::sin(angle) * needleLen);
    renderer.drawLine(c, tip, 2.0f, accent.withAlpha(0.85f));
    renderer.fillCircle(tip, look.large ? 3.5f : 2.5f, accent);

    const float wellR = r * (look.large ? 0.34f : look.smallWellRatio);
    renderer.fillCircle(c, wellR, editorNeutral(0.045f, 0.96f));
    renderer.strokeCircle(c, wellR, 1.2f, accent.withAlpha(0.36f));

    renderer.drawTextCentered(label, NUIRect(rect.x, rect.bottom() + 4.0f, rect.width, 14.0f), 10.5f,
                              theme.getColor("textPrimary").withAlpha(0.95f));
    const float pad = look.valueLabelPad;
    renderer.drawTextCentered(valueText, NUIRect(rect.x - pad, rect.bottom() + 19.0f, rect.width + pad * 2.0f, 13.0f),
                              look.large ? 10.0f : 9.0f, accent.withAlpha(0.96f));
}

} // namespace EditorKit
} // namespace AestraUI
