// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumblePluginEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>

namespace AestraUI {

namespace {
constexpr float kCardRadius = 14.0f;
}

RumblePluginEditor::RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("RumblePluginEditor");
    setPanelTitle("Aestra Rumble");
    setSize(kWindowWidth, kWindowHeight);
    setEnforceParentBounds(true);
    buildControls();
}

void RumblePluginEditor::buildControls() {
    m_controls.clear();
    if (!m_instance) {
        return;
    }

    struct Meta {
        const char* title;
        const char* subtitle;
    };

    const Meta meta[] = {
        {"Decay", "Tail length"},
        {"Drive", "Body + grit"},
        {"Tone", "Low-end focus"},
        {"Gain", "Final level"},
    };

    auto params = m_instance->getParameters();
    const size_t count = std::min<size_t>(params.size(), 4);
    m_controls.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        MacroControl control;
        control.parameterId = params[i].id;
        control.title = meta[i].title;
        control.subtitle = meta[i].subtitle;
        control.normalizedValue = m_instance->getParameter(params[i].id);
        m_controls.push_back(std::move(control));
    }

    layoutControls();
}

void RumblePluginEditor::layoutControls() {
    const auto bounds = getBounds();
    const float contentX = bounds.x + kPadding;
    const float contentY = bounds.y + AestraPanelWindow::TITLE_BAR_H + kPadding;
    const float contentW = bounds.width - kPadding * 2.0f;
    const float cardsY = contentY + kHeroHeight + 12.0f;
    const float gap = 10.0f;
    const float cardW = (contentW - gap * 3.0f) / 4.0f;
    const float cardH = bounds.height - (cardsY - bounds.y) - kPadding;

    for (size_t i = 0; i < m_controls.size(); ++i) {
        auto& control = m_controls[i];
        const float x = contentX + static_cast<float>(i) * (cardW + gap);
        control.cardBounds = NUIRect(x, cardsY, cardW, cardH);
        control.trackBounds = NUIRect(x + 16.0f, cardsY + 82.0f, cardW - 32.0f, 8.0f);

        const float thumbX = control.trackBounds.x + control.trackBounds.width * control.normalizedValue - 7.0f;
        control.thumbBounds = NUIRect(thumbX, control.trackBounds.y - 5.0f, 14.0f, 18.0f);
    }
}

void RumblePluginEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
}

void RumblePluginEditor::drawHero(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(bounds, kCardRadius, NUIColor(0.116f, 0.116f, 0.116f, 0.92f));
    renderer.strokeRoundedRect(bounds, kCardRadius, 1.0f, theme.getColor("accentPrimary").withAlpha(0.45f));

    renderer.drawText("Low-end first. Sweep and punch are derived from the current macros.",
                      {bounds.x + 14.0f, bounds.y + 18.0f}, 11.0f, theme.getColor("textPrimary"));
    renderer.drawText("Decay stretches the tail, Drive adds bite, Tone tightens the body, Gain sets output.",
                      {bounds.x + 14.0f, bounds.y + 38.0f}, 10.0f, theme.getColor("textSecondary").withAlpha(0.95f));
}

void RumblePluginEditor::drawControl(NUIRenderer& renderer, const MacroControl& control, bool hovered) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor accent = theme.getColor("accentPrimary");
    const NUIColor cardColor = hovered || control.isDragging ? NUIColor(0.147f, 0.147f, 0.147f, 0.98f)
                                                             : NUIColor(0.114f, 0.114f, 0.114f, 0.96f);

    renderer.fillRoundedRect(control.cardBounds, kCardRadius, cardColor);
    renderer.strokeRoundedRect(control.cardBounds, kCardRadius, 1.0f,
                               hovered || control.isDragging ? accent.withAlpha(0.60f)
                                                             : NUIColor(1.0f, 1.0f, 1.0f, 0.07f));

    renderer.drawText(control.title, {control.cardBounds.x + 14.0f, control.cardBounds.y + 18.0f}, 12.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText(control.subtitle, {control.cardBounds.x + 14.0f, control.cardBounds.y + 38.0f}, 10.0f,
                      theme.getColor("textSecondary"));
    renderer.drawText(m_instance ? m_instance->getParameterDisplay(control.parameterId) : "0",
                      {control.cardBounds.x + 14.0f, control.cardBounds.y + 58.0f}, 10.0f,
                      accent.withAlpha(0.95f));

    renderer.fillRoundedRect(control.trackBounds, 4.0f, NUIColor(0.021f, 0.021f, 0.021f, 0.78f));
    const float filledWidth = std::max(0.0f, std::min(control.trackBounds.width, control.trackBounds.width * control.normalizedValue));
    if (filledWidth > 0.0f) {
        renderer.fillRoundedRect({control.trackBounds.x, control.trackBounds.y, filledWidth, control.trackBounds.height},
                                 4.0f, accent.withAlpha(0.90f));
    }

    renderer.fillRoundedRect(control.thumbBounds, 7.0f,
                             hovered || control.isDragging ? NUIColor(1.0f, 1.0f, 1.0f, 1.0f)
                                                           : NUIColor(0.86f, 0.86f, 0.90f, 0.95f));

    const float meterBottom = control.cardBounds.bottom() - 18.0f;
    const float meterTop = meterBottom - 92.0f;
    const float levelHeight = (meterBottom - meterTop) * control.normalizedValue;
    renderer.fillRoundedRect({control.cardBounds.x + 18.0f, meterBottom - levelHeight, control.cardBounds.width - 36.0f, levelHeight},
                             10.0f, accent.withAlpha(0.20f));
}

void RumblePluginEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    const auto bounds = getBounds();
    drawHero(renderer, {bounds.x + kPadding, bounds.y + AestraPanelWindow::TITLE_BAR_H + kPadding, bounds.width - kPadding * 2.0f, kHeroHeight});

    for (size_t i = 0; i < m_controls.size(); ++i) {
        drawControl(renderer, m_controls[i], static_cast<int>(i) == m_hoveredControl);
    }
}

int RumblePluginEditor::hitTestControl(float x, float y) const {
    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (m_controls[i].cardBounds.contains({x, y})) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void RumblePluginEditor::updateControlValue(int controlIndex, float normalizedValue) {
    if (!m_instance || controlIndex < 0 || controlIndex >= static_cast<int>(m_controls.size())) {
        return;
    }

    auto& control = m_controls[static_cast<size_t>(controlIndex)];
    control.normalizedValue = std::clamp(normalizedValue, 0.0f, 1.0f);
    m_instance->setParameter(control.parameterId, control.normalizedValue);
    const float thumbX = control.trackBounds.x + control.trackBounds.width * control.normalizedValue - 7.0f;
    control.thumbBounds.x = thumbX;
    setDirty(true);
}

bool RumblePluginEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) {
        return false;
    }

    // Let base class handle title bar / close / drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    const auto bounds = getBounds();
    const bool contains = bounds.contains(event.position);

    if (!contains && !isDraggingWindow()) {
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        m_hoveredControl = hitTestControl(event.position.x, event.position.y);
        if (m_hoveredControl >= 0) {
            m_controls[static_cast<size_t>(m_hoveredControl)].isDragging = true;
            const auto& track = m_controls[static_cast<size_t>(m_hoveredControl)].trackBounds;
            updateControlValue(m_hoveredControl, (event.position.x - track.x) / std::max(1.0f, track.width));
            return true;
        }
    }

    if (!event.pressed && !event.released && m_hoveredControl >= 0 && event.button == NUIMouseButton::Left) {
        auto& control = m_controls[static_cast<size_t>(m_hoveredControl)];
        if (control.isDragging) {
            updateControlValue(m_hoveredControl,
                               (event.position.x - control.trackBounds.x) / std::max(1.0f, control.trackBounds.width));
            return true;
        }
    }

    if (!event.pressed && event.button == NUIMouseButton::Left) {
        for (auto& control : m_controls) {
            control.isDragging = false;
        }
    }

    if (!event.pressed && !event.released) {
        const int hovered = contains ? hitTestControl(event.position.x, event.position.y) : -1;
        if (hovered != m_hoveredControl) {
            m_hoveredControl = hovered;
            setDirty(true);
        }
    }

    return contains;
}

} // namespace AestraUI
