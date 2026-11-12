// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginEditorKit.h"
#include "../Graphics/NUIDepth.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
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


// ---------------------------------------------------------------------------------------------------------------
// Ember control kit (FD-73)
// ---------------------------------------------------------------------------------------------------------------

namespace {

struct KnobGeometry {
    float disc;
    float cell;
    float track;
};

KnobGeometry knobGeometry(KnobSize size) {
    switch (size) {
    case KnobSize::Large: return {56.0f, 80.0f, 3.0f};
    case KnobSize::Small: return {28.0f, 56.0f, 2.0f};
    case KnobSize::Medium: break;
    }
    return {40.0f, 64.0f, 2.0f};
}

NUIRect textRect(float x, float centreY, float width, float height = 20.0f) {
    return NUIRect(x, centreY - height * 0.5f, width, height);
}

} // namespace

EmberColors emberColors() {
    auto& tm = NUIThemeManager::getInstance();
    const auto& t = tm.getCurrentTheme();
    EmberColors c;
    c.well = tm.getColor("timelineBed");
    c.body = t.backgroundPrimary;
    c.panel = t.backgroundSecondary;
    c.control = t.surfaceTertiary;
    c.raised = t.surfaceRaised;
    c.border = t.border;
    c.edge = tm.getColor("controlEdge");
    c.ink = t.textPrimary;
    c.inkSoft = t.textSecondary;
    c.inkMuted = t.textMuted;
    c.inkOff = t.textDisabled;
    c.primary = t.primary;
    c.primaryHover = t.primaryHover;
    c.secondary = t.secondary;
    c.selected = tm.getColor("controlSelected");
    c.guide = tm.getColor("controlGuide");
    c.meter = t.meterSafe;
    c.warn = t.meterWarn;
    c.gridMajor = t.divider;
    c.gridMinor = t.backgroundSecondary;
    return c;
}

float emberKnobCellWidth(KnobSize size) { return knobGeometry(size).cell; }
float emberKnobDiscSize(KnobSize size) { return knobGeometry(size).disc; }
float emberKnobCellHeight(KnobSize size) { return knobGeometry(size).disc + 38.0f; }

void drawTextLeft(NUIRenderer& renderer, const std::string& text, float x, float centreY, float size, NUIColor color) {
    renderer.drawTextCentered(text, textRect(x, centreY, renderer.measureText(text, size).width), size, color);
}

void drawTextRight(NUIRenderer& renderer, const std::string& text, float rightX, float centreY, float size,
                   NUIColor color) {
    const float w = renderer.measureText(text, size).width;
    renderer.drawTextCentered(text, textRect(rightX - w, centreY, w), size, color);
}

float drawCaps(NUIRenderer& renderer, const std::string& text, float x, float centreY, float size, NUIColor color) {
    const float tracking = size * 0.12f;
    std::string upper;
    for (char raw : text) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
    float cursor = 0.0f;
    for (size_t i = 0; i < upper.size(); ++i) {
        // Position from the width of everything before it, so the font's own pair spacing is kept.
        const float before = renderer.measureText(upper.substr(0, i), size).width;
        const std::string glyph(1, upper[i]);
        const float w = renderer.measureText(glyph, size).width;
        const float gx = x + before + tracking * static_cast<float>(i);
        if (upper[i] != ' ') renderer.drawTextCentered(glyph, textRect(gx, centreY, w), size, color);
        cursor = before + w + tracking * static_cast<float>(i + 1);
    }
    return cursor;
}

void drawSectionHeader(NUIRenderer& renderer, float x, float y, float width, const std::string& label) {
    const auto c = emberColors();
    const float tw = drawCaps(renderer, label, x, y + 6.0f, 10.0f, c.inkMuted);
    renderer.fillRect(NUIRect(x + tw + 8.0f, y + 6.0f, std::max(0.0f, width - tw - 8.0f), 1.0f), c.border);
}

void drawWell(NUIRenderer& renderer, const NUIRect& rect) {
    const auto c = emberColors();
    renderer.fillRect(rect, c.well);
    renderer.strokeRect(rect, 1.0f, c.border);
}

void drawEmberKnob(NUIRenderer& renderer, const NUIPoint& cell, KnobSize size, float value, const std::string& label,
                   const std::string& valueText, ControlState state, bool bipolar, bool bypassed) {
    const auto c = emberColors();
    const auto& theme = NUIThemeManager::getInstance().getCurrentTheme();
    const KnobGeometry g = knobGeometry(size);
    const bool lit = state == ControlState::Hover || state == ControlState::Drag;
    const bool disabled = state == ControlState::Disabled;
    const float v = std::clamp(value, 0.0f, 1.0f);
    const NUIPoint ctr(cell.x + g.cell * 0.5f, cell.y + g.disc * 0.5f);
    const float r = g.disc * 0.5f - g.track * 0.5f - 1.0f;
    const float angle = kKnobStart + v * kKnobSweep;

    drawArc(renderer, ctr, r, kKnobStart, kKnobStart + kKnobSweep, g.track, theme.meterBackground); // the sunk ring
    const NUIColor arc = (disabled || bypassed) ? c.inkOff : lit ? c.primaryHover : c.primary;
    if (bipolar) {
        drawArc(renderer, ctr, r, kKnobStart + kKnobSweep * 0.5f, angle, g.track + 1.0f, arc);
        // Detent tick above the arc's top-centre.
        renderer.drawLine({ctr.x, ctr.y - r - g.track * 0.5f - 1.0f}, {ctr.x, ctr.y - r - g.track * 0.5f - 5.0f}, 1.0f,
                          c.inkMuted);
    } else {
        drawArc(renderer, ctr, r, kKnobStart, angle, g.track + 1.0f, arc);
    }

    const float rd = r - g.track * 0.5f - (size == KnobSize::Large ? 5.0f : 4.0f);
    // Design Spec, Knob (locked): a lit cap sitting in the sunk ring, lifting while it is turned.
    Depth::knobCap(renderer, ctr, rd, angle, state == ControlState::Drag, !disabled);

    const float labelSize = size == KnobSize::Small ? 10.0f : 11.0f;
    renderer.drawTextCentered(label, textRect(cell.x, cell.y + g.disc + 13.0f, g.cell, 14.0f), labelSize,
                              lit ? c.ink : c.inkSoft);
    if (state == ControlState::Drag) {
        const float w = std::max(g.cell, renderer.measureText(valueText, 12.0f).width + 16.0f);
        const NUIRect pill(ctr.x - w * 0.5f, cell.y + g.disc + 20.0f, w, 20.0f);
        renderer.fillRect(pill, c.selected);
        renderer.strokeRect(pill, 1.0f, c.primary);
        renderer.drawTextCentered(valueText, pill, 12.0f, c.ink);
    } else {
        renderer.drawTextCentered(valueText, textRect(cell.x - 4.0f, cell.y + g.disc + 29.0f, g.cell + 8.0f, 16.0f),
                                  12.0f, disabled ? c.inkMuted : c.ink);
    }
}

void drawSegmented(NUIRenderer& renderer, const NUIRect& rect, const std::string* labels, int count, int selected,
                   int hovered, bool bypassed, const SegmentGlyph& glyph) {
    if (count <= 0) return;
    const auto c = emberColors();
    renderer.fillRect(rect, c.body);
    const float cellW = rect.width / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        const float x0 = std::round(rect.x + cellW * static_cast<float>(i));
        const float x1 = std::round(rect.x + cellW * static_cast<float>(i + 1));
        const NUIRect cell(x0, rect.y, x1 - x0, rect.height);
        const bool on = i == selected;
        const bool hot = i == hovered;
        if (on) {
            renderer.fillRect(cell, c.selected);
            renderer.fillRect(NUIRect(x0, rect.bottom() - 2.0f, cell.width, 2.0f), bypassed ? c.inkOff : c.primary);
        } else if (hot) {
            renderer.fillRect(cell, c.raised);
        }
        if (i > 0) renderer.fillRect(NUIRect(x0, rect.y + 1.0f, 1.0f, rect.height - 2.0f), c.border);
        const NUIColor ink = (on || hot) ? c.ink : c.inkSoft;
        if (glyph) {
            glyph(renderer, i, cell.x + 10.0f, rect.y + rect.height * 0.5f, ink);
            // 11 px so "Saw Down" fits beside its mark instead of being cut short.
            renderer.drawTextCentered(labels[i], NUIRect(cell.x + 28.0f, cell.y, cell.width - 28.0f, cell.height), 11.0f,
                                      ink);
        } else {
            renderer.drawTextCentered(labels[i], cell, 12.0f, ink);
        }
    }
    renderer.strokeRect(rect, 1.0f, c.edge);
}

void drawToggleButton(NUIRenderer& renderer, const NUIRect& rect, const std::string& label, bool on, bool hovered,
                      bool bypassed) {
    const auto c = emberColors();
    renderer.fillRect(rect, on ? c.selected : hovered ? c.raised : c.body);
    renderer.strokeRect(rect, 1.0f, c.edge);
    const NUIRect mark(rect.x + 8.0f, rect.y + rect.height * 0.5f - 3.0f, 6.0f, 6.0f);
    if (on) renderer.fillRect(mark, bypassed ? c.inkOff : c.primary);
    else renderer.strokeRect(mark, 1.0f, c.inkMuted);
    drawCaps(renderer, label, rect.x + 20.0f, rect.y + rect.height * 0.5f, 11.0f, (on || hovered) ? c.ink : c.inkSoft);
}

void drawPushButton(NUIRenderer& renderer, const NUIRect& rect, const std::string& label, bool hovered) {
    const auto c = emberColors();
    renderer.fillRect(rect, hovered ? c.raised : c.body);
    renderer.strokeRect(rect, 1.0f, c.edge);
    renderer.drawTextCentered(label, rect, 12.0f, hovered ? c.ink : c.inkSoft);
}

EmberSliderGeometry emberSliderGeometry(const NUIRect& rect, float labelWidth) {
    return {rect.x + labelWidth + 8.0f, rect.right() - 72.0f};
}

void drawEmberSlider(NUIRenderer& renderer, const NUIRect& rect, float labelWidth, const std::string& label,
                     float value, const std::string& valueText, ControlState state, bool bypassed) {
    const auto c = emberColors();
    const bool lit = state == ControlState::Hover || state == ControlState::Drag;
    const float v = std::clamp(value, 0.0f, 1.0f);
    const float cy = rect.y + rect.height * 0.5f;
    const EmberSliderGeometry g = emberSliderGeometry(rect, labelWidth);
    const float tx = g.x0 + (g.x1 - g.x0) * v;

    drawTextLeft(renderer, label, rect.x, cy, 11.0f, lit ? c.ink : c.inkSoft);
    // Design Spec, Slider (locked): a sunk slot, the travel in primary, the fader cap on its side.
    (void)tx;
    const bool live = !bypassed && state != ControlState::Disabled;
    Depth::sliderSlot(renderer, NUIRect(g.x0, cy - 3.0f, g.x1 - g.x0, 6.0f), v, 0.0f, live);
    Depth::sliderCap(renderer, {g.x0 + (g.x1 - g.x0) * v, cy}, state == ControlState::Drag);
    if (state == ControlState::Drag) {
        const float w = renderer.measureText(valueText, 12.0f).width + 16.0f;
        const NUIRect pill(rect.right() - w, cy - 10.0f, w, 20.0f);
        renderer.fillRect(pill, c.selected);
        renderer.strokeRect(pill, 1.0f, c.primary);
        renderer.drawTextCentered(valueText, pill, 12.0f, c.ink);
    } else {
        drawTextRight(renderer, valueText, rect.right(), cy, 12.0f, c.ink);
    }
}

float logFrequencyX(float hz, float x, float width, float lo, float hi) {
    return x + width * std::log(hz / lo) / std::log(hi / lo);
}

void drawFrequencyGrid(NUIRenderer& renderer, const NUIRect& plot) {
    const auto c = emberColors();
    static const float kLines[] = {20,   30,   40,   50,   60,   70,   80,   90,    100,   200,   300,  400,  500,
                                   600,  700,  800,  900,  1000, 2000, 3000, 4000,  5000,  6000,  7000, 8000, 9000,
                                   10000, 20000};
    for (float f : kLines) {
        const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
        const float x = std::round(logFrequencyX(f, plot.x, plot.width));
        renderer.fillRect(NUIRect(x, plot.y + 1.0f, 1.0f, plot.height - 2.0f), major ? c.gridMajor : c.gridMinor);
    }
    struct Label { float hz; const char* text; };
    static const Label kLabels[] = {{20, "20"}, {100, "100"}, {1000, "1k"}, {10000, "10k"}, {20000, "20k"}};
    for (size_t i = 0; i < 5; ++i) {
        const float x = std::clamp(logFrequencyX(kLabels[i].hz, plot.x, plot.width), plot.x + 4.0f, plot.right() - 4.0f);
        const float y = plot.bottom() - 8.0f;
        if (i == 0) drawTextLeft(renderer, kLabels[i].text, x, y, 9.0f, c.inkMuted);
        else if (i == 4) drawTextRight(renderer, kLabels[i].text, x, y, 9.0f, c.inkMuted);
        else {
            const float w = renderer.measureText(kLabels[i].text, 9.0f).width;
            drawTextLeft(renderer, kLabels[i].text, x - w * 0.5f, y, 9.0f, c.inkMuted);
        }
    }
}

void drawVerticalMeter(NUIRenderer& renderer, const NUIRect& rect, float db, MeterKind kind, bool bypassed) {
    const auto c = emberColors();
    drawWell(renderer, rect);
    const float inner = rect.height - 2.0f;
    if (kind == MeterKind::Level) {
        constexpr float kLo = -48.0f;
        const float f = std::clamp((db - kLo) / -kLo, 0.0f, 1.0f);
        const float bottom = rect.bottom() - 1.0f;
        const float top = bottom - inner * f;
        const float warnY = bottom - inner * ((-6.0f - kLo) / -kLo);
        const float bodyTop = std::max(top, warnY);
        renderer.fillRect(NUIRect(rect.x + 1.0f, bodyTop, rect.width - 2.0f, bottom - bodyTop),
                          bypassed ? c.inkOff : c.meter);
        if (top < warnY)
            renderer.fillRect(NUIRect(rect.x + 1.0f, top, rect.width - 2.0f, warnY - top), bypassed ? c.inkOff : c.warn);
        renderer.fillRect(NUIRect(rect.x + 1.0f, top - 2.0f, rect.width - 2.0f, 2.0f), c.ink);
    } else {
        const float f = std::clamp(-db / 24.0f, 0.0f, 1.0f);
        renderer.fillRect(NUIRect(rect.x + 1.0f, rect.y + 1.0f, rect.width - 2.0f, inner * f),
                          bypassed ? c.inkOff : c.warn);
    }
}

} // namespace EditorKit
} // namespace AestraUI
