// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDelayEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDelay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;
NUIColor accent() { return NUIColor(0.48f, 0.34f, 0.78f, 1.0f); }
NUIColor cyanAccent() { return NUIColor(0.0f, 0.90f, 0.80f, 1.0f); }
NUIColor panelBg() { return NUIColor(0.050f, 0.048f, 0.066f, 0.992f); }
NUIColor cardBg() { return NUIColor(0.085f, 0.080f, 0.115f, 0.95f); }
}

AestraDelayEditor::AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDelayEditor");
    setSize(kWinW, kWinH);
    buildControls();
}

void AestraDelayEditor::buildControls() {
    m_knobs.clear();
    m_divisionButtons.clear();
    if (!m_instance) return;

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    struct Meta { const char* label; uint32_t id; bool readOnly; };
    const Meta meta[] = {
        {"Time", Delay::kTime, true},
        {"Feedback", Delay::kFeedback, false},
        {"Damping", Delay::kDamping, false},
        {"Stereo", Delay::kStereoShift, false},
        {"Mod Rate", Delay::kModRate, false},
        {"Mod Depth", Delay::kModDepth, false},
    };

    for (const auto& item : meta) {
        Knob k;
        k.label = item.label;
        k.paramId = item.id;
        k.value = m_instance->getParameter(item.id);
        k.readOnly = item.readOnly;
        m_knobs.push_back(k);
    }

    const int order[] = {
        Delay::kDiv1_4, Delay::kDiv1_8, Delay::kDiv1_16, Delay::kDiv1_32,
        Delay::kDiv1_4D, Delay::kDiv1_8D, Delay::kDiv1_16D,
        Delay::kDiv1_4T, Delay::kDiv1_8T, Delay::kDiv1_16T
    };
    for (int idx : order) {
        DivisionButton b;
        b.index = idx;
        b.label = Delay::noteDivisionLabel(idx);
        m_divisionButtons.push_back(b);
    }

    layoutControls();
}

void AestraDelayEditor::layoutControls() {
    const auto b = getBounds();
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;

    const float pillY = b.y + kTitleH + 14.0f;
    const float pillH = 34.0f;
    const float groupGap = 14.0f;
    const float groupW = (contentW - groupGap) * 0.5f;
    m_freeRect = {contentX, pillY, groupW * 0.5f, pillH};
    m_syncRect = {contentX + groupW * 0.5f, pillY, groupW * 0.5f, pillH};
    m_stereoRect = {contentX + groupW + groupGap, pillY, groupW * 0.5f, pillH};
    m_pingPongRect = {contentX + groupW + groupGap + groupW * 0.5f, pillY, groupW * 0.5f, pillH};

    const bool sync = m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) > 0.5f;
    const float gridY = pillY + pillH + 14.0f;
    const float gridH = sync ? 76.0f : 0.0f;
    if (sync) {
        const float cellW = (contentW - 3.0f * 8.0f) / 4.0f;
        const float cellH = 22.0f;
        for (size_t i = 0; i < m_divisionButtons.size(); ++i) {
            const int row = static_cast<int>(i / 4);
            const int col = static_cast<int>(i % 4);
            const float x = contentX + static_cast<float>(col) * (cellW + 8.0f);
            const float y = gridY + static_cast<float>(row) * (cellH + 6.0f);
            m_divisionButtons[i].bounds = {x, y, cellW, cellH};
        }
    }

    const float knobY = gridY + gridH + 16.0f;
    const float cellGap = 14.0f;
    const float cellW = (contentW - cellGap * 2.0f) / 3.0f;
    const float cellH = 84.0f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        const int row = static_cast<int>(i / 3);
        const int col = static_cast<int>(i % 3);
        const float x = contentX + static_cast<float>(col) * (cellW + cellGap);
        const float y = knobY + static_cast<float>(row) * (cellH + 10.0f);
        m_knobs[i].bounds = {x, y, cellW, cellH};
        m_knobs[i].knobRect = {x + (cellW - kKnobSize) * 0.5f, y + 18.0f, kKnobSize, kKnobSize};
    }

    m_mixSliderRect = {contentX, b.bottom() - 48.0f, contentW, 28.0f};
}

void AestraDelayEditor::drawTitleBar(NUIRenderer& renderer) {
    const auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect({b.x, b.y, b.width, kTitleH + 8.0f}, kRadius, NUIColor(0.09f, 0.08f, 0.13f, 0.94f));
    renderer.fillCircle({b.x + kPad + 10.0f, b.y + 25.0f}, 8.0f, cyanAccent().withAlpha(0.42f));
    renderer.fillCircle({b.x + kPad + 10.0f, b.y + 25.0f}, 3.8f, cyanAccent());
    renderer.drawText("Aestra Delay", {b.x + kPad + 28.0f, b.y + 14.0f}, 14.0f, theme.getColor("textPrimary"));
    renderer.drawText("Tempo-aware stereo echoes", {b.x + kPad + 28.0f, b.y + 31.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.72f));

    const NUIRect chip(b.right() - 106.0f, b.y + 17.0f, 55.0f, 20.0f);
    renderer.fillRoundedRect(chip, 10.0f, NUIColor(0.06f, 0.08f, 0.10f, 0.8f));
    renderer.strokeRoundedRect(chip, 10.0f, 1.0f, cyanAccent().withAlpha(0.38f));
    renderer.drawText("SYNC", {chip.x + 14.0f, chip.y + 5.0f}, 8.0f, cyanAccent().withAlpha(0.86f));

    const NUIRect closeRect(b.right() - 36.0f, b.y + 16.0f, 22.0f, 22.0f);
    renderer.fillRoundedRect(closeRect, 11.0f, NUIColor(1, 1, 1, 0.08f));
    renderer.drawLine({closeRect.x + 7.0f, closeRect.y + 7.0f}, {closeRect.x + 15.0f, closeRect.y + 15.0f}, 1.6f,
                      theme.getColor("textPrimary").withAlpha(0.72f));
    renderer.drawLine({closeRect.x + 15.0f, closeRect.y + 7.0f}, {closeRect.x + 7.0f, closeRect.y + 15.0f}, 1.6f,
                      theme.getColor("textPrimary").withAlpha(0.72f));
}

void AestraDelayEditor::drawPillSwitches(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const bool sync = m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    const bool ping = m_instance && m_instance->getParameter(Delay::kStereoMode) > 0.5f;

    auto drawGroup = [&](const NUIRect& left, const NUIRect& right, const char* l, const char* r, bool rightActive) {
        const NUIRect outer(left.x, left.y, left.width + right.width, left.height);
        renderer.fillRoundedRect(outer, 10.0f, NUIColor(0.032f, 0.030f, 0.044f, 0.96f));
        renderer.strokeRoundedRect(outer, 10.0f, 1.0f, accent().withAlpha(0.45f));
        auto seg = [&](const NUIRect& rect, const char* label, bool active) {
            renderer.fillRoundedRect(rect, 9.0f, active ? accent().withAlpha(0.96f) : NUIColor(0, 0, 0, 0));
            renderer.drawText(label, {rect.center().x - 24.0f, rect.y + 11.0f}, 10.0f,
                              active ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.76f));
        };
        seg(left, l, !rightActive);
        seg(right, r, rightActive);
    };

    drawGroup(m_freeRect, m_syncRect, "Free", "Sync", sync);
    drawGroup(m_stereoRect, m_pingPongRect, "Stereo", "Ping-Pong", ping);
}

void AestraDelayEditor::drawDivisionGrid(NUIRenderer& renderer) {
    if (!m_instance || m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) <= 0.5f) return;

    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const int active = Delay::noteDivisionIndexFromParam(m_instance->getParameter(Delay::kNoteDivision));
    for (const auto& btn : m_divisionButtons) {
        const bool isActive = btn.index == active;
        renderer.fillRoundedRect(btn.bounds, 6.0f, isActive ? accent().withAlpha(0.95f) : NUIColor(0.07f, 0.065f, 0.09f, 0.90f));
        renderer.strokeRoundedRect(btn.bounds, 6.0f, 1.0f, accent().withAlpha(isActive ? 0.9f : 0.32f));
        renderer.drawText(btn.label, {btn.bounds.center().x - 14.0f, btn.bounds.y + 6.0f}, 8.5f,
                          isActive ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.78f));
    }
}

std::string AestraDelayEditor::formattedValue(uint32_t paramId) const {
    if (!m_instance) return {};
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (paramId == Delay::kTime) {
        if (auto delay = std::dynamic_pointer_cast<Delay>(m_instance)) {
            return std::to_string(static_cast<int>(std::round(delay->getEffectiveDelayMs()))) + "ms";
        }
    }
    return m_instance->getParameterDisplay(paramId);
}

void AestraDelayEditor::drawKnob(NUIRenderer& renderer, const Knob& k) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor knobAccent = k.readOnly ? cyanAccent() : accent();
    renderer.fillRoundedRect(k.bounds, 10.0f,
        k.hovered || k.dragging ? NUIColor(0.13f, 0.115f, 0.17f, 0.98f) : cardBg());
    renderer.strokeRoundedRect(k.bounds, 10.0f, 1.0f,
        k.hovered || k.dragging ? knobAccent.withAlpha(0.55f) : NUIColor(1, 1, 1, 0.06f));

    const float cx = k.knobRect.center().x;
    const float cy = k.knobRect.center().y;
    const float r = kKnobSize * 0.40f;
    renderer.fillCircle({cx, cy}, r + 7.0f, knobAccent.withAlpha(0.07f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, knobAccent.withAlpha(0.32f));

    const float sa = kPi * 0.75f;
    const float ea = sa + k.value * kPi * 1.5f;
    for (int i = 0; i < 34; ++i) {
        const float a1 = sa + (ea - sa) * static_cast<float>(i) / 34.0f;
        const float a2 = sa + (ea - sa) * static_cast<float>(i + 1) / 34.0f;
        renderer.drawLine({cx + std::cos(a1) * (r - 3.0f), cy + std::sin(a1) * (r - 3.0f)},
                          {cx + std::cos(a2) * (r - 3.0f), cy + std::sin(a2) * (r - 3.0f)},
                          3.0f, knobAccent.withAlpha(k.readOnly ? 0.40f : 0.82f));
    }

    const float pa = sa + k.value * kPi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa) * (r - 7.0f), cy + std::sin(pa) * (r - 7.0f)}, 3.2f,
                        k.dragging ? NUIColor(1, 1, 1, 1) : knobAccent);
    renderer.drawText(k.label, {k.bounds.x + 8.0f, k.bounds.y + 5.0f}, 9.0f, theme.getColor("textPrimary"));
    renderer.drawText(formattedValue(k.paramId), {k.bounds.x + 8.0f, k.bounds.bottom() - 12.0f}, 8.0f,
                      knobAccent.withAlpha(0.9f));
}

void AestraDelayEditor::drawMixSlider(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const float mix = m_instance ? m_instance->getParameter(Delay::kMix) : 0.0f;
    const NUIRect track(m_mixSliderRect.x + 58.0f, m_mixSliderRect.y + 11.0f, m_mixSliderRect.width - 104.0f, 6.0f);
    renderer.fillRoundedRect(m_mixSliderRect, 10.0f, NUIColor(0.07f, 0.065f, 0.09f, 0.92f));
    renderer.strokeRoundedRect(m_mixSliderRect, 10.0f, 1.0f, accent().withAlpha(0.35f));
    renderer.drawText("Mix", {m_mixSliderRect.x + 14.0f, m_mixSliderRect.y + 9.0f}, 9.0f, theme.getColor("textPrimary"));
    renderer.fillRoundedRect(track, 3.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 3.0f, accent().withAlpha(0.92f));
    renderer.fillCircle({track.x + track.width * mix, track.center().y}, 8.0f, theme.getColor("textPrimary"));
    renderer.drawText(std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%",
                      {m_mixSliderRect.right() - 42.0f, m_mixSliderRect.y + 9.0f}, 8.0f, accent().withAlpha(0.92f));
}

void AestraDelayEditor::onRender(NUIRenderer& renderer) {
    const auto b = getBounds();
    renderer.fillRoundedRect(b, kRadius, panelBg());
    renderer.strokeRoundedRect(b, kRadius, 1.0f, NUIColor(0.0f, 0.90f, 0.80f, 0.16f));
    drawTitleBar(renderer);
    drawPillSwitches(renderer);
    drawDivisionGrid(renderer);
    for (auto& k : m_knobs) {
        if (m_instance) k.value = m_instance->getParameter(k.paramId);
        drawKnob(renderer, k);
    }
    drawMixSlider(renderer);
}

int AestraDelayEditor::hitTestKnob(float x, float y) const {
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (!m_knobs[i].readOnly && m_knobs[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

int AestraDelayEditor::hitTestDivision(float x, float y) const {
    for (size_t i = 0; i < m_divisionButtons.size(); ++i) {
        if (m_divisionButtons[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraDelayEditor::hitTestCloseButton(float x, float y) const {
    return NUIRect(getBounds().right() - 36.0f, getBounds().y + 16.0f, 22.0f, 22.0f).contains({x, y});
}

bool AestraDelayEditor::hitTestTitleBar(float x, float y) const {
    return NUIRect(getBounds().x, getBounds().y, getBounds().width - 44.0f, kTitleH).contains({x, y});
}

void AestraDelayEditor::updateKnobValue(int idx, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_knobs.size()) || !m_instance) return;
    m_knobs[idx].value = std::clamp(v, 0.0f, 1.0f);
    m_instance->setParameter(m_knobs[idx].paramId, m_knobs[idx].value);
    setDirty(true);
}

bool AestraDelayEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    const auto b = getBounds();
    const bool contains = b.contains(event.position);
    const bool draggingKnob = std::any_of(m_knobs.begin(), m_knobs.end(), [](const Knob& k) { return k.dragging; });

    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !draggingKnob && !m_draggingMix) {
        if (m_onClose) m_onClose();
        return false;
    }
    if (!contains && !m_isDraggingWindow && !draggingKnob && !m_draggingMix) return false;

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hitTestCloseButton(event.position.x, event.position.y)) {
            if (m_onClose) m_onClose();
            return true;
        }
        if (hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true;
            m_dragStartPos = event.position;
            m_windowStartPos = {b.x, b.y};
            return true;
        }
        if (m_instance) {
            if (m_freeRect.contains(event.position)) {
                m_instance->setParameter(Delay::kSyncMode, 0.0f);
                layoutControls();
                setDirty(true);
                return true;
            }
            if (m_syncRect.contains(event.position)) {
                m_instance->setParameter(Delay::kSyncMode, 1.0f);
                layoutControls();
                setDirty(true);
                return true;
            }
            if (m_stereoRect.contains(event.position)) {
                m_instance->setParameter(Delay::kStereoMode, 0.0f);
                setDirty(true);
                return true;
            }
            if (m_pingPongRect.contains(event.position)) {
                m_instance->setParameter(Delay::kStereoMode, 1.0f);
                setDirty(true);
                return true;
            }
            if (m_instance->getParameter(Delay::kSyncMode) > 0.5f) {
                const int div = hitTestDivision(event.position.x, event.position.y);
                if (div >= 0) {
                    m_instance->setParameter(Delay::kNoteDivision,
                                             Delay::noteDivisionParamFromIndex(m_divisionButtons[static_cast<size_t>(div)].index));
                    setDirty(true);
                    return true;
                }
            }
            if (m_mixSliderRect.contains(event.position)) {
                m_draggingMix = true;
                const float sliderX = m_mixSliderRect.x + 58.0f;
                const float sliderW = m_mixSliderRect.width - 104.0f;
                m_instance->setParameter(Delay::kMix, std::clamp((event.position.x - sliderX) / sliderW, 0.0f, 1.0f));
                setDirty(true);
                return true;
            }
        }
        const int kIdx = hitTestKnob(event.position.x, event.position.y);
        if (kIdx >= 0) {
            m_knobs[static_cast<size_t>(kIdx)].dragging = true;
            m_knobs[static_cast<size_t>(kIdx)].dragStartY = event.position.y;
            m_knobs[static_cast<size_t>(kIdx)].dragStartValue = m_knobs[static_cast<size_t>(kIdx)].value;
            return true;
        }
    }

    if (m_isDraggingWindow) {
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_isDraggingWindow = false;
            return true;
        }
        setBounds(m_windowStartPos.x + event.position.x - m_dragStartPos.x,
                  m_windowStartPos.y + event.position.y - m_dragStartPos.y,
                  b.width, b.height);
        layoutControls();
        return true;
    }

    if (m_draggingMix && m_instance) {
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingMix = false;
            return true;
        }
        const float sliderX = m_mixSliderRect.x + 58.0f;
        const float sliderW = m_mixSliderRect.width - 104.0f;
        m_instance->setParameter(Delay::kMix, std::clamp((event.position.x - sliderX) / sliderW, 0.0f, 1.0f));
        setDirty(true);
        return true;
    }

    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].dragging) {
            updateKnobValue(static_cast<int>(i),
                m_knobs[i].dragStartValue + (m_knobs[i].dragStartY - event.position.y) / 150.0f);
            if (!event.pressed && event.button == NUIMouseButton::Left) m_knobs[i].dragging = false;
            return true;
        }
    }

    if (!event.pressed && !event.released) {
        const int h = contains ? hitTestKnob(event.position.x, event.position.y) : -1;
        if (h != m_hoveredKnob) {
            m_hoveredKnob = h;
            for (size_t i = 0; i < m_knobs.size(); ++i) {
                m_knobs[i].hovered = static_cast<int>(i) == h;
            }
            setDirty(true);
        }
    }

    return contains;
}

} // namespace AestraUI
