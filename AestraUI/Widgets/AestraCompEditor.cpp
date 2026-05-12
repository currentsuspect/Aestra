// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraCompEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraComp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;

NUIColor surfaceBg() { return NUIColor(0.052f, 0.056f, 0.064f, 0.980f); }
NUIColor insetBg() { return NUIColor(0.018f, 0.020f, 0.024f, 0.960f); }
// TODO: migrate plugin knob accent from teal to Aestra purple
NUIColor amber() { return NUIColor(0.95f, 0.62f, 0.25f, 1.0f); }
NUIColor teal() { return NUIColor(0.22f, 0.76f, 0.68f, 1.0f); }
NUIColor red() { return NUIColor(0.92f, 0.28f, 0.22f, 1.0f); }

float levelToNorm(float linear) {
    const float db = linear > 1.0e-8f ? 20.0f * std::log10(linear) : -60.0f;
    return std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
}

float smoothMeter(float current, float target, float attack, float release) {
    const float coeff = target > current ? attack : release;
    return current + (target - current) * coeff;
}
} // namespace

AestraCompEditor::AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraCompEditor");
    setPanelTitle("Aestra Compressor");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildControls();
}

void AestraCompEditor::buildControls() {
    m_controls.clear();
    if (!m_instance) return;

    using Comp = Aestra::Audio::Plugins::AestraComp;
    struct Meta {
        const char* label;
        uint32_t id;
    };

    const Meta controls[] = {
        {"Threshold", Comp::kThreshold},
        {"Ratio", Comp::kRatio},
        {"Attack", Comp::kAttack},
        {"Release", Comp::kRelease},
        {"Knee", Comp::kKnee},
        {"Makeup", Comp::kMakeup},
        {"Mix", Comp::kMix},
        {"Input", Comp::kInputGain},
        {"Output", Comp::kOutputGain},
        {"Detector HPF", Comp::kDetectorHPF},
    };

    for (const auto& item : controls) {
        Control control;
        control.label = item.label;
        control.paramId = item.id;
        control.value = std::clamp(m_instance->getParameter(item.id), 0.0f, 1.0f);
        m_controls.push_back(control);
    }

    layoutControls();
}

void AestraCompEditor::layoutControls() {
    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }

    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    m_bypassRect = NUIRect(b.right() - 116.0f, b.y + AestraPanelWindow::TITLE_BAR_H + 6.0f, 72.0f, 26.0f);

    const float meterTop = b.y + AestraPanelWindow::TITLE_BAR_H + 42.0f;
    m_grMeterRect = NUIRect(contentX, meterTop, contentW, 80.0f);
    m_inputMeterRect = NUIRect(contentX, meterTop + 94.0f, contentW * 0.5f - 8.0f, 28.0f);
    m_outputMeterRect = NUIRect(contentX + contentW * 0.5f + 8.0f, meterTop + 94.0f, contentW * 0.5f - 8.0f, 28.0f);

    const float gridY = meterTop + 142.0f;
    const float gapX = 10.0f;
    const float gapY = 16.0f;
    const float cellW = (contentW - gapX * 4.0f) / 5.0f;
    const float cellHPrimary = 108.0f;
    const float cellHSecondary = 86.0f;
    for (size_t i = 0; i < m_controls.size(); ++i) {
        const int row = static_cast<int>(i / 5);
        const int col = static_cast<int>(i % 5);
        const float x = contentX + static_cast<float>(col) * (cellW + gapX);
        const float cellH = (row == 0) ? cellHPrimary : cellHSecondary;
        const float y = gridY + (row == 0 ? 0.0f : cellHPrimary + gapY);
        const float knobSz = (row == 0) ? kKnobSizePrimary : kKnobSizeSecondary;
        m_controls[i].bounds = NUIRect(x, y, cellW, cellH);
        m_controls[i].knobRect = NUIRect(x + (cellW - knobSz) * 0.5f, y + 21.0f, knobSz, knobSz);
    }
}

void AestraCompEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraCompEditor::syncControlsFromPlugin() {
    if (!m_instance) return;
    for (auto& control : m_controls) {
        if (control.dragging) continue;
        control.value = std::clamp(m_instance->getParameter(control.paramId), 0.0f, 1.0f);
    }
}

void AestraCompEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;

    syncControlsFromPlugin();
    if (auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(m_instance)) {
        const float gr = std::clamp(comp->getCurrentGainReductionDb(), 0.0f, 48.0f);
        const float in = std::clamp(comp->getInputLevel(), 0.0f, 16.0f);
        const float out = std::clamp(comp->getOutputLevel(), 0.0f, 16.0f);
        m_grDisplayDb = smoothMeter(m_grDisplayDb, gr, 0.58f, 0.18f);
        m_inputDisplay = smoothMeter(m_inputDisplay, in, 0.55f, 0.16f);
        m_outputDisplay = smoothMeter(m_outputDisplay, out, 0.55f, 0.16f);
    }
    repaint();
}

void AestraCompEditor::drawBypassButton(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = isBypassed();
    if (bypassed) {
        renderer.fillRoundedRect(m_bypassRect, 8.0f, red().withAlpha(m_bypassHovered ? 0.94f : 0.78f));
        renderer.strokeRoundedRect(m_bypassRect, 8.0f, 1.0f, red().withAlpha(0.50f));
        renderer.drawText("BYPASSED", {m_bypassRect.x + 12.0f, m_bypassRect.y + 8.0f}, 8.0f,
                          theme.getColor("textPrimary"));
    } else {
        renderer.fillRoundedRect(m_bypassRect, 8.0f, theme.getColor("success").withAlpha(0.18f));
        renderer.drawTextCentered("ACTIVE", m_bypassRect, 8.0f, theme.getColor("success"));
    }
}

void AestraCompEditor::drawGainReductionMeter(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect b = m_grMeterRect;
    renderer.fillRoundedRect(b, 10.0f, surfaceBg());
    renderer.strokeRoundedRect(b, 10.0f, 1.0f, NUIColor(1, 1, 1, 0.075f));
    renderer.drawText("GAIN REDUCTION", {b.x + 16.0f, b.y + 12.0f}, 9.0f,
                      theme.getColor("textPrimary").withAlpha(0.82f));

    const NUIRect track(b.x + 16.0f, b.y + 38.0f, b.width - 104.0f, 12.0f);
    renderer.fillRoundedRect(track, 6.0f, insetBg());
    for (int i = 0; i <= 4; ++i) {
        const float x = track.x + track.width * static_cast<float>(i) / 4.0f;
        renderer.drawLine({x, track.y - 4.0f}, {x, track.bottom() + 4.0f}, 1.0f, NUIColor(1, 1, 1, 0.075f));
    }

    const float norm = std::clamp(m_grDisplayDb / 24.0f, 0.0f, 1.0f);
    renderer.fillRoundedRect({track.x, track.y, track.width * norm, track.height}, 6.0f,
                             amber().withAlpha(0.90f));
    renderer.drawText("0", {track.x - 1.0f, track.bottom() + 11.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.72f));
    renderer.drawText("-12", {track.x + track.width * 0.5f - 10.0f, track.bottom() + 11.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.72f));
    renderer.drawText("-24", {track.right() - 18.0f, track.bottom() + 11.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.72f));

    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "-%.1fdB", m_grDisplayDb);
    renderer.drawText(buf, {b.right() - 78.0f, b.y + 31.0f}, 16.0f, amber().withAlpha(0.96f));
}

void AestraCompEditor::drawLevelMeter(NUIRenderer& renderer,
                                      const NUIRect& bounds,
                                      const std::string& label,
                                      float smoothedLevel) {
    auto& theme = NUIThemeManager::getInstance();
    const float norm = levelToNorm(smoothedLevel);
    renderer.fillRoundedRect(bounds, 8.0f, surfaceBg().withAlpha(0.90f));
    const NUIRect track(bounds.x + 48.0f, bounds.y + 10.0f, bounds.width - 60.0f, 8.0f);
    renderer.fillRoundedRect(track, 4.0f, insetBg());
    renderer.fillRoundedRect({track.x, track.y, track.width * norm, track.height}, 4.0f,
                             (label == "IN" ? teal() : amber()).withAlpha(0.86f));
    renderer.strokeRoundedRect(bounds, 8.0f, 1.0f, NUIColor(1, 1, 1, 0.065f));
    renderer.drawText(label, {bounds.x + 14.0f, bounds.y + 9.0f}, 8.0f,
                      theme.getColor("textPrimary").withAlpha(0.82f));
}

std::string AestraCompEditor::valueText(uint32_t paramId) const {
    if (!m_instance) return {};
    return m_instance->getParameterDisplay(paramId);
}

void AestraCompEditor::drawControl(NUIRenderer& renderer, const Control& control) {
    auto& theme = NUIThemeManager::getInstance();
    const bool hot = control.hovered || control.dragging;
    const bool isPrimary = control.paramId == Aestra::Audio::Plugins::AestraComp::kThreshold ||
                           control.paramId == Aestra::Audio::Plugins::AestraComp::kRatio ||
                           control.paramId == Aestra::Audio::Plugins::AestraComp::kAttack ||
                           control.paramId == Aestra::Audio::Plugins::AestraComp::kRelease;
    const NUIColor accent = isPrimary ? teal() : teal().withAlpha(0.72f);

    renderer.fillRoundedRect(control.bounds, 9.0f, hot ? NUIColor(0.070f, 0.078f, 0.085f, 0.99f) : surfaceBg());
    renderer.strokeRoundedRect(control.bounds, 9.0f, 1.0f, hot ? accent.withAlpha(0.42f) : NUIColor(1, 1, 1, 0.060f));

    const float cx = control.knobRect.center().x;
    const float cy = control.knobRect.center().y;
    const float knobRadius = std::min(control.knobRect.width, control.knobRect.height) * 0.5f;
    const float r = knobRadius * 0.70f;
    renderer.fillCircle({cx, cy}, r + 7.0f, accent.withAlpha(hot ? 0.16f : 0.08f));
    renderer.fillCircle({cx, cy}, r, insetBg());
    renderer.strokeCircle({cx, cy}, r + 3.0f, 2.0f, NUIColor(1, 1, 1, 0.08f));

    const float start = kPi * 0.75f;
    const float sweep = kPi * 1.5f * control.value;
    std::array<NUIPoint, 30> arc{};
    const float arcDivisor = arc.size() > 1 ? static_cast<float>(arc.size() - 1) : 1.0f;
    for (size_t i = 0; i < arc.size(); ++i) {
        const float t = static_cast<float>(i) / arcDivisor;
        const float a = start + sweep * t;
        arc[i] = {cx + std::cos(a) * (r + 5.0f), cy + std::sin(a) * (r + 5.0f)};
    }
    renderer.drawPolyline(arc.data(), static_cast<int>(arc.size()), isPrimary ? 3.0f : 2.5f, accent.withAlpha(0.92f));

    const float pointerAngle = start + sweep;
    const float pointerLen = r - (isPrimary ? 5.0f : 4.0f);
    renderer.drawLine({cx, cy}, {cx + std::cos(pointerAngle) * pointerLen, cy + std::sin(pointerAngle) * pointerLen},
                      isPrimary ? 2.0f : 1.75f, theme.getColor("textPrimary").withAlpha(0.84f));

    const float labelAlpha = isPrimary ? 0.78f : 0.64f;
    renderer.drawText(control.label, {control.bounds.x + 9.0f, control.bounds.y + 7.0f}, isPrimary ? 8.5f : 8.0f,
                      theme.getColor("textPrimary").withAlpha(labelAlpha));
    const float valueY = control.bounds.bottom() - (isPrimary ? 17.0f : 15.0f);
    renderer.drawText(valueText(control.paramId), {control.bounds.x + 9.0f, valueY}, isPrimary ? 8.5f : 8.0f,
                      accent.withAlpha(0.95f));
}

void AestraCompEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    auto b = getBounds();
    drawBypassButton(renderer);
    drawGainReductionMeter(renderer);
    drawLevelMeter(renderer, m_inputMeterRect, "IN", m_inputDisplay);
    drawLevelMeter(renderer, m_outputMeterRect, "OUT", m_outputDisplay);

    const float sepY = m_outputMeterRect.bottom() + 12.0f;
    renderer.drawLine({b.x + kPad, sepY}, {b.right() - kPad, sepY}, 1.0f, NUIColor(1, 1, 1, 0.050f));

    for (const auto& control : m_controls) {
        drawControl(renderer, control);
    }
}

int AestraCompEditor::hitTestControl(float x, float y) const {
    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (m_controls[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

void AestraCompEditor::updateControlValue(int idx, float normalizedValue) {
    if (idx < 0 || idx >= static_cast<int>(m_controls.size()) || !m_instance) return;
    const float value = std::clamp(normalizedValue, 0.0f, 1.0f);
    m_controls[idx].value = value;
    m_instance->setParameter(m_controls[idx].paramId, value);
    repaint();
}

void AestraCompEditor::setBypassed(bool bypassed) {
    if (!m_instance) return;
    m_instance->setParameter(Aestra::Audio::Plugins::AestraComp::kBypass, bypassed ? 1.0f : 0.0f);
    repaint();
}

bool AestraCompEditor::isBypassed() const {
    return m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kBypass) > 0.5f;
}

bool AestraCompEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    // Let base class handle title bar / close / drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    auto b = getBounds();
    const bool draggingControl = std::any_of(m_controls.begin(), m_controls.end(),
                                             [](const Control& control) { return control.dragging; });
    const bool contains = b.contains(event.position);
    if (!contains && !isDraggingWindow() && !draggingControl) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_bypassRect.contains(event.position)) {
            setBypassed(!isBypassed());
            return true;
        }

        const int idx = hitTestControl(event.position.x, event.position.y);
        if (idx >= 0) {
            m_controls[idx].dragging = true;
            m_controls[idx].dragStartY = event.position.y;
            m_controls[idx].dragStartValue = m_controls[idx].value;
            return true;
        }
    }

    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (!m_controls[i].dragging) continue;
        const float delta = (m_controls[i].dragStartY - event.position.y) / 150.0f;
        updateControlValue(static_cast<int>(i), m_controls[i].dragStartValue + delta);
        if (!event.pressed && event.button == NUIMouseButton::Left) m_controls[i].dragging = false;
        return true;
    }

    if (!event.pressed && !event.released) {
        const int hover = contains ? hitTestControl(event.position.x, event.position.y) : -1;
        const bool bypassHover = contains && m_bypassRect.contains(event.position);
        if (hover != m_hoveredControl || bypassHover != m_bypassHovered) {
            m_hoveredControl = hover;
            m_bypassHovered = bypassHover;
            for (size_t i = 0; i < m_controls.size(); ++i) {
                m_controls[i].hovered = static_cast<int>(i) == hover;
            }
            repaint();
        }
    }

    return contains;
}

} // namespace AestraUI
