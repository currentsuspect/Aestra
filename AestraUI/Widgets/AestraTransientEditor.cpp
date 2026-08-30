// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraTransientEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraTransient.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraTransient;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kKnobSweep = kPi * 1.5f;                      // 270° total (135° each side of center)
constexpr float kKnobStart = kPi * 0.75f;                     // 7 o'clock
constexpr float kKnobCenter = kKnobStart + kKnobSweep * 0.5f; // 12 o'clock = neutral
constexpr float kDragRangePx = 160.0f;

NUIColor accent() {
    return NUIColor(0.32f, 0.74f, 0.92f, 1.0f); // transient blue
}
NUIColor panelSurface() {
    return editorNeutral(0.027f, 0.96f);
}
NUIColor insetSurface() {
    return editorNeutral(0.038f, 0.96f);
}
NUIColor sketchSurface() {
    return editorNeutral(0.044f, 0.96f);
}

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color) {
    if (endAngle - startAngle <= 0.001f) {
        return;
    }
    std::array<NUIPoint, 49> pts{};
    const float div = static_cast<float>(pts.size() - 1);
    for (size_t i = 0; i < pts.size(); ++i) {
        const float t = static_cast<float>(i) / div;
        const float a = startAngle + (endAngle - startAngle) * t;
        pts[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()), thickness, color);
}
} // namespace

AestraTransientEditor::AestraTransientEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraTransientEditor");
    setPanelTitle("Aestra Transient");
    setBadgeText("Transient Shaper");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraTransientEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraTransientEditor::layoutControls() {
    const auto b = getBounds();
    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;

    // Bypass pill in the row immediately below the title bar — the only
    // spot in the editor that does NOT overlap a control. Sustain starts
    // at contentTop + 16, the Mix slider lives in the bottom band, so a
    // thin pill between them has no collision. The AestraPanelWindow
    // drawContent clip starts at contentTop (= b.y + 32), so the pill
    // sits inside the clip rect.
    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 18.0f;
    const float bypassY = b.y + AestraPanelWindow::TITLE_BAR_H + 2.0f;
    m_bypassRect = NUIRect(b.right() - 18.0f - kBypassW, bypassY, kBypassW, kBypassH);

    // Two large bipolar knobs (Attack left, Sustain right) flanking the
    // envelope sketch in the middle.
    const float knobRow = contentTop + 16.0f;
    constexpr float kKnobSize = 130.0f;
    m_attackRect = NUIRect(b.x + 28.0f, knobRow, kKnobSize, kKnobSize);
    m_sustainRect = NUIRect(b.right() - 28.0f - kKnobSize, knobRow, kKnobSize, kKnobSize);

    // Envelope sketch sits between the knobs, at the same height so its
    // top/bottom edges align with the bipolar knob wells.
    const float sketchLeft = m_attackRect.right() + 18.0f;
    const float sketchRight = m_sustainRect.x - 18.0f;
    m_sketchRect = NUIRect(sketchLeft, knobRow + 6.0f, sketchRight - sketchLeft, kKnobSize - 12.0f);

    // Output knob and Mix slider share a bottom band; the band starts below
    // the bipolar knobs' label/value/range captions so nothing overlaps.
    // Captions run to bottom + 4 + 14 + 2 + 16 + 2 + 12 = bottom + 50.
    const float captionBottom = m_attackRect.bottom() + 50.0f;
    const float bottomRow = captionBottom + 12.0f;
    constexpr float kOutSize = 64.0f;
    m_outputRect = NUIRect(b.x + 30.0f, bottomRow, kOutSize, kOutSize);
    m_mixRect =
        NUIRect(m_outputRect.right() + 30.0f, bottomRow + 18.0f, b.right() - m_outputRect.right() - 60.0f, 28.0f);
}

void AestraTransientEditor::onResize(int /*width*/, int /*height*/) {
    layoutControls();
}

void AestraTransientEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance) {
        return;
    }

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f, contentRect.width - 24.0f,
                           contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, editorInk(0.055f));

    drawBipolarKnob(renderer, m_attackRect, AestraTransient::kAttack, "Attack", "±100%");
    drawBipolarKnob(renderer, m_sustainRect, AestraTransient::kSustain, "Sustain", "±100%");
    drawUnipolarKnob(renderer, m_outputRect, AestraTransient::kOutput, "Output", "−12…+12 dB");
    drawEnvelopeSketch(renderer, m_sketchRect);
    drawMixSlider(renderer);
    drawBypassPill(renderer);
}

void AestraTransientEditor::drawBipolarKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId,
                                            const char* label, const char* rangeText) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance->getParameter(paramId);
    // value is in [0,1] where 0.5 is neutral; convert to bipolar [-1, 1].
    const float bipolar = AestraTransient::bipolarFromNorm(value);

    const NUIPoint c = rect.center();
    const float r = rect.width * 0.5f - 4.0f;

    renderer.fillCircle(c, r + 4.0f, insetSurface());
    renderer.strokeCircle(c, r + 4.0f, 1.0f, editorInk(0.060f));

    // Backplate arc: full bidirectional sweep, dimmer than the active fill.
    drawArc(renderer, c, r - 3.0f, kKnobStart, kKnobStart + kKnobSweep, 4.0f, editorNeutral(0.199f, 1.0f));

    // Active fill: from the centre detent out toward the current side.
    // The fill colour itself flips with sign (blue right, amber left) so
    // "boost" and "cut" read at a glance without staring at the value.
    const float halfSweep = kKnobSweep * 0.5f;
    if (bipolar >= 0.0f) {
        drawArc(renderer, c, r - 3.0f, kKnobCenter, kKnobCenter + halfSweep * std::min(bipolar, 1.0f), 4.0f,
                accent().withAlpha(0.95f));
    } else {
        const NUIColor cutAccent(0.94f, 0.62f, 0.30f, 0.95f);
        drawArc(renderer, c, r - 3.0f, kKnobCenter, kKnobCenter - halfSweep * std::min(-bipolar, 1.0f), 4.0f,
                cutAccent);
    }

    // Centre detent marker: a short white tick at 12 o'clock.
    const float tickInner = r - 9.0f;
    const float tickOuter = r - 3.0f;
    const NUIPoint tickIn{c.x + std::cos(kKnobCenter) * tickInner, c.y + std::sin(kKnobCenter) * tickInner};
    const NUIPoint tickOut{c.x + std::cos(kKnobCenter) * tickOuter, c.y + std::sin(kKnobCenter) * tickOuter};
    renderer.drawLine(tickIn, tickOut, 2.0f, theme.getColor("textPrimary").withAlpha(0.85f));

    // Needle always points from 12 o'clock; the fill arc conveys the value.
    const float needleLen = r - 16.0f;
    const float needleAngle =
        kKnobCenter + (bipolar >= 0.0f ? 1.0f : -1.0f) * halfSweep * std::min(std::abs(bipolar), 1.0f);
    const NUIPoint tip(c.x + std::cos(needleAngle) * needleLen, c.y + std::sin(needleAngle) * needleLen);
    renderer.drawLine(c, tip, 2.0f, theme.getColor("textPrimary").withAlpha(0.85f));
    renderer.fillCircle(tip, 3.0f, theme.getColor("textPrimary").withAlpha(0.95f));

    // Inner well, mirrored from Sat.
    const float wellR = r * 0.32f;
    renderer.fillCircle(c, wellR, editorNeutral(0.045f, 0.96f));
    renderer.strokeCircle(c, wellR, 1.0f, accent().withAlpha(0.30f));

    // Label band beneath the knob: name, then signed value (e.g. "+12%", "0%", "−100%").
    const NUIRect labelRect(rect.x, rect.bottom() + 4.0f, rect.width, 14.0f);
    renderer.drawTextCentered(label, labelRect, 11.0f, theme.getColor("textPrimary").withAlpha(0.95f));
    const std::string valStr = m_instance->getParameterDisplay(paramId);
    const NUIRect valRect(rect.x, rect.bottom() + 20.0f, rect.width, 16.0f);
    renderer.drawTextCentered(valStr, valRect, 12.5f, accent().withAlpha(0.96f));
    const NUIRect rangeRect(rect.x, rect.bottom() + 38.0f, rect.width, 12.0f);
    renderer.drawTextCentered(rangeText, rangeRect, 9.5f, theme.getColor("textSecondary").withAlpha(0.55f));
}

void AestraTransientEditor::drawUnipolarKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId,
                                             const char* label, const char* rangeText) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance->getParameter(paramId);
    const NUIPoint c = rect.center();
    const float r = rect.width * 0.5f - 3.0f;

    renderer.fillCircle(c, r + 3.0f, insetSurface());
    renderer.strokeCircle(c, r + 3.0f, 1.0f, editorInk(0.055f));

    // Output trims a bipolar range but is presented as a single sweep with
    // 12 o'clock = 0 dB, the conventional "trim" affordance. Sweep midpoint
    // sits at kPi (180° + 3π/4 in screen-coords y-down) so a normalized
    // value of 0.5 (i.e. unity gain) renders at 12 o'clock.
    const float sweepStart = kPi * 0.75f; // 135° (7 o'clock)
    const float sweepEnd = kPi * 2.25f;   // 405° / 45° (1 o'clock)
    drawArc(renderer, c, r - 2.0f, sweepStart, sweepEnd, 3.0f, editorNeutral(0.199f, 1.0f));
    const float fillEnd = sweepStart + (sweepEnd - sweepStart) * value;
    drawArc(renderer, c, r - 2.0f, sweepStart, fillEnd, 3.0f, accent().withAlpha(0.92f));

    const float needleLen = r - 8.0f;
    const float angle = sweepStart + (sweepEnd - sweepStart) * value;
    const NUIPoint tip(c.x + std::cos(angle) * needleLen, c.y + std::sin(angle) * needleLen);
    renderer.drawLine(c, tip, 1.8f, accent().withAlpha(0.85f));
    renderer.fillCircle(tip, 2.6f, accent());

    const NUIRect labelRect(rect.x, rect.bottom() + 4.0f, rect.width, 13.0f);
    renderer.drawTextCentered(label, labelRect, 10.5f, theme.getColor("textPrimary").withAlpha(0.95f));
    const std::string valStr = m_instance->getParameterDisplay(paramId);
    const NUIRect valRect(rect.x, rect.bottom() + 19.0f, rect.width, 14.0f);
    renderer.drawTextCentered(valStr, valRect, 11.5f, accent().withAlpha(0.92f));
    const NUIRect rangeRect(rect.x, rect.bottom() + 35.0f, rect.width, 11.0f);
    renderer.drawTextCentered(rangeText, rangeRect, 9.0f, theme.getColor("textSecondary").withAlpha(0.50f));
}

void AestraTransientEditor::drawEnvelopeSketch(NUIRenderer& renderer, const NUIRect& rect) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(rect, 10.0f, sketchSurface());
    renderer.strokeRoundedRect(rect, 10.0f, 1.0f, editorInk(0.060f));

    // Two envelope halves derived from the current settings: attack portion
    // rises and is amplified by Attack amount; decay portion falls and is
    // amplified by Sustain amount. The sketch updates live as the user
    // turns the knobs, so the surface "shows" what the plugin will do without
    // any audio-thread tap.
    const float attackAmt = AestraTransient::bipolarFromNorm(m_instance->getParameter(AestraTransient::kAttack));
    const float sustainAmt = AestraTransient::bipolarFromNorm(m_instance->getParameter(AestraTransient::kSustain));

    const float padX = 18.0f;
    const float padY = 16.0f;
    const NUIRect plotRect{rect.x + padX, rect.y + padY, rect.width - padX * 2, rect.height - padY * 2};
    // Centre baseline (zero gain line)
    const float cy = plotRect.y + plotRect.height * 0.5f;
    renderer.drawLine({plotRect.x, cy}, {plotRect.right(), cy}, 1.0f, editorInk(0.05f));

    constexpr int kSamples = 96;
    std::array<NUIPoint, kSamples> polyline{};
    const float attackEnd = 0.18f; // first 18% of the plot is the attack portion
    // Y-extent of the unboosted peak as a fraction of the plot height, so
    // the curve uses the real rect rather than a 0.85-pixel line.
    const float peak = plotRect.height * 0.35f;
    const float ampA = attackAmt; // −1…+1
    const float ampS = sustainAmt;

    for (int i = 0; i < kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
        // Reference envelope: a peak in the first slice, exponential decay after.
        float env;
        if (t < attackEnd) {
            const float u = t / attackEnd;
            // Fast attack: rising from 0 to 1 across the attack slice
            env = 1.0f - (1.0f - u) * (1.0f - u); // ease-out
        } else {
            const float u = (t - attackEnd) / (1.0f - attackEnd);
            env = std::exp(-u * 3.0f);
        }
        // Apply bipolar shaping: positive in the attack region, negative in the decay region
        float shaped;
        if (t < attackEnd) {
            shaped = env * (1.0f + ampA * 0.7f);
        } else {
            shaped = env * (1.0f + ampS * 0.7f);
        }
        const float x = plotRect.x + plotRect.width * t;
        const float y = cy - shaped * peak;
        polyline[i] = {x, y};
    }
    renderer.drawPolyline(polyline.data(), kSamples, 2.0f, accent().withAlpha(0.92f));

    // Centre marker
    const float cx = plotRect.x + plotRect.width * attackEnd;
    renderer.drawLine({cx, plotRect.y}, {cx, plotRect.bottom()}, 1.0f, editorInk(0.10f));
    renderer.drawTextCentered("attack",
                              NUIRect(plotRect.x, plotRect.bottom() - 14.0f, plotRect.width * attackEnd, 12.0f), 9.0f,
                              theme.getColor("textSecondary").withAlpha(0.55f));
    renderer.drawTextCentered("sustain", NUIRect(cx, plotRect.bottom() - 14.0f, plotRect.right() - cx, 12.0f), 9.0f,
                              theme.getColor("textSecondary").withAlpha(0.55f));
}

void AestraTransientEditor::drawMixSlider(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance->getParameter(kMix);
    renderer.fillRoundedRect(m_mixRect, 6.0f, insetSurface());
    renderer.strokeRoundedRect(m_mixRect, 6.0f, 1.0f, editorInk(0.050f));
    // Fill
    NUIRect fill{m_mixRect.x, m_mixRect.y, m_mixRect.width * value, m_mixRect.height};
    renderer.fillRoundedRect(fill, 6.0f, accent().withAlpha(0.45f));
    // Thumb
    const float thumbX = m_mixRect.x + m_mixRect.width * value - 6.0f;
    renderer.fillRoundedRect(NUIRect(thumbX, m_mixRect.y - 4.0f, 12.0f, m_mixRect.height + 8.0f), 4.0f,
                             accent().withAlpha(0.95f));

    const std::string label = m_instance->getParameterDisplay(kMix);
    renderer.drawTextCentered("Mix", NUIRect(m_mixRect.x, m_mixRect.y - 18.0f, 40.0f, 14.0f), 10.5f,
                              theme.getColor("textPrimary").withAlpha(0.95f));
    renderer.drawTextCentered(label, NUIRect(m_mixRect.right() - 60.0f, m_mixRect.y - 18.0f, 60.0f, 14.0f), 10.5f,
                              accent().withAlpha(0.95f));
}

void AestraTransientEditor::drawBypassPill(NUIRenderer& renderer) {
    if (!m_instance) {
        return;
    }
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance->getParameter(kBypass);
    const bool on = value > 0.5f;
    const NUIColor bg = on ? NUIColor(0.18f, 0.20f, 0.22f, 0.95f) : editorNeutral(0.18f, 0.55f);
    renderer.fillRoundedRect(m_bypassRect, 13.0f, bg);
    renderer.strokeRoundedRect(m_bypassRect, 13.0f, 1.0f, editorInk(0.080f));
    const NUIColor text = on ? NUIColor(0.78f, 0.86f, 0.94f, 1.0f) : theme.getColor("textPrimary").withAlpha(0.78f);
    renderer.drawTextCentered(on ? "Bypassed" : "Active", m_bypassRect, 10.5f, text);
}

int AestraTransientEditor::knobAtPoint(const NUIPoint& p) const {
    if (m_attackRect.contains(p)) {
        return 0;
    }
    if (m_sustainRect.contains(p)) {
        return 1;
    }
    if (m_outputRect.contains(p)) {
        return 2;
    }
    return -1;
}

bool AestraTransientEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }
    if (!m_instance) {
        return false;
    }

    // Bypass pill: click toggles
    if (bypassContains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(kBypass);
        m_instance->setParameter(kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }
    m_bypassHovered = bypassContains(event.position);

    if (m_dragging >= 0 && m_dragging < 3) {
        if (event.released) {
            endKnobCapture();
            m_dragging = -1;
            return true;
        }
        if (event.button == NUIMouseButton::None) {
            // Service-owned frame delta for the knob drag
            // (AestraPanelWindow::knobDragStep, same path Sat uses).
            const uint32_t paramId = static_cast<uint32_t>(m_dragging);
            const float step = knobDragStep(event, kDragRangePx);
            const float cur = m_instance->getParameter(paramId);
            m_instance->setParameter(paramId, std::clamp(cur + step, 0.0f, 1.0f));
            setDirty();
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int hit = knobAtPoint(event.position);
        if (hit >= 0) {
            m_dragging = hit;
            const NUIRect& rect = hit == 0 ? m_attackRect : (hit == 1 ? m_sustainRect : m_outputRect);
            beginKnobCapture({rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f}, event.position);
            return true;
        }
        if (mixContains(event.position)) {
            m_dragging = 3; // mix
            // Jump the value to the press position so a click anywhere on
            // the track selects, not just a drag from the current thumb.
            const float t = std::clamp((event.position.x - m_mixRect.x) / m_mixRect.width, 0.0f, 1.0f);
            m_instance->setParameter(kMix, t);
            setDirty();
            return true;
        }
    }

    // Mix horizontal drag: uses the live cursor x, not the platform's
    // capture-delta (the horizontal slider does NOT use cursor capture —
    // that pattern is for knob vertical-drag; capturing here would feel
    // weird because the cursor moves laterally). Release resets the
    // drag state so the next press starts fresh.
    if (m_dragging == 3) {
        if (event.released) {
            m_dragging = -1;
            return true;
        }
        if (!event.pressed) {
            const float t = std::clamp((event.position.x - m_mixRect.x) / m_mixRect.width, 0.0f, 1.0f);
            m_instance->setParameter(kMix, t);
            setDirty();
            return true;
        }
    }

    return consumeInsideBounds(event);
}

} // namespace AestraUI
