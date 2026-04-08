// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDelayEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>

namespace AestraUI {

AestraDelayEditor::AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDelayEditor"); setSize(kWinW, kWinH);
    buildControls();
}

void AestraDelayEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;
    struct Meta { const char* label; } meta[] = {{"Time"},{"Feedback"},{"Damping"},{"Stereo"},{"Mod"},{"Rate"},{"Mix"}};
    uint32_t ids[] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 7; ++i) {
        Knob k; k.label = meta[i].label; k.paramId = ids[i];
        k.value = m_instance->getParameter(ids[i]);
        m_knobs.push_back(k);
    }
    layoutControls();
}

void AestraDelayEditor::layoutControls() {
    auto b = getBounds();
    float totalW = m_knobs.size() * kKnobSize + (m_knobs.size() - 1) * kKnobGap;
    float startX = b.x + (b.width - totalW) * 0.5f;
    float controlH = kKnobSize + 36.0f;
    float y = b.y + kTitleH + std::max(16.0f, (b.height - kTitleH - controlH) * 0.5f);
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        auto& k = m_knobs[i];
        float x = startX + i * (kKnobSize + kKnobGap);
        k.bounds = NUIRect(x - 8, y, kKnobSize + 16, kKnobSize + 36);
        k.knobRect = NUIRect(x, y + 8, kKnobSize, kKnobSize);
    }
}

void AestraDelayEditor::drawTitleBar(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    NUIRect titleBar(b.x, b.y, b.width, kTitleH);
    renderer.fillRoundedRect(titleBar, kRadius, NUIColor(0.16f, 0.18f, 0.27f, 0.94f));
    renderer.drawText("Aestra Delay", {titleBar.x + kPad, titleBar.y + 10.0f}, 13.0f, theme.getColor("textPrimary"));
    renderer.drawText("Stereo delay with modulation", {titleBar.x + kPad, titleBar.y + 23.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.82f));
    float cx = titleBar.right() - 26.0f, cy = titleBar.y + 13.0f;
    renderer.drawLine({cx+4, cy+4}, {cx+12, cy+12}, 1.5f, theme.getColor("textSecondary"));
    renderer.drawLine({cx+12, cy+4}, {cx+4, cy+12}, 1.5f, theme.getColor("textSecondary"));
    renderer.drawLine({titleBar.x, titleBar.bottom()}, {titleBar.right(), titleBar.bottom()}, 1.0f, NUIColor(1,1,1,0.08f));
}

void AestraDelayEditor::drawKnob(NUIRenderer& renderer, const Knob& k, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(k.bounds, 8.0f,
        k.hovered || k.dragging ? NUIColor(0.14f, 0.12f, 0.16f, 0.98f) : NUIColor(0.10f, 0.09f, 0.12f, 0.96f));
    renderer.strokeRoundedRect(k.bounds, 8.0f, 1.0f,
        k.hovered || k.dragging ? accent.withAlpha(0.5f) : NUIColor(1,1,1,0.05f));
    float cx = k.knobRect.center().x, cy = k.knobRect.center().y, r = kKnobSize * 0.42f;
    renderer.fillCircle({cx, cy}, r, NUIColor(0.08f, 0.08f, 0.09f, 0.95f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, accent.withAlpha(0.3f));
    constexpr float pi = 3.14159265358979323846f;
    float sa = pi * 0.75f, ea = sa + k.value * pi * 1.5f;
    for (int i = 0; i < 32; ++i) {
        float a1 = sa + (ea - sa) * i / 32, a2 = sa + (ea - sa) * (i + 1) / 32;
        renderer.drawLine({cx + std::cos(a1)*(r-3), cy + std::sin(a1)*(r-3)},
                          {cx + std::cos(a2)*(r-3), cy + std::sin(a2)*(r-3)}, 3.0f, accent.withAlpha(0.8f));
    }
    float pa = sa + k.value * pi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa)*(r-6), cy + std::sin(pa)*(r-6)}, 3.0f,
                        k.dragging ? NUIColor(1,1,1,1.0f) : accent);
    renderer.drawText(k.label, {k.bounds.x + 6.0f, k.bounds.y + 4.0f}, 9.0f, theme.getColor("textPrimary"));
    renderer.drawText(m_instance ? m_instance->getParameterDisplay(k.paramId) : "0",
                      {k.bounds.x + 6.0f, k.bounds.bottom() - 10.0f}, 8.0f, accent.withAlpha(0.9f));
}

void AestraDelayEditor::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    float cardY = m_knobs.empty() ? (b.y + kTitleH + 16.0f) : (m_knobs.front().bounds.y - 12.0f);
    float cardH = m_knobs.empty() ? 108.0f : (m_knobs.front().bounds.height + 24.0f);
    renderer.fillRoundedRect(b, kRadius, NUIColor(0.07f, 0.08f, 0.11f, 0.985f));
    renderer.fillRoundedRect({b.x + 1.0f, cardY, b.width - 2.0f, cardH}, 11.0f,
                             NUIColor(0.10f, 0.11f, 0.16f, 0.72f));
    renderer.strokeRoundedRect(b, kRadius, 1.0f, NUIColor(0.60f, 0.68f, 1.0f, 0.16f));
    drawTitleBar(renderer);
    NUIColor accent(0.6f, 0.3f, 1.0f);
    for (const auto& k : m_knobs) drawKnob(renderer, k, accent);
}

int AestraDelayEditor::hitTestKnob(float x, float y) const {
    for (size_t i = 0; i < m_knobs.size(); ++i)
        if (m_knobs[i].bounds.contains({x, y})) return static_cast<int>(i);
    return -1;
}
bool AestraDelayEditor::hitTestCloseButton(float x, float y) const {
    return NUIRect(getBounds().right() - 26, getBounds().y + 13, 16, 16).contains({x, y});
}
bool AestraDelayEditor::hitTestTitleBar(float x, float y) const {
    return NUIRect(getBounds().x, getBounds().y, getBounds().width - 32, kTitleH).contains({x, y});
}
void AestraDelayEditor::updateKnobValue(int idx, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_knobs.size()) || !m_instance) return;
    m_knobs[idx].value = v = std::clamp(v, 0.0f, 1.0f);
    m_instance->setParameter(m_knobs[idx].paramId, v);
    setDirty(true);
}

bool AestraDelayEditor::onMouseEvent(const NUIMouseEvent& event) {
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
        if (kIdx >= 0) { m_knobs[kIdx].dragging = true; m_knobs[kIdx].dragStartY = event.position.y;
            m_knobs[kIdx].dragStartValue = m_knobs[kIdx].value; return true; }
    }
    if (m_isDraggingWindow) {
        if (!event.pressed && event.button == NUIMouseButton::Left) { m_isDraggingWindow = false; return true; }
        setBounds(m_windowStartPos.x + event.position.x - m_dragStartPos.x,
                  m_windowStartPos.y + event.position.y - m_dragStartPos.y, b.width, b.height);
        layoutControls(); return true;
    }
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].dragging) {
            updateKnobValue(static_cast<int>(i),
                std::clamp(m_knobs[i].dragStartValue + (m_knobs[i].dragStartY - event.position.y) / 150.0f, 0.0f, 1.0f));
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
