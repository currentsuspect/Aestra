// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Filled shapes with colour that varies across them: gradients, a cap lit from above, and the inner shadow of a sunk
// well (Design Spec, FD-79). Split from NUIRendererGL.cpp, which is pinned by the file-size ratchet; the shader
// branches they rely on (primitive types 1, 9 and 10) stay with the shader source there.
#include "NUIRendererGL.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

void NUIRendererGL::fillRoundedRectVertical(const NUIRect& rect, float radius, const NUIColor& top,
                                            const NUIColor& bottom) {
    // The same squircle as fillRoundedRect, with the colour running from top to bottom: a cap lit from above.
    AestraUI::NUIRect snapped{std::floor(rect.x), std::floor(rect.y), std::round(rect.width), std::round(rect.height)};
    if (snapped.width <= 0.0f || snapped.height <= 0.0f)
        return;
    constexpr float kAASafePad = 1.5f;
    const AestraUI::NUIRect quad{snapped.x - kAASafePad, snapped.y - kAASafePad, snapped.width + kAASafePad * 2.0f,
                                 snapped.height + kAASafePad * 2.0f};
    const float safeRadius = std::min(radius, std::min(snapped.width * 0.5f, snapped.height * 0.5f));
    if (currentTextureId_ != 0) {
        flush();
        currentTextureId_ = 0;
    }
    // The pad sits outside the shape, so stretch the ramp over the quad to keep the shape's own ends exact.
    const float t0 = -kAASafePad / snapped.height;
    const float t1 = 1.0f + kAASafePad / snapped.height;
    const NUIColor qTop = NUIColor::lerp(top, bottom, t0);
    const NUIColor qBottom = NUIColor::lerp(top, bottom, t1);
    const float w = snapped.width, h = snapped.height;
    addVertex(quad.x, quad.y, 0.0f, 0.0f, qTop, w, h, quad.width, quad.height, safeRadius, 1.0f, 0.0f, 1.0f);
    addVertex(quad.right(), quad.y, 1.0f, 0.0f, qTop, w, h, quad.width, quad.height, safeRadius, 1.0f, 0.0f, 1.0f);
    addVertex(quad.right(), quad.bottom(), 1.0f, 1.0f, qBottom, w, h, quad.width, quad.height, safeRadius, 1.0f, 0.0f,
              1.0f);
    addVertex(quad.x, quad.bottom(), 0.0f, 1.0f, qBottom, w, h, quad.width, quad.height, safeRadius, 1.0f, 0.0f, 1.0f);
    pushQuadIndices(static_cast<uint32_t>(vertices_.size()) - 4);
}

void NUIRendererGL::drawInnerShadow(const NUIRect& rect, float radius, float offset, float blur,
                                    const NUIColor& color, bool verticalOnly) {
    if (rect.width <= 0.0f || rect.height <= 0.0f || color.a <= 0.0f)
        return;
    constexpr float kAASafePad = 1.5f;
    const AestraUI::NUIRect quad{rect.x - kAASafePad, rect.y - kAASafePad, rect.width + kAASafePad * 2.0f,
                                 rect.height + kAASafePad * 2.0f};
    const float safeRadius = std::min(radius, std::min(rect.width * 0.5f, rect.height * 0.5f));
    addQuad(quad, color, rect.width, rect.height, quad.width, quad.height, safeRadius, blur, offset,
            verticalOnly ? 10.0f : 9.0f);
}

void NUIRendererGL::fillRectGradient(const NUIRect& rect, const NUIColor& colorStart, const NUIColor& colorEnd, bool vertical) {
    ensureBasicPrimitive();
    if (vertical) {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorStart);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorEnd);
    } else {
        addVertex(rect.x, rect.y, 0, 0, colorStart);
        addVertex(rect.right(), rect.y, 1, 0, colorEnd);
        addVertex(rect.right(), rect.bottom(), 1, 1, colorEnd);
        addVertex(rect.x, rect.bottom(), 0, 1, colorStart);
    }
    
    uint32_t base = static_cast<uint32_t>(vertices_.size()) - 4;
    pushQuadIndices(base);
}

void NUIRendererGL::fillCircleGradient(const NUIPoint& center, float radius, const NUIColor& colorInner, const NUIColor& colorOuter) {
    ensureBasicPrimitive();
    // Simple radial gradient
    // These vertices have never had indices, so they have never drawn. Keep it
    // that way here: on the quad-only path they would be drawn as quads.
    useExplicitIndices();
    const int segments = 32;
    const float angleStep = 2.0f * 3.14159f / segments;
    
    for (int i = 0; i < segments; ++i) {
        float angle1 = i * angleStep;
        float angle2 = (i + 1) * angleStep;
        
        addVertex(center.x, center.y, 0.5f, 0.5f, colorInner);
        addVertex(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius, 0, 0, colorOuter);
        addVertex(center.x + std::cos(angle2) * radius, center.y + std::sin(angle2) * radius, 1, 1, colorOuter);
    }
}

// ============================================================================
// Effects (Simplified for now)
// ============================================================================

} // namespace AestraUI
