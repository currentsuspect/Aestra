// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLimitEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraLimit.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;

NUIColor accentColor() { return NUIColor(0.95f, 0.60f, 0.12f, 1.0f); }
NUIColor bgDark() { return NUIColor(0.018f, 0.020f, 0.024f, 0.96f); }
NUIColor textDim() { return NUIColor(1.0f, 1.0f, 1.0f, 0.30f); }
NUIColor textBright() { return NUIColor(1.0f, 1.0f, 1.0f, 0.88f); }
NUIColor meterBg() { return NUIColor(0.008f, 0.008f, 0.012f, 1.0f); }

float smoothMeter(float current, float target, float attack, float release) {
    const float coeff = target > current ? attack : release;
    return current + (target - current) * coeff;
}

float levelToNorm(float linear) {
    const float db = linear > 1.0e-8f ? 20.0f * std::log10(linear) : -60.0f;
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}
} // namespace

AestraLimitEditor::AestraLimitEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraLimitEditor");
    setPanelTitle("Aestra Limit");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);

    buildControls();
}

void AestraLimitEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
    for (auto& control : m_controls) {
        if (control.slider) control.slider->setPlatformBridge(bridge);
    }
}

void AestraLimitEditor::buildControls() {
    m_controls.clear();
    if (!m_instance) return;

    using Limit = Aestra::Audio::Plugins::AestraLimit;
    struct Meta { const char* label; uint32_t id; bool primary; };
    const Meta controls[] = {
        {"Ceiling", Limit::kCeiling, true},
        {"Release", Limit::kRelease, false},
    };

    for (const auto& item : controls) {
        KnobControl control;
        control.label = item.label;
        control.paramId = item.id;
        control.isPrimary = item.primary;

        auto slider = std::make_shared<NUISlider>();
        slider->setStyle(NUISlider::Style::Rotary);
        slider->setRange(0.0, 1.0);
        slider->setValue(std::clamp(m_instance->getParameter(item.id), 0.0f, 1.0f));
        slider->setPlatformBridge(getPlatformBridge());
        slider->setOnValueChange([this, paramId = item.id](double value) {
            if (m_instance) {
                m_instance->setParameter(paramId, static_cast<float>(std::clamp(value, 0.0, 1.0)));
                repaint();
            }
        });

        control.slider = slider;
        addChild(slider);
        m_controls.push_back(control);
    }

    layoutControls();
}

void AestraLimitEditor::layoutControls() {
    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }

    const float cx = b.x + kPad;
    const float cw = b.width - kPad * 2.0f;
    const float titleH = AestraPanelWindow::TITLE_BAR_H;
    const float y0 = b.y + titleH + 8.0f;

    // GR Meter (left 60% of width)
    const float grW = cw * 0.58f;
    const float grH = 120.0f;
    m_grBarRect = NUIRect(cx, y0, grW, grH);
    m_grLabelRect = NUIRect(cx, y0 + grH + 2.0f, grW, 16.0f);

    // Ceiling knob (right 40%)
    const float ceilX = cx + grW + 12.0f;
    const float ceilW = cw - grW - 12.0f;
    m_ceilingKnobRect = NUIRect(ceilX, y0, ceilW, grH + 18.0f);
    if (!m_controls.empty() && m_controls[0].slider) {
        const float knobX = ceilX + (ceilW - kKnobSizePrimary) * 0.5f;
        const float knobY = y0 + 8.0f;
        m_controls[0].bounds = NUIRect(ceilX, y0, ceilW, grH + 18.0f);
        m_controls[0].slider->setBounds(NUIRect(knobX, knobY, kKnobSizePrimary, kKnobSizePrimary));
    }

    // Bottom row: Release knob, Auto/Manual pill, Bypass
    const float rowY = m_grLabelRect.bottom() + 14.0f;
    const float knobW = 80.0f;

    // Release knob (left)
    m_releaseKnobRect = NUIRect(cx, rowY, knobW, 90.0f);
    if (m_controls.size() > 1 && m_controls[1].slider) {
        m_controls[1].bounds = NUIRect(cx, rowY, knobW, 90.0f);
        const float kx = cx + (knobW - kKnobSizeSecondary) * 0.5f;
        const float ky = rowY + 12.0f;
        m_controls[1].slider->setBounds(NUIRect(kx, ky, kKnobSizeSecondary, kKnobSizeSecondary));
    }

    // Auto/Manual pills (center)
    const float pillY = rowY + 28.0f;
    const float pillGap = 6.0f;
    const float pillX = cx + knobW + 16.0f;
    m_autoPillRect = NUIRect(pillX, pillY, kPillW, kPillH);
    m_manualPillRect = NUIRect(pillX + kPillW + pillGap, pillY, kPillW, kPillH);

    // Bypass pill (right of release mode pills)
    m_bypassPillRect = NUIRect(m_manualPillRect.right() + 16.0f, pillY, kPillW, kPillH);

    // Bottom meter bar: IN / OUT / GR
    const float meterY = m_releaseKnobRect.bottom() + 10.0f;
    const float meterH = 26.0f;
    const float thirdW = (cw - 8.0f) / 3.0f;
    m_inMeterRect = NUIRect(cx, meterY, thirdW, meterH);
    m_outMeterRect = NUIRect(cx + thirdW + 4.0f, meterY, thirdW, meterH);
    m_grNumRect = NUIRect(cx + (thirdW + 4.0f) * 2.0f, meterY, thirdW, meterH);
}

void AestraLimitEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraLimitEditor::syncControlsFromPlugin() {
    if (!m_instance) return;
    for (auto& control : m_controls) {
        if (control.slider)
            control.slider->setValue(std::clamp(m_instance->getParameter(control.paramId), 0.0f, 1.0f));
    }
}

void AestraLimitEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;

    syncControlsFromPlugin();
    if (auto limit = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraLimit>(m_instance)) {
        const float gr = std::clamp(limit->getGainReductionDb(), 0.0f, 48.0f);
        const float in = std::clamp(limit->getInputLevel(), 0.0f, 16.0f);
        const float out = std::clamp(limit->getOutputLevel(), 0.0f, 16.0f);
        m_grDisplayDb = smoothMeter(m_grDisplayDb, gr, 0.60f, 0.15f);
        m_inputDisplay = smoothMeter(m_inputDisplay, in, 0.55f, 0.16f);
        m_outputDisplay = smoothMeter(m_outputDisplay, out, 0.55f, 0.16f);
    }
    repaint();
}

std::string AestraLimitEditor::valueText(uint32_t paramId) const {
    if (!m_instance) return {};
    return m_instance->getParameterDisplay(paramId);
}

void AestraLimitEditor::drawGrMeter(NUIRenderer& renderer) {
    const auto& b = m_grBarRect;
    const NUIColor accent = accentColor();

    // Background
    renderer.fillRoundedRect(b, 6.0f, meterBg());
    renderer.strokeRoundedRect(b, 6.0f, 1.0f, NUIColor(1, 1, 1, 0.06f));

    // Fill bar (right-to-left for GR)
    const float grNorm = std::clamp(m_grDisplayDb / 24.0f, 0.0f, 1.0f);
    if (grNorm > 0.001f) {
        const float fillW = b.width * grNorm;
        const NUIRect fillRect(b.x + b.width - fillW, b.y, fillW, b.height);

        // Gradient: dark amber at low GR, bright yellow-red at high GR
        NUIColor fillColor = accent;
        if (grNorm > 0.5f) {
            fillColor = NUIColor(0.92f, 0.30f, 0.18f, 0.88f);
        } else if (grNorm > 0.25f) {
            fillColor = NUIColor(0.95f, 0.60f, 0.12f, 0.88f);
        }
        renderer.fillRoundedRect(fillRect, 6.0f, fillColor);

        // Top specular
        renderer.drawLine({fillRect.x + 4.0f, fillRect.y + 1.0f},
                          {fillRect.right() - 4.0f, fillRect.y + 1.0f},
                          1.0f, NUIColor(1, 1, 1, 0.15f));
    }

    // GR label
    renderer.drawText("GAIN REDUCTION", {b.x + 10.0f, b.y + 6.0f}, 9.0f, textDim());

    // GR numeric readout
    char grBuf[32]{};
    std::snprintf(grBuf, sizeof(grBuf), "-%.1f dB", m_grDisplayDb);
    const float grTextW = renderer.measureText(grBuf, 28.0f).width;
    renderer.drawText(grBuf, {b.right() - grTextW - 12.0f, b.y + 6.0f}, 28.0f,
                      m_grDisplayDb > 6.0f ? NUIColor(0.96f, 0.35f, 0.22f, 0.95f)
                                           : accent.withAlpha(0.92f));

    // Reduction scale ticks
    constexpr int kTicks = 6;
    for (int i = 0; i <= kTicks; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kTicks);
        const float x = b.x + t * b.width;
        const float tickH = (i == 0 || i == kTicks) ? 6.0f : 4.0f;
        renderer.drawLine({x, b.bottom() - tickH}, {x, b.bottom()}, 1.0f, NUIColor(1, 1, 1, 0.10f));

        if (i < kTicks && i > 0) {
            char tickBuf[8]{};
            std::snprintf(tickBuf, sizeof(tickBuf), "%d", static_cast<int>(24.0f * (1.0f - t)));
            renderer.drawTextCentered(tickBuf, {x - 12.0f, b.bottom() + 2.0f, 24.0f, 12.0f},
                                      7.0f, textDim());
        }
    }
}

void AestraLimitEditor::drawKnob(NUIRenderer& renderer, const KnobControl& control) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = control.slider ? static_cast<float>(control.slider->getValue()) : 0.0f;
    const NUIColor accent = accentColor();
    const NUIColor knobAccent = control.isPrimary ? accent : accent.withAlpha(0.72f);

    // Cell background
    renderer.fillRoundedRect(control.bounds, 8.0f, NUIColor(0.035f, 0.035f, 0.040f, 0.96f));
    renderer.strokeRoundedRect(control.bounds, 8.0f, 1.0f,
                               control.isPrimary ? NUIColor(0.28f, 0.18f, 0.06f, 1.0f)
                                                 : NUIColor(0.118f, 0.118f, 0.133f, 1.0f));

    const NUIRect knobRect = control.slider ? control.slider->getBounds() : NUIRect();
    const float cx = knobRect.center().x;
    const float cy = knobRect.center().y;
    const float r = std::min(knobRect.width, knobRect.height) * 0.35f;

    // Glow
    renderer.fillCircle({cx, cy}, r + 6.0f, knobAccent.withAlpha(0.06f));

    // Body
    renderer.fillCircle({cx, cy}, r, bgDark());
    renderer.strokeCircle({cx, cy}, r + 2.5f, 1.5f, NUIColor(1, 1, 1, 0.07f));

    // Arc
    const float start = kPi * 0.75f;
    const float sweep = kPi * 1.5f * value;
    std::array<NUIPoint, 32> arc{};
    for (size_t i = 0; i < arc.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(arc.size() - 1);
        const float a = start + sweep * t;
        arc[i] = {cx + std::cos(a) * (r + 4.0f), cy + std::sin(a) * (r + 4.0f)};
    }
    renderer.drawPolyline(arc.data(), static_cast<int>(arc.size()),
                          control.isPrimary ? 2.8f : 2.2f, knobAccent.withAlpha(0.90f));

    // Pointer
    const float pa = start + sweep;
    const float pointerLen = r - (control.isPrimary ? 4.0f : 3.0f);
    renderer.drawLine({cx, cy}, {cx + std::cos(pa) * pointerLen, cy + std::sin(pa) * pointerLen},
                      control.isPrimary ? 1.8f : 1.5f, theme.getColor("textPrimary").withAlpha(0.82f));

    // Label
    renderer.drawTextCentered(control.label,
                              {control.bounds.x, control.bounds.y + 4.0f, control.bounds.width, 12.0f},
                              9.0f, NUIColor(1, 1, 1, 0.50f));

    // Value
    const float valY = control.isPrimary
        ? control.bounds.bottom() - 18.0f
        : knobRect.bottom() + 4.0f;
    const NUIColor valColor = control.isPrimary
        ? NUIColor(0.85f, 0.65f, 0.20f, 1.0f)
        : NUIColor(1, 1, 1, 0.85f);
    renderer.drawTextCentered(valueText(control.paramId),
                              {control.bounds.x, valY, control.bounds.width, 14.0f},
                              11.0f, valColor);
}

void AestraLimitEditor::drawPill(NUIRenderer& renderer, NUIRect rect,
                                  bool selected, bool hovered,
                                  const char* label, NUIColor accent) {
    if (selected) {
        renderer.fillRoundedRect({rect.x - 2.0f, rect.y - 2.0f, rect.width + 4.0f, rect.height + 4.0f},
                                 7.0f, accent.withAlpha(0.12f));
        renderer.fillRoundedRect(rect, 5.0f, accent.withAlpha(0.75f));
        renderer.drawLine({rect.x + 5.0f, rect.y + 1.0f},
                          {rect.x + rect.width - 5.0f, rect.y + 1.0f},
                          1.0f, NUIColor(1, 1, 1, 0.18f));
        renderer.strokeRoundedRect(rect, 5.0f, 1.0f, accent);
        renderer.drawTextCentered(label, rect, 9.5f, NUIColor(1, 1, 1, 0.97f));
    } else {
        const NUIColor bg = hovered ? NUIColor(0.04f, 0.04f, 0.048f, 0.90f)
                                    : NUIColor(0.022f, 0.022f, 0.028f, 0.90f);
        renderer.fillRoundedRect(rect, 5.0f, bg);
        renderer.drawLine({rect.x + 5.0f, rect.y + 1.0f},
                          {rect.x + rect.width - 5.0f, rect.y + 1.0f},
                          0.5f, NUIColor(0, 0, 0, 0.25f));
        const NUIColor border = hovered ? NUIColor(1, 1, 1, 0.16f) : NUIColor(1, 1, 1, 0.08f);
        renderer.strokeRoundedRect(rect, 5.0f, 1.0f, border);
        renderer.drawTextCentered(label, rect, 9.0f,
                                  hovered ? NUIColor(1, 1, 1, 0.72f) : NUIColor(1, 1, 1, 0.40f));
    }
}

void AestraLimitEditor::drawMeterBar(NUIRenderer& renderer, NUIRect rect,
                                      float norm, const char* label,
                                      const char* value, NUIColor color) {
    renderer.fillRoundedRect(rect, 5.0f, meterBg());
    renderer.strokeRoundedRect(rect, 5.0f, 1.0f, NUIColor(1, 1, 1, 0.06f));

    if (norm > 0.001f) {
        renderer.fillRoundedRect({rect.x, rect.y, rect.width * norm, rect.height},
                                 5.0f, color.withAlpha(0.85f));
    }

    const float textY = rect.y + (rect.height - 8.0f) * 0.5f;
    renderer.drawText(label, {rect.x + 8.0f, textY}, 8.0f, textDim());

    const NUISize valSize = renderer.measureText(value, 8.0f);
    renderer.drawText(value, {rect.right() - valSize.width - 10.0f, textY}, 8.0f, textBright());
}

void AestraLimitEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor accent = accentColor();

    // GR Meter
    drawGrMeter(renderer);

    // Ceiling knob
    if (!m_controls.empty()) drawKnob(renderer, m_controls[0]);

    // Release knob
    if (m_controls.size() > 1) drawKnob(renderer, m_controls[1]);

    // Release mode pills
    const bool autoMode = m_instance &&
        m_instance->getParameter(Aestra::Audio::Plugins::AestraLimit::kReleaseMode) <= 0.5f;
    drawPill(renderer, m_autoPillRect, autoMode, m_autoHovered, "AUTO", accent);
    drawPill(renderer, m_manualPillRect, !autoMode, m_manualHovered, "MANUAL", accent);

    // Bypass pill
    const bool bypassed = m_instance &&
        m_instance->getParameter(Aestra::Audio::Plugins::AestraLimit::kBypass) > 0.5f;
    drawPill(renderer, m_bypassPillRect, bypassed, m_bypassHovered, "BYPASS",
             NUIColor(0.92f, 0.28f, 0.22f, 1.0f));

    // Bottom meters
    char inBuf[16]{}, outBuf[16]{}, grBuf[16]{};
    const float inDb = m_inputDisplay > 1.0e-8f ? 20.0f * std::log10(m_inputDisplay) : -60.0f;
    const float outDb = m_outputDisplay > 1.0e-8f ? 20.0f * std::log10(m_outputDisplay) : -60.0f;
    std::snprintf(inBuf, sizeof(inBuf), "%.1f dB", inDb);
    std::snprintf(outBuf, sizeof(outBuf), "%.1f dB", outDb);
    std::snprintf(grBuf, sizeof(grBuf), "-%.1f dB", m_grDisplayDb);

    drawMeterBar(renderer, m_inMeterRect, levelToNorm(m_inputDisplay), "IN", inBuf,
                 NUIColor(0.35f, 0.45f, 1.0f, 1.0f));
    drawMeterBar(renderer, m_outMeterRect, levelToNorm(m_outputDisplay), "OUT", outBuf,
                 NUIColor(0.35f, 0.45f, 1.0f, 1.0f));
    drawMeterBar(renderer, m_grNumRect, std::clamp(m_grDisplayDb / 24.0f, 0.0f, 1.0f),
                 "GR", grBuf, accent);
}

bool AestraLimitEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;

    auto b = getBounds();
    if (!b.contains(event.position) && !isDraggingWindow()) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        // Bypass pill
        if (m_bypassPillRect.contains(event.position)) {
            if (m_instance) {
                const bool was = m_instance->getParameter(Aestra::Audio::Plugins::AestraLimit::kBypass) > 0.5f;
                m_instance->setParameter(Aestra::Audio::Plugins::AestraLimit::kBypass, was ? 0.0f : 1.0f);
                setDirty(true);
            }
            return true;
        }

        // Auto/Manual pill
        if (m_autoPillRect.contains(event.position)) {
            if (m_instance) {
                m_instance->setParameter(Aestra::Audio::Plugins::AestraLimit::kReleaseMode, 0.0f);
                setDirty(true);
            }
            return true;
        }
        if (m_manualPillRect.contains(event.position)) {
            if (m_instance) {
                m_instance->setParameter(Aestra::Audio::Plugins::AestraLimit::kReleaseMode, 1.0f);
                setDirty(true);
            }
            return true;
        }
    }

    // Double-click reset on knobs
    if (event.pressed && event.button == NUIMouseButton::Left && event.doubleClick) {
        for (auto& c : m_controls) {
            if (c.slider && c.slider->getBounds().contains({event.position.x, event.position.y})) {
                const float def = m_instance->getParameters()[c.paramId].defaultValue;
                m_instance->setParameter(c.paramId, def);
                c.slider->setValue(def);
                setDirty(true);
                return true;
            }
        }
    }

    // Hover tracking
    if (!event.pressed && !event.released) {
        const bool contains = b.contains(event.position);
        const bool bh = contains && m_bypassPillRect.contains(event.position);
        const bool ah = contains && m_autoPillRect.contains(event.position);
        const bool mh = contains && m_manualPillRect.contains(event.position);
        if (bh != m_bypassHovered || ah != m_autoHovered || mh != m_manualHovered) {
            m_bypassHovered = bh;
            m_autoHovered = ah;
            m_manualHovered = mh;
            repaint();
        }
    }

    return NUIComponent::onMouseEvent(event);
}

} // namespace AestraUI
