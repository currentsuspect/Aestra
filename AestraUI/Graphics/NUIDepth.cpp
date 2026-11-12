// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NUIDepth.h"

#include "../Core/NUIThemeSystem.h"
#include "NUIRenderer.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {
namespace Depth {

namespace {

const NUIThemeProperties& theme() { return NUIThemeManager::getInstance().getCurrentTheme(); }

NUIColor mix(const NUIColor& a, const NUIColor& b, float t) { return NUIColor::lerp(a, b, std::clamp(t, 0.0f, 1.0f)); }

NUIColor scaledAlpha(const NUIColor& c, float k) { return c.withAlpha(std::clamp(c.a * k, 0.0f, 1.0f)); }

} // namespace

void bed(NUIRenderer& renderer, const NUIRect& rect, float radius, Cut cut) {
    bed(renderer, rect, radius, theme().backgroundPrimary, cut);
}

void bed(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill, Cut cut) {
    const auto& t = theme();
    renderer.fillRoundedRect(rect, radius, fill);
    // Spec values: Shallow = inset 2 2 4 neuLo / -1 -1 3 neuHi; Deep = 3 3 6 / -2 -2 5; Slot = 1 2 3 / -1 -1 1.
    switch (cut) {
    case Cut::Shallow:
        renderer.drawInnerShadow(rect, radius, 2.0f, 2.0f, t.neuLo);
        renderer.drawInnerShadow(rect, radius, -1.0f, 1.5f, t.neuHi);
        break;
    case Cut::Deep:
        renderer.drawInnerShadow(rect, radius, 3.0f, 3.0f, t.neuLo);
        renderer.drawInnerShadow(rect, radius, -2.0f, 2.5f, t.neuHi);
        break;
    case Cut::Slot:
        renderer.drawInnerShadow(rect, radius, 1.5f, 1.5f, t.neuLo);
        renderer.drawInnerShadow(rect, radius, -1.0f, 0.5f, t.neuHi);
        break;
    }
    if (t.neuEdge.a > 0.0f)
        renderer.strokeRoundedRect(rect, radius, 1.0f, t.neuEdge);
}

void object(NUIRenderer& renderer, const NUIRect& rect, float radius, const ObjectLook& look) {
    const auto& t = theme();
    // Cast shadow first: 0 5 8 -2 neuLo and 0 1 2 neuLo at rest; deeper and further when held.
    const float lift = look.held ? 2.0f : 1.0f;
    NUIRect cast = rect;
    cast.x += 1.0f;
    cast.width -= 2.0f;
    renderer.drawShadow(cast, 0.0f, 2.5f * lift, 3.0f * lift, scaledAlpha(t.neuLo, 0.75f));
    renderer.drawShadow(rect, 0.0f, 1.0f, 1.0f, scaledAlpha(t.neuLo, 0.6f));

    NUIRect face = rect;
    if (look.held) {
        // A 5% lift: grow about the centre, as the spec's scale(1.05) does.
        const float gx = rect.width * 0.025f, gy = rect.height * 0.025f;
        face = NUIRect{rect.x - gx, rect.y - gy, rect.width + gx * 2.0f, rect.height + gy * 2.0f};
    }
    const NUIColor top = mix(look.base, NUIColor::white(), look.lightTop);
    const NUIColor bottom = mix(look.base, NUIColor::black(), look.shadeBottom);
    renderer.fillRoundedRectVertical(face, radius, top, bottom);
    // Lit top edge and shaded bottom edge (inset 0 1px 0 white 24%, inset 0 -1px 0 black 32%).
    renderer.drawInnerShadow(face, radius, 1.0f, 0.5f, NUIColor::white().withAlpha(0.24f), true);
    renderer.drawInnerShadow(face, radius, -1.0f, 0.5f, NUIColor::black().withAlpha(0.32f), true);
}

void object(NUIRenderer& renderer, const NUIRect& rect, float radius, bool held) {
    ObjectLook look;
    look.base = theme().surfaceRaised;
    look.held = held;
    object(renderer, rect, radius, look);
}

void floatingCard(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill, float lift) {
    const auto& t = theme();
    // 0 8 18 -6 shadow and 0 1 3 shadow for menus; a dialog (lift 2) is 0 24 48 -12 and 0 2 8.
    NUIRect cast = rect;
    const float inset = 6.0f * lift;
    cast.x += inset;
    cast.width = std::max(0.0f, cast.width - inset * 2.0f);
    renderer.drawShadow(cast, 0.0f, 8.0f * lift, 9.0f * lift, t.shadow);
    renderer.drawShadow(rect, 0.0f, 1.0f * lift, 1.5f * lift, t.shadow);
    renderer.fillRoundedRect(rect, radius, fill);
    if (t.neuEdge.a > 0.0f)
        renderer.strokeRoundedRect(rect, radius, 1.0f, t.neuEdge);
}

void sliderSlot(NUIRenderer& renderer, const NUIRect& track, float t, float origin, bool enabled) {
    const auto& th = theme();
    const NUIRect slot{track.x, std::round(track.y + track.height * 0.5f - 3.0f), track.width, 6.0f};
    bed(renderer, slot, 3.0f, th.meterBackground, Cut::Slot);
    const float a = slot.x + slot.width * std::clamp(std::min(t, origin), 0.0f, 1.0f);
    const float b = slot.x + slot.width * std::clamp(std::max(t, origin), 0.0f, 1.0f);
    if (b - a >= 1.0f)
        renderer.fillRoundedRect({a, slot.y + 1.0f, b - a, slot.height - 2.0f}, 2.0f, enabled ? th.primary : th.textDisabled);
    if (origin > 0.0f && origin < 1.0f) {
        const float x = std::round(slot.x + slot.width * origin) + 0.5f;
        renderer.drawLine({x, slot.y - 3.0f}, {x, slot.bottom() + 3.0f}, 1.0f, th.textMuted);
    }
}

void sliderCap(NUIRenderer& renderer, const NUIPoint& centre, bool held) {
    const auto& th = theme();
    const NUIRect cap{std::round(centre.x - 11.0f), std::round(centre.y - 8.0f), 22.0f, 16.0f};
    object(renderer, cap, 3.0f, held);
    // Grip ridges either side of the centre line, and the line itself.
    for (float dx : {-6.0f, -4.0f, 4.0f, 6.0f})
        renderer.drawLine({cap.x + 11.0f + dx, cap.y + 4.0f}, {cap.x + 11.0f + dx, cap.bottom() - 4.0f}, 1.0f,
                          NUIColor::black().withAlpha(0.26f));
    renderer.fillRect({cap.x + 10.0f, cap.y + 2.0f, 2.0f, cap.height - 4.0f}, th.textPrimary);
}

void faderCap(NUIRenderer& renderer, const NUIRect& cap, bool master, bool held) {
    const auto& th = theme();
    ObjectLook look;
    look.base = master ? NUIColor::lerp(th.backgroundPrimary, th.textPrimary, 0.8f) : th.surfaceRaised;
    look.held = held;
    const float radius = master ? 4.0f : 3.0f;
    object(renderer, cap, radius, look);
    // Grip ridges above and below the centre line: a dark groove with a lit lip under it.
    const float inset = std::max(2.0f, cap.width * 0.2f);
    const float cy = std::round(cap.y + cap.height * 0.5f);
    for (float dy : {-6.0f, -4.0f, 4.0f, 6.0f}) {
        if (std::fabs(dy) + 1.0f >= cap.height * 0.5f)
            continue;
        renderer.fillRect({cap.x + inset, cy + dy, cap.width - inset * 2.0f, 1.0f}, NUIColor::black().withAlpha(0.26f));
        renderer.fillRect({cap.x + inset, cy + dy + 1.0f, cap.width - inset * 2.0f, 1.0f},
                          NUIColor::white().withAlpha(0.10f));
    }
    const float lineH = master ? 3.0f : 2.0f;
    renderer.fillRect({cap.x + 2.0f, cy - lineH * 0.5f, cap.width - 4.0f, lineH}, master ? th.primary : th.textPrimary);
}

void knobCap(NUIRenderer& renderer, const NUIPoint& centre, float radius, float angle, bool held, bool enabled) {
    const auto& th = theme();
    const float r = held ? radius * 1.06f : radius;
    const NUIRect disc{centre.x - r, centre.y - r, r * 2.0f, r * 2.0f};
    ObjectLook look;
    look.base = th.surfaceRaised;
    look.held = held;
    object(renderer, disc, r, look);
    const NUIColor ink = enabled ? th.textPrimary : th.textDisabled;
    renderer.drawLine({centre.x + std::cos(angle) * r * 0.25f, centre.y + std::sin(angle) * r * 0.25f},
                      {centre.x + std::cos(angle) * (r - 2.0f), centre.y + std::sin(angle) * (r - 2.0f)}, 2.0f, ink);
}

void restingPanel(NUIRenderer& renderer, const NUIRect& rect, float radius, const NUIColor& fill) {
    const auto& t = theme();
    renderer.drawShadow(rect, 0.0f, 2.0f, 4.0f, scaledAlpha(t.neuLo, 0.5f));
    renderer.fillRoundedRect(rect, radius, fill);
}

void focusRing(NUIRenderer& renderer, const NUIRect& rect, float radius, float gap) {
    // The stroke is centred on its path, so a 2px ring 2px off the control sits on a path gap + 1 out.
    const float out = gap + 1.0f;
    const NUIRect ring{rect.x - out, rect.y - out, rect.width + out * 2.0f, rect.height + out * 2.0f};
    renderer.strokeRoundedRect(ring, radius > 0.0f ? radius + out : 0.0f, 2.0f, theme().focusRing);
}

void glass(NUIRenderer& renderer, const NUIRect& rect, float radius) {
    const auto& t = theme();
    // Reflection: the spec's 158deg band, glassHi fading to 30% by 36% of the way, then a crisp edge. The renderer
    // has no angled gradient, so the band is a vertical ramp over the top 36% with that crisp lower edge.
    const NUIRect band{rect.x, rect.y, rect.width, std::max(1.0f, rect.height * 0.36f)};
    renderer.fillRoundedRectVertical(band, std::min(radius, band.height * 0.5f), t.glassHi, scaledAlpha(t.glassHi, 0.3f));
    // Lit top edge, the faint pane edge, and shade where the glass meets its bezel at the top and bottom.
    renderer.drawInnerShadow(rect, radius, 1.0f, 0.5f, t.glassEdge, true);
    renderer.strokeRoundedRect(rect, radius, 1.0f, scaledAlpha(t.glassEdge, 0.35f));
    renderer.drawInnerShadow(rect, radius, 3.0f, 6.0f, t.glassShade, true);
    renderer.drawInnerShadow(rect, radius, -2.0f, 5.0f, t.glassShade, true);
}

} // namespace Depth
} // namespace AestraUI
