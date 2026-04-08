// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraCompEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

AestraCompEditor::AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraCompEditor");
    setSize(kWinW, kWinH);
    buildControls();
}

void AestraCompEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;
    struct Meta { const char* label; const char* sub; } meta[] = {
        {"Threshold", "dB"}, {"Ratio", ":1"}, {"Attack", "ms"},
        {"Release", "ms"}, {"Makeup", "dB"}, {"Knee", "dB"}
    };
    uint32_t ids[] = {0, 1, 2, 3, 4, 5};
    for (int i = 0; i < 6; ++i) {
        Knob k;
        k.label = meta[i].label; k.subtitle = meta[i].sub;
        k.paramId = ids[i]; k.value = m_instance->getParameter(ids[i]);
        m_knobs.push_back(k);
    }
    layoutControls();
}

void AestraCompEditor::layoutControls() {
    auto b = getBounds();
    float totalW = m_knobs.size() * kKnobSize + (m_knobs.size() - 1) * kKnobGap;
    float startX = b.x + (b.width - totalW) * 0.5f;
    float controlH = kKnobSize + 50.0f;
    float y = b.y + kTitleH + std::max(20.0f, (b.height - kTitleH - controlH) * 0.5f + 6.0f);
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        auto& k = m_knobs[i];
        float x = startX + i * (kKnobSize + kKnobGap);
        k.bounds = NUIRect(x - 10, y, kKnobSize + 20, kKnobSize + 50);
        k.knobRect = NUIRect(x, y + 10, kKnobSize, kKnobSize);
    }
}

void AestraCompEditor::drawTitleBar(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    NUIRect titleBar(b.x, b.y, b.width, kTitleH);
    renderer.fillRoundedRect(titleBar, kRadius, NUIColor(0.16f, 0.18f, 0.27f, 0.94f));
    renderer.drawText("Aestra Comp", {titleBar.x + kPad, titleBar.y + 10.0f}, 13.0f, theme.getColor("textPrimary"));
    renderer.drawText("Dynamics processor", {titleBar.x + kPad, titleBar.y + 23.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.82f));
    float cx = titleBar.right() - 16.0f - 10.0f, cy = titleBar.y + (kTitleH - 16.0f) * 0.5f;
    renderer.drawLine({cx+4, cy+4}, {cx+12, cy+12}, 1.5f, theme.getColor("textSecondary"));
    renderer.drawLine({cx+12, cy+4}, {cx+4, cy+12}, 1.5f, theme.getColor("textSecondary"));
    renderer.drawLine({titleBar.x, titleBar.bottom()}, {titleBar.right(), titleBar.bottom()},
                      1.0f, NUIColor(1,1,1,0.08f));
}

void AestraCompEditor::drawMeter(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(bounds, 8.0f, NUIColor(0.04f, 0.04f, 0.05f, 0.8f));
    // GR meter bar
    float meterW = 6.0f;
    float meterX = bounds.right() - 16.0f;
    float meterTop = bounds.y + 4.0f;
    float meterBot = bounds.bottom() - 4.0f;
    float meterH = meterBot - meterTop;
    renderer.fillRoundedRect({meterX, meterTop, meterW, meterH}, 3.0f, NUIColor(0.06f, 0.06f, 0.07f, 0.9f));
    // Simulated GR display (would be driven by plugin in real implementation)
    float grNorm = 0.3f;
    float grH = meterH * grNorm;
    NUIColor grColor = NUIColor(1.0f, 0.6f, 0.2f);
    renderer.fillRoundedRect({meterX, meterBot - grH, meterW, grH}, 3.0f, grColor.withAlpha(0.8f));
    renderer.drawText("GR", {meterX - 14.0f, meterTop - 2.0f}, 7.0f, theme.getColor("textSecondary").withAlpha(0.5f));
}

void AestraCompEditor::drawKnob(NUIRenderer& renderer, const Knob& k) {
    auto& theme = NUIThemeManager::getInstance();
    NUIColor accent = NUIColor(1.0f, 0.6f, 0.2f);

    // Background
    renderer.fillRoundedRect(k.bounds, 8.0f,
        k.hovered || k.dragging ? NUIColor(0.14f, 0.12f, 0.16f, 0.98f) : NUIColor(0.10f, 0.09f, 0.12f, 0.96f));
    renderer.strokeRoundedRect(k.bounds, 8.0f, 1.0f,
        k.hovered || k.dragging ? accent.withAlpha(0.5f) : NUIColor(1,1,1,0.05f));

    // Knob circle
    float cx = k.knobRect.center().x, cy = k.knobRect.center().y, r = kKnobSize * 0.42f;
    renderer.fillCircle({cx, cy}, r, NUIColor(0.08f, 0.08f, 0.09f, 0.95f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, accent.withAlpha(0.3f));

    // Knob arc (filled portion)
    constexpr float pi = 3.14159265358979323846f;
    float startAngle = pi * 0.75f;
    float endAngle = startAngle + k.value * pi * 1.5f;
    int numSeg = 32;
    for (int i = 0; i < numSeg; ++i) {
        float a1 = startAngle + (endAngle - startAngle) * i / numSeg;
        float a2 = startAngle + (endAngle - startAngle) * (i + 1) / numSeg;
        float x1 = cx + std::cos(a1) * (r - 3);
        float y1 = cy + std::sin(a1) * (r - 3);
        float x2 = cx + std::cos(a2) * (r - 3);
        float y2 = cy + std::sin(a2) * (r - 3);
        renderer.drawLine({x1, y1}, {x2, y2}, 3.0f, accent.withAlpha(0.8f));
    }

    // Knob pointer
    float ptrAngle = startAngle + k.value * pi * 1.5f;
    float px = cx + std::cos(ptrAngle) * (r - 6);
    float py = cy + std::sin(ptrAngle) * (r - 6);
    renderer.fillCircle({px, py}, 3.0f, k.dragging ? NUIColor(1,1,1,1.0f) : accent);

    // Labels
    renderer.drawText(k.label, {k.bounds.x + 8.0f, k.bounds.y + 4.0f}, 9.0f, theme.getColor("textPrimary"));
    renderer.drawText(m_instance ? m_instance->getParameterDisplay(k.paramId) : "0",
                      {k.bounds.x + 8.0f, k.bounds.bottom() - 12.0f}, 8.5f,
                      accent.withAlpha(0.9f));
}

void AestraCompEditor::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    float cardY = m_knobs.empty() ? (b.y + kTitleH + 18.0f) : (m_knobs.front().bounds.y - 14.0f);
    float cardH = m_knobs.empty() ? 126.0f : (m_knobs.front().bounds.height + 28.0f);
    renderer.fillRoundedRect(b, kRadius, NUIColor(0.07f, 0.08f, 0.11f, 0.985f));
    renderer.fillRoundedRect({b.x + 1.0f, cardY, b.width - 2.0f, cardH}, 11.0f,
                             NUIColor(0.10f, 0.11f, 0.16f, 0.72f));
    renderer.strokeRoundedRect(b, kRadius, 1.0f, NUIColor(0.60f, 0.68f, 1.0f, 0.16f));
    drawTitleBar(renderer);
    for (const auto& k : m_knobs) drawKnob(renderer, k);
    NUIRect meterArea(b.x + kPad, b.y + kTitleH + 10, b.width - kPad * 2 - 26, 24);
    drawMeter(renderer, meterArea);
}

int AestraCompEditor::hitTestKnob(float x, float y) const {
    for (size_t i = 0; i < m_knobs.size(); ++i)
        if (m_knobs[i].bounds.contains({x, y})) return static_cast<int>(i);
    return -1;
}
bool AestraCompEditor::hitTestCloseButton(float x, float y) const {
    auto b = getBounds();
    return NUIRect(b.right() - 26, b.y + 13, 16, 16).contains({x, y});
}
bool AestraCompEditor::hitTestTitleBar(float x, float y) const {
    auto b = getBounds();
    return NUIRect(b.x, b.y, b.width - 32, kTitleH).contains({x, y});
}

void AestraCompEditor::updateKnobValue(int idx, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_knobs.size()) || !m_instance) return;
    v = std::clamp(v, 0.0f, 1.0f);
    m_knobs[idx].value = v;
    m_instance->setParameter(m_knobs[idx].paramId, v);
    setDirty(true);
}

bool AestraCompEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    auto b = getBounds();
    bool isDraggingKnob = std::any_of(m_knobs.begin(), m_knobs.end(),
                                      [](const Knob& knob) { return knob.dragging; });
    bool contains = b.contains(event.position);
    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !isDraggingKnob) {
        if (m_onClose) m_onClose(); return false;
    }
    if (!contains && !m_isDraggingWindow && !isDraggingKnob) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hitTestCloseButton(event.position.x, event.position.y)) { if (m_onClose) m_onClose(); return true; }
        if (hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true; m_dragStartPos = event.position;
            m_windowStartPos = {b.x, b.y}; return true;
        }
        int kIdx = hitTestKnob(event.position.x, event.position.y);
        if (kIdx >= 0) {
            m_knobs[kIdx].dragging = true;
            m_knobs[kIdx].dragStartY = event.position.y;
            m_knobs[kIdx].dragStartValue = m_knobs[kIdx].value;
            return true;
        }
    }
    if (m_isDraggingWindow) {
        if (!event.pressed && event.button == NUIMouseButton::Left) { m_isDraggingWindow = false; return true; }
        float dx = event.position.x - m_dragStartPos.x, dy = event.position.y - m_dragStartPos.y;
        setBounds(m_windowStartPos.x + dx, m_windowStartPos.y + dy, b.width, b.height);
        layoutControls(); return true;
    }
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].dragging) {
            float delta = (m_knobs[i].dragStartY - event.position.y) / 150.0f;
            updateKnobValue(static_cast<int>(i), std::clamp(m_knobs[i].dragStartValue + delta, 0.0f, 1.0f));
            if (!event.pressed && event.button == NUIMouseButton::Left) m_knobs[i].dragging = false;
            return true;
        }
    }
    if (!event.pressed && !event.released) {
        int h = contains ? hitTestKnob(event.position.x, event.position.y) : -1;
        if (h != m_hoveredKnob) {
            m_hoveredKnob = h;
            for (size_t i = 0; i < m_knobs.size(); ++i) m_knobs[i].hovered = (static_cast<int>(i) == h);
            setDirty(true);
        }
    }
    return contains;
}

} // namespace AestraUI
