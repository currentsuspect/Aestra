// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumblePluginEditor.h"

#include "NUIIcon.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "RumblePresetBank.h"
#include "../../AestraCore/include/AestraLog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kKnobStart = kPi * 0.75f;
constexpr float kKnobSweep = kPi * 1.5f;

NUIColor impactAccent() {
    return NUIColor(1.0f, 0.38f, 0.14f, 1.0f);
}
NUIColor sweepAccent() {
    return NUIColor(0.98f, 0.66f, 0.18f, 1.0f);
}
NUIColor bodyAccent() {
    return NUIColor(0.58f, 0.32f, 1.0f, 1.0f);
}
NUIColor cleanAccent() {
    return NUIColor(0.35f, 0.72f, 1.0f, 1.0f);
}
NUIColor shellSurface() {
    return editorNeutral(NUIColor(0.026f, 0.025f, 0.032f, 0.985f));
}
NUIColor panelSurface() {
    return editorNeutral(NUIColor(0.043f, 0.041f, 0.052f, 0.98f));
}
NUIColor insetSurface() {
    return editorNeutral(NUIColor(0.022f, 0.021f, 0.027f, 0.98f));
}

std::shared_ptr<NUIIcon> chevronLeftIcon() {
    static auto icon = std::make_shared<NUIIcon>(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M15 6l-6 6 6 6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronRightIcon() {
    static auto icon = std::make_shared<NUIIcon>(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M9 6l6 6-6 6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronDownIcon() {
    static auto icon = std::make_shared<NUIIcon>(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 9l6 6 6-6"/>
        </svg>
    )svg");
    return icon;
}

std::shared_ptr<NUIIcon> chevronUpIcon() {
    static auto icon = std::make_shared<NUIIcon>(R"svg(
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
             stroke-linecap="round" stroke-linejoin="round">
            <path d="M6 15l6-6 6 6"/>
        </svg>
    )svg");
    return icon;
}

void drawSvgIcon(NUIRenderer& renderer, const std::shared_ptr<NUIIcon>& icon, const NUIRect& bounds,
                 const NUIColor& color, float size) {
    if (!icon) {
        return;
    }
    icon->setBounds(
        {std::round(bounds.center().x - size * 0.5f), std::round(bounds.center().y - size * 0.5f), size, size});
    icon->setColor(color);
    icon->onRender(renderer);
}

NUIColor presetAccent(size_t presetIndex) {
    if (presetIndex < 4) {
        return impactAccent();
    }
    if (presetIndex < 8) {
        return cleanAccent();
    }
    if (presetIndex < 12) {
        return bodyAccent();
    }
    return NUIColor(0.30f, 0.82f, 0.68f, 1.0f);
}

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color) {
    if (endAngle <= startAngle + 0.001f) {
        return;
    }
    std::array<NUIPoint, 49> points{};
    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points.size() - 1);
        const float angle = startAngle + (endAngle - startAngle) * t;
        points[i] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
}

NUIColor controlAccent(uint32_t parameterId) {
    switch (parameterId) {
    case 9:
        return impactAccent();
    case 4:
        return sweepAccent();
    case 24:
        return cleanAccent();
    default:
        return bodyAccent();
    }
}
} // namespace

RumblePluginEditor::RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("RumblePluginEditor");
    setPanelTitle("Aestra Rumble");
    setBadgeText("808 Instrument");
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
        uint32_t parameterId;
        const char* title;
        const char* subtitle;
        Zone zone;
    };
    const Meta meta[] = {
        {9, "PUNCH", "Chest hit", Zone::Impact},
        {4, "SWEEP", "Pitch strike", Zone::Impact},
        {0, "DECAY", "Tail length", Zone::Impact},
        {1, "DRIVE", "Weight + grit", Zone::Body},
        {23, "HARMONICS", "Speaker translation", Zone::Body},
        {24, "CLEAN SUB", "Fundamental hold", Zone::Body},
        {2, "TONE", "Top-end focus", Zone::Body},
        {3, "OUTPUT", "Final level", Zone::Body},
    };

    const auto parameters = m_instance->getParameters();
    // The macro layout, preset application, and preset matching all assume the
    // full 25-parameter contract. Silently dropping a missing ID would shift
    // the index-based zones while presets still write every ID — reject the
    // incompatible instance outright instead of rendering a lying panel.
    for (const auto& item : meta) {
        if (item.parameterId >= parameters.size()) {
            Aestra::Log::error("[RumbleEditor] Instance exposes " + std::to_string(parameters.size()) +
                               " parameters; id " + std::to_string(item.parameterId) +
                               " missing — refusing to build controls");
            m_controls.clear();
            return;
        }
    }
    m_controls.reserve(std::size(meta));
    for (const auto& item : meta) {
        MacroControl control;
        control.parameterId = item.parameterId;
        control.title = item.title;
        control.subtitle = item.subtitle;
        control.defaultValue = parameters[item.parameterId].defaultValue;
        control.normalizedValue = m_instance->getParameter(item.parameterId);
        control.zone = item.zone;
        m_controls.push_back(std::move(control));
    }
    layoutControls();
}

void RumblePluginEditor::layoutControls() {
    const auto bounds = getBounds();
    const float contentTop = bounds.y + AestraPanelWindow::TITLE_BAR_H + kPadding;
    const float contentWidth = std::max(0.0f, bounds.width - kPadding * 2.0f);
    m_headerBounds = NUIRect(bounds.x + kPadding, contentTop, contentWidth, kHeaderHeight);

    const float presetWidth = std::clamp(contentWidth * 0.43f, 310.0f, 370.0f);
    m_previousPresetBounds = NUIRect(m_headerBounds.right() - presetWidth, m_headerBounds.y + 14.0f, 34.0f, 48.0f);
    m_nextPresetBounds = NUIRect(m_headerBounds.right() - 34.0f, m_headerBounds.y + 14.0f, 34.0f, 48.0f);
    m_presetButtonBounds =
        NUIRect(m_previousPresetBounds.right() + 6.0f, m_headerBounds.y + 14.0f, presetWidth - 80.0f, 48.0f);

    const float mainY = m_headerBounds.bottom() + 12.0f;
    const float mainHeight = std::max(0.0f, bounds.bottom() - kPadding - mainY);
    const float gap = 12.0f;
    const float impactWidth = std::round((contentWidth - gap) * 0.53f);
    m_impactPanelBounds = NUIRect(bounds.x + kPadding, mainY, impactWidth, mainHeight);
    m_bodyPanelBounds = NUIRect(m_impactPanelBounds.right() + gap, mainY, contentWidth - impactWidth - gap, mainHeight);
    m_impactShapeBounds = NUIRect(m_impactPanelBounds.x + 16.0f, m_impactPanelBounds.y + 42.0f,
                                  m_impactPanelBounds.width - 32.0f, 100.0f);

    const float impactKnobY = m_impactShapeBounds.bottom() + 22.0f;
    const float impactKnobWidth = (m_impactPanelBounds.width - 44.0f) / 3.0f;
    for (size_t i = 0; i < std::min<size_t>(3, m_controls.size()); ++i) {
        auto& control = m_controls[i];
        control.bounds = NUIRect(m_impactPanelBounds.x + 12.0f + static_cast<float>(i) * (impactKnobWidth + 10.0f),
                                 impactKnobY, impactKnobWidth, m_impactPanelBounds.bottom() - impactKnobY - 12.0f);
        const float knobSize = std::min(88.0f, control.bounds.width - 20.0f);
        control.knobBounds =
            NUIRect(control.bounds.center().x - knobSize * 0.5f, control.bounds.y + 16.0f, knobSize, knobSize);
    }

    const float bodyTop = m_bodyPanelBounds.y + 45.0f;
    const float bodyGap = 8.0f;
    const float bodyCellWidth = (m_bodyPanelBounds.width - 32.0f - bodyGap * 2.0f) / 3.0f;
    const float bodyCellHeight = (m_bodyPanelBounds.height - 59.0f - bodyGap) * 0.5f;
    for (size_t offset = 0; offset < 5 && offset + 3 < m_controls.size(); ++offset) {
        auto& control = m_controls[offset + 3];
        const size_t row = offset < 3 ? 0 : 1;
        const size_t column = offset < 3 ? offset : offset - 3;
        const float rowX = row == 0 ? m_bodyPanelBounds.x + 12.0f
                                    : m_bodyPanelBounds.center().x - (bodyCellWidth * 2.0f + bodyGap) * 0.5f;
        control.bounds =
            NUIRect(rowX + static_cast<float>(column) * (bodyCellWidth + bodyGap),
                    bodyTop + static_cast<float>(row) * (bodyCellHeight + bodyGap), bodyCellWidth, bodyCellHeight);
        const float knobSize = std::min(68.0f, std::min(control.bounds.width - 18.0f, control.bounds.height - 54.0f));
        control.knobBounds =
            NUIRect(control.bounds.center().x - knobSize * 0.5f, control.bounds.y + 12.0f, knobSize, knobSize);
    }

    m_presetMenuBounds = NUIRect(m_impactPanelBounds.x, m_impactPanelBounds.y,
                                 m_bodyPanelBounds.right() - m_impactPanelBounds.x, m_impactPanelBounds.height);
    const float menuPad = 18.0f;
    const float columnGap = 10.0f;
    const float columnWidth = (m_presetMenuBounds.width - menuPad * 2.0f - columnGap * 3.0f) / 4.0f;
    const float firstItemY = m_presetMenuBounds.y + 56.0f;
    for (size_t i = 0; i < m_presetItemBounds.size(); ++i) {
        const size_t column = i / 4;
        const size_t row = i % 4;
        m_presetItemBounds[i] =
            NUIRect(m_presetMenuBounds.x + menuPad + static_cast<float>(column) * (columnWidth + columnGap),
                    firstItemY + static_cast<float>(row) * 48.0f, columnWidth, 40.0f);
    }
}

void RumblePluginEditor::syncControlValues() {
    if (!m_instance) {
        return;
    }
    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (static_cast<int>(i) != m_draggingControl) {
            m_controls[i].normalizedValue = m_instance->getParameter(m_controls[i].parameterId);
        }
    }
}

void RumblePluginEditor::drawHeader(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_headerBounds, 14.0f, shellSurface());
    renderer.strokeRoundedRect(m_headerBounds, 14.0f, 1.0f, bodyAccent().withAlpha(0.34f));

    const NUIPoint markCenter{m_headerBounds.x + 35.0f, m_headerBounds.center().y};
    renderer.fillCircle(markCenter, 21.0f, impactAccent().withAlpha(0.13f));
    renderer.strokeCircle(markCenter, 21.0f, 1.4f, impactAccent().withAlpha(0.72f));
    renderer.drawTextCentered("R", NUIRect(markCenter.x - 18.0f, markCenter.y - 18.0f, 36.0f, 36.0f), 17.0f,
                              impactAccent());
    renderer.drawText("RUMBLE", {m_headerBounds.x + 68.0f, m_headerBounds.y + 18.0f}, 17.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText("808 BASS INSTRUMENT", {m_headerBounds.x + 69.0f, m_headerBounds.y + 43.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.82f));

    const auto drawArrowButton = [&](const NUIRect& rect, const std::shared_ptr<NUIIcon>& icon, bool hovered) {
        renderer.fillRoundedRect(rect, 9.0f, hovered ? bodyAccent().withAlpha(0.26f) : insetSurface());
        renderer.strokeRoundedRect(rect, 9.0f, 1.0f,
                                   hovered ? bodyAccent().withAlpha(0.62f) : editorInk(0.08f));
        drawSvgIcon(renderer, icon, rect, hovered ? bodyAccent() : theme.getColor("textPrimary").withAlpha(0.72f),
                    15.0f);
    };
    drawArrowButton(m_previousPresetBounds, chevronLeftIcon(), m_previousPresetHovered);
    drawArrowButton(m_nextPresetBounds, chevronRightIcon(), m_nextPresetHovered);

    const int match = matchingFactoryPreset();
    const bool factory = match >= 0;
    if (factory) {
        m_activePreset = static_cast<size_t>(match);
    }
    const auto& preset = Aestra::Plugins::kRumbleFactoryPresets[m_activePreset];
    renderer.fillRoundedRect(m_presetButtonBounds, 9.0f,
                             m_presetButtonHovered || m_presetMenuOpen ? bodyAccent().withAlpha(0.16f)
                                                                       : insetSurface());
    renderer.strokeRoundedRect(m_presetButtonBounds, 9.0f, 1.0f,
                               m_presetButtonHovered || m_presetMenuOpen ? bodyAccent().withAlpha(0.65f)
                                                                         : editorInk(0.09f));
    renderer.fillRoundedRect({m_presetButtonBounds.x + 9.0f, m_presetButtonBounds.y + 10.0f, 3.0f, 28.0f}, 1.5f,
                             (factory ? presetAccent(m_activePreset) : bodyAccent()).withAlpha(0.88f));
    renderer.drawText(factory ? std::string(preset.category) : "CUSTOM",
                      {m_presetButtonBounds.x + 19.0f, m_presetButtonBounds.y + 8.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.86f));
    renderer.drawText(factory ? std::string(preset.name) : "Edited Sound",
                      {m_presetButtonBounds.x + 19.0f, m_presetButtonBounds.y + 25.0f}, 11.5f,
                      factory ? presetAccent(m_activePreset) : theme.getColor("textPrimary"));
    renderer.drawLine({m_presetButtonBounds.right() - 32.0f, m_presetButtonBounds.y + 8.0f},
                      {m_presetButtonBounds.right() - 32.0f, m_presetButtonBounds.bottom() - 8.0f}, 1.0f,
                      editorInk(0.07f));
    const NUIRect chevronBounds(m_presetButtonBounds.right() - 30.0f, m_presetButtonBounds.y, 28.0f,
                                m_presetButtonBounds.height);
    drawSvgIcon(renderer, m_presetMenuOpen ? chevronUpIcon() : chevronDownIcon(), chevronBounds,
                bodyAccent().withAlpha(m_presetButtonHovered || m_presetMenuOpen ? 0.92f : 0.66f), 12.0f);
}

void RumblePluginEditor::drawImpactPanel(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_impactPanelBounds, 16.0f, panelSurface());
    renderer.strokeRoundedRect(m_impactPanelBounds, 16.0f, 1.0f, impactAccent().withAlpha(0.24f));
    renderer.fillRoundedRect({m_impactPanelBounds.x + 12.0f, m_impactPanelBounds.y + 13.0f, 4.0f, 16.0f}, 2.0f,
                             impactAccent());
    renderer.drawText("IMPACT", {m_impactPanelBounds.x + 24.0f, m_impactPanelBounds.y + 14.0f}, 10.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText("ATTACK / PITCH / TAIL", {m_impactPanelBounds.right() - 142.0f, m_impactPanelBounds.y + 15.0f},
                      8.0f, theme.getColor("textSecondary").withAlpha(0.65f));
    drawImpactShape(renderer);
    for (size_t i = 0; i < std::min<size_t>(3, m_controls.size()); ++i) {
        drawKnob(renderer, m_controls[i], static_cast<int>(i), true);
    }
}

void RumblePluginEditor::drawBodyPanel(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_bodyPanelBounds, 16.0f, panelSurface());
    renderer.strokeRoundedRect(m_bodyPanelBounds, 16.0f, 1.0f, bodyAccent().withAlpha(0.24f));
    renderer.fillRoundedRect({m_bodyPanelBounds.x + 12.0f, m_bodyPanelBounds.y + 13.0f, 4.0f, 16.0f}, 2.0f,
                             bodyAccent());
    renderer.drawText("WEIGHT + COLOR", {m_bodyPanelBounds.x + 24.0f, m_bodyPanelBounds.y + 14.0f}, 10.0f,
                      theme.getColor("textPrimary"));
    for (size_t i = 3; i < m_controls.size(); ++i) {
        drawKnob(renderer, m_controls[i], static_cast<int>(i), false);
    }
}

void RumblePluginEditor::drawImpactShape(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_impactShapeBounds, 11.0f, insetSurface());
    renderer.strokeRoundedRect(m_impactShapeBounds, 11.0f, 1.0f, impactAccent().withAlpha(0.16f));
    for (int i = 1; i < 4; ++i) {
        const float x = m_impactShapeBounds.x + m_impactShapeBounds.width * static_cast<float>(i) / 4.0f;
        renderer.drawLine({x, m_impactShapeBounds.y + 12.0f}, {x, m_impactShapeBounds.bottom() - 12.0f}, 1.0f,
                          editorInk(0.035f));
    }
    const float punch = m_controls.size() > 0 ? m_controls[0].normalizedValue : 0.0f;
    const float sweep = m_controls.size() > 1 ? m_controls[1].normalizedValue : 0.0f;
    const float decay = m_controls.size() > 2 ? m_controls[2].normalizedValue : 0.0f;
    std::array<NUIPoint, 97> points{};
    float phase = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points.size() - 1);
        const float frequency = 2.0f + 8.0f * sweep * std::exp(-t * 7.0f);
        phase += frequency * 0.13f;
        const float envelope = std::exp(-t * (2.0f + 7.0f * (1.0f - decay)));
        const float attack = 0.34f + punch * 0.62f * std::exp(-t * 13.0f);
        points[i] = {m_impactShapeBounds.x + 10.0f + t * (m_impactShapeBounds.width - 20.0f),
                     m_impactShapeBounds.center().y -
                         std::sin(phase) * envelope * attack * (m_impactShapeBounds.height * 0.40f)};
    }
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), 2.2f, impactAccent().withAlpha(0.92f));
    renderer.drawText("TRANSIENT SHAPE", {m_impactShapeBounds.x + 11.0f, m_impactShapeBounds.y + 9.0f}, 7.5f,
                      theme.getColor("textSecondary").withAlpha(0.60f));
}

void RumblePluginEditor::drawKnob(NUIRenderer& renderer, const MacroControl& control, int controlIndex, bool primary) {
    auto& theme = NUIThemeManager::getInstance();
    const bool hovered = m_hoveredControl == controlIndex;
    const bool dragging = m_draggingControl == controlIndex;
    const NUIColor accent = controlAccent(control.parameterId);
    renderer.fillRoundedRect(control.bounds, 11.0f,
                             hovered || dragging ? editorNeutral(NUIColor(0.068f, 0.064f, 0.080f, 0.98f))
                                                 : editorNeutral(NUIColor(0.036f, 0.034f, 0.043f, 0.82f)));
    renderer.strokeRoundedRect(control.bounds, 11.0f, 1.0f,
                               hovered || dragging ? accent.withAlpha(0.50f) : editorInk(0.045f));

    const NUIPoint center = control.knobBounds.center();
    const float radius = control.knobBounds.width * 0.5f;
    const float valueAngle = kKnobStart + control.normalizedValue * kKnobSweep;
    renderer.fillCircle(center, radius, insetSurface());
    renderer.strokeCircle(center, radius, 1.0f, editorInk(0.07f));
    drawArc(renderer, center, radius - 6.0f, kKnobStart, kKnobStart + kKnobSweep, primary ? 4.0f : 3.2f,
            editorInk(0.09f));
    drawArc(renderer, center, radius - 6.0f, kKnobStart, valueAngle, primary ? 4.0f : 3.2f, accent.withAlpha(0.94f));
    const float needleLength = radius - (primary ? 16.0f : 13.0f);
    const NUIPoint tip{center.x + std::cos(valueAngle) * needleLength, center.y + std::sin(valueAngle) * needleLength};
    renderer.drawLine(center, tip, 2.0f, accent.withAlpha(0.86f));
    renderer.fillCircle(center, primary ? 7.0f : 5.5f, editorNeutral(NUIColor(0.075f, 0.071f, 0.088f, 1.0f)));
    renderer.fillCircle(center, primary ? 2.8f : 2.2f, accent.withAlpha(0.90f));

    const float labelY = control.knobBounds.bottom() + (primary ? 8.0f : 5.0f);
    renderer.drawTextCentered(control.title, NUIRect(control.bounds.x, labelY, control.bounds.width, 14.0f),
                              primary ? 10.5f : 9.0f, theme.getColor("textPrimary").withAlpha(0.94f));
    renderer.drawTextCentered(m_instance ? m_instance->getParameterDisplay(control.parameterId) : "",
                              NUIRect(control.bounds.x, labelY + 17.0f, control.bounds.width, 13.0f),
                              primary ? 10.0f : 8.5f, accent.withAlpha(0.96f));
    if (primary && control.bounds.bottom() - labelY > 47.0f) {
        renderer.drawTextCentered(control.subtitle,
                                  NUIRect(control.bounds.x, labelY + 35.0f, control.bounds.width, 12.0f), 8.0f,
                                  theme.getColor("textSecondary").withAlpha(0.62f));
    }
}

void RumblePluginEditor::drawPresetBrowser(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_presetMenuBounds, 16.0f, editorNeutral(NUIColor(0.018f, 0.017f, 0.023f, 1.0f)));
    renderer.strokeRoundedRect(m_presetMenuBounds, 16.0f, 1.2f, bodyAccent().withAlpha(0.58f));
    renderer.drawText("FACTORY SOUNDS", {m_presetMenuBounds.x + 18.0f, m_presetMenuBounds.y + 17.0f}, 11.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText("16 starting points / 4 characters",
                      {m_presetMenuBounds.x + 132.0f, m_presetMenuBounds.y + 19.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.70f));

    for (size_t column = 0; column < 4; ++column) {
        const size_t firstIndex = column * 4;
        const auto& firstPreset = Aestra::Plugins::kRumbleFactoryPresets[firstIndex];
        renderer.drawText(std::string(firstPreset.category),
                          {m_presetItemBounds[column * 4].x + 3.0f, m_presetMenuBounds.y + 41.0f}, 8.0f,
                          presetAccent(firstIndex).withAlpha(0.88f));
    }
    for (size_t i = 0; i < Aestra::Plugins::kRumbleFactoryPresets.size(); ++i) {
        const bool active = i == m_activePreset;
        const bool hovered = static_cast<int>(i) == m_hoveredPreset;
        const NUIColor accent = presetAccent(i);
        renderer.fillRoundedRect(m_presetItemBounds[i], 8.0f,
                                 active    ? accent.withAlpha(0.22f)
                                 : hovered ? editorNeutral(NUIColor(0.095f, 0.088f, 0.115f, 0.96f))
                                           : panelSurface());
        renderer.strokeRoundedRect(m_presetItemBounds[i], 8.0f, 1.0f,
                                   active || hovered ? accent.withAlpha(0.58f) : editorInk(0.055f));
        renderer.drawText(std::string(Aestra::Plugins::kRumbleFactoryPresets[i].name),
                          {m_presetItemBounds[i].x + 10.0f, m_presetItemBounds[i].y + 13.0f}, 9.5f,
                          active ? accent : theme.getColor("textPrimary").withAlpha(0.88f));
    }

    const size_t described = m_hoveredPreset >= 0 ? static_cast<size_t>(m_hoveredPreset) : m_activePreset;
    const auto& preset = Aestra::Plugins::kRumbleFactoryPresets[described];
    const NUIRect descriptionBounds(m_presetMenuBounds.x + 18.0f, m_presetMenuBounds.bottom() - 54.0f,
                                    m_presetMenuBounds.width - 36.0f, 36.0f);
    renderer.fillRoundedRect(descriptionBounds, 8.0f, insetSurface());
    renderer.drawText(std::string(preset.name), {descriptionBounds.x + 11.0f, descriptionBounds.y + 8.0f}, 9.0f,
                      presetAccent(described));
    renderer.drawText(std::string(preset.description), {descriptionBounds.x + 112.0f, descriptionBounds.y + 8.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.82f));
}

void RumblePluginEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    syncControlValues();
    const auto bounds = getBounds();
    renderer.fillRect({bounds.x, bounds.y + AestraPanelWindow::TITLE_BAR_H, bounds.width,
                       bounds.height - AestraPanelWindow::TITLE_BAR_H},
                      shellSurface());
    drawHeader(renderer);
    drawImpactPanel(renderer);
    drawBodyPanel(renderer);
    if (m_presetMenuOpen) {
        drawPresetBrowser(renderer);
    }
}

int RumblePluginEditor::hitTestControl(NUIPoint point) const {
    for (size_t i = 0; i < m_controls.size(); ++i) {
        if (m_controls[i].bounds.contains(point)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void RumblePluginEditor::setControlValue(int controlIndex, float normalizedValue) {
    if (!m_instance || controlIndex < 0 || controlIndex >= static_cast<int>(m_controls.size())) {
        return;
    }
    auto& control = m_controls[static_cast<size_t>(controlIndex)];
    control.normalizedValue = std::clamp(normalizedValue, 0.0f, 1.0f);
    m_instance->setParameter(control.parameterId, control.normalizedValue);
    setDirty(true);
}

void RumblePluginEditor::applyFactoryPreset(size_t presetIndex) {
    if (!m_instance || Aestra::Plugins::kRumbleFactoryPresets.empty()) {
        return;
    }
    m_activePreset = presetIndex % Aestra::Plugins::kRumbleFactoryPresets.size();
    const auto& preset = Aestra::Plugins::kRumbleFactoryPresets[m_activePreset];
    for (uint32_t parameterId = 0; parameterId < preset.values.size(); ++parameterId) {
        m_instance->setParameter(parameterId, preset.values[parameterId]);
    }
    syncControlValues();
    setDirty(true);
}

int RumblePluginEditor::matchingFactoryPreset() const {
    if (!m_instance) {
        return -1;
    }
    for (size_t presetIndex = 0; presetIndex < Aestra::Plugins::kRumbleFactoryPresets.size(); ++presetIndex) {
        const auto& values = Aestra::Plugins::kRumbleFactoryPresets[presetIndex].values;
        bool matches = true;
        for (uint32_t parameterId = 0; parameterId < values.size(); ++parameterId) {
            if (std::abs(m_instance->getParameter(parameterId) - values[parameterId]) > 1.0e-5f) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return static_cast<int>(presetIndex);
        }
    }
    return -1;
}

bool RumblePluginEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) {
        return false;
    }
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }
    if (!m_instance) {
        return false;
    }

    // Handle an active knob drag before the bounds check: a release outside
    // the panel must still end the drag, or the knob stays latched to the
    // pointer when it re-enters.
    if (m_draggingControl >= 0) {
        if (event.released) {
            endKnobCapture();
            m_draggingControl = -1;
            setDirty(true);
            return true;
        }
        if (event.button == NUIMouseButton::None || event.type == NUIMouseEventType::Drag) {
            // Service-owned frame delta (up = increase), accumulated into value.
            const float cur = m_controls[static_cast<size_t>(m_draggingControl)].normalizedValue;
            setControlValue(m_draggingControl, cur + knobDragStep(event, kDragRangePixels));
            return true;
        }
    }

    const bool contains = getBounds().contains(event.position);
    if (!contains && !isDraggingWindow()) {
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_previousPresetBounds.contains(event.position)) {
            const size_t count = Aestra::Plugins::kRumbleFactoryPresets.size();
            applyFactoryPreset((m_activePreset + count - 1) % count);
            return true;
        }
        if (m_nextPresetBounds.contains(event.position)) {
            applyFactoryPreset((m_activePreset + 1) % Aestra::Plugins::kRumbleFactoryPresets.size());
            return true;
        }
        if (m_presetButtonBounds.contains(event.position)) {
            m_presetMenuOpen = !m_presetMenuOpen;
            m_hoveredPreset = -1;
            setDirty(true);
            return true;
        }
        if (m_presetMenuOpen) {
            for (size_t i = 0; i < m_presetItemBounds.size(); ++i) {
                if (m_presetItemBounds[i].contains(event.position)) {
                    applyFactoryPreset(i);
                    m_presetMenuOpen = false;
                    return true;
                }
            }
            if (!m_presetMenuBounds.contains(event.position)) {
                m_presetMenuOpen = false;
                setDirty(true);
            }
            return true;
        }

        const int controlIndex = hitTestControl(event.position);
        if (controlIndex >= 0) {
            // The platform never populates event.doubleClick — pair up quick
            // same-spot presses manually so reset-to-default actually fires.
            const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
            const bool isDoubleClick =
                event.doubleClick ||
                ((nowMs - m_lastClickTimeMs) < 400 &&
                 std::abs(event.position.x - m_lastClickPos.x) < 5.0f &&
                 std::abs(event.position.y - m_lastClickPos.y) < 5.0f);
            m_lastClickTimeMs = isDoubleClick ? 0 : nowMs;
            m_lastClickPos = event.position;
            if (isDoubleClick) {
                setControlValue(controlIndex, m_controls[static_cast<size_t>(controlIndex)].defaultValue);
                return true;
            }
            m_draggingControl = controlIndex;
            beginKnobCapture(m_controls[static_cast<size_t>(controlIndex)].knobBounds.center(), event.position);
            return true;
        }
    }

    if (!event.pressed && !event.released) {
        m_presetButtonHovered = m_presetButtonBounds.contains(event.position);
        m_previousPresetHovered = m_previousPresetBounds.contains(event.position);
        m_nextPresetHovered = m_nextPresetBounds.contains(event.position);
        m_hoveredControl = m_presetMenuOpen ? -1 : hitTestControl(event.position);
        m_hoveredPreset = -1;
        if (m_presetMenuOpen) {
            for (size_t i = 0; i < m_presetItemBounds.size(); ++i) {
                if (m_presetItemBounds[i].contains(event.position)) {
                    m_hoveredPreset = static_cast<int>(i);
                    break;
                }
            }
        }
        setDirty(true);
    }

    if (event.type == NUIMouseEventType::Scroll && m_hoveredControl >= 0 && !m_presetMenuOpen) {
        const float direction = event.wheelDelta > 0.0f ? 1.0f : -1.0f;
        setControlValue(m_hoveredControl,
                        m_controls[static_cast<size_t>(m_hoveredControl)].normalizedValue + direction * 0.02f);
        return true;
    }
    return contains;
}

void RumblePluginEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
