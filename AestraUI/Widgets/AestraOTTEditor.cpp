// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraOTTEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraOTT.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraOTT;

constexpr float kDragRangePx = 160.0f;    // full param range per drag distance

NUIColor accent() {
    return NUIColor(0.74f, 0.42f, 0.88f, 1.0f);
} // OTT violet
NUIColor panelSurface() {
    return editorNeutral(0.027f, 0.96f);
}
NUIColor insetSurface() {
    return editorNeutral(0.038f, 0.96f);
}

} // namespace

AestraOTTEditor::AestraOTTEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraOTTEditor");
    setPanelTitle("Aestra OTT");
    setBadgeText("Dynamics");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraOTTEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraOTTEditor::layoutControls() {
    const auto b = getBounds();
    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;

    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 26.0f;
    m_bypassRect = NUIRect(b.right() - 44.0f - kBypassW, contentTop + 16.0f, kBypassW, kBypassH);

    // Large depth knob on the left; macro row to its right, band row below.
    const float knobTop = contentTop + 52.0f;
    m_depthRect = NUIRect(b.x + 44.0f, knobTop + 4.0f, 108.0f, 108.0f);
    m_timeRect = NUIRect(b.x + 208.0f, knobTop, 64.0f, 64.0f);
    m_inRect = NUIRect(b.x + 322.0f, knobTop, 64.0f, 64.0f);
    m_outRect = NUIRect(b.x + 436.0f, knobTop, 64.0f, 64.0f);

    const float bandTop = std::min(knobTop + 150.0f, b.bottom() - 92.0f);
    m_lowRect = NUIRect(b.x + 52.0f, bandTop, 52.0f, 52.0f);
    m_midRect = NUIRect(b.x + 142.0f, bandTop, 52.0f, 52.0f);
    m_highRect = NUIRect(b.x + 232.0f, bandTop, 52.0f, 52.0f);
    m_xloRect = NUIRect(b.x + 356.0f, bandTop, 52.0f, 52.0f);
    m_xhiRect = NUIRect(b.x + 446.0f, bandTop, 52.0f, 52.0f);
}

void AestraOTTEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance)
        return;

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f, contentRect.width - 24.0f,
                           contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, editorInk(0.055f));

    drawBypassPill(renderer);
    drawKnob(renderer, m_depthRect, AestraOTT::kDepth, "Depth", true, false);
    drawKnob(renderer, m_timeRect, AestraOTT::kTime, "Time", false, false);
    drawKnob(renderer, m_inRect, AestraOTT::kInGain, "In", false, true);
    drawKnob(renderer, m_outRect, AestraOTT::kOutGain, "Out", false, true);
    drawKnob(renderer, m_lowRect, AestraOTT::kLowGain, "Low", false, true);
    drawKnob(renderer, m_midRect, AestraOTT::kMidGain, "Mid", false, true);
    drawKnob(renderer, m_highRect, AestraOTT::kHighGain, "High", false, true);
    drawKnob(renderer, m_xloRect, AestraOTT::kXoverLow, "X-Low", false, false);
    drawKnob(renderer, m_xhiRect, AestraOTT::kXoverHigh, "X-High", false, false);
}

void AestraOTTEditor::drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                               bool large, bool bipolar) {
    const float value = m_instance ? m_instance->getParameter(paramId) : 0.0f;
    const std::string valueText = m_instance ? m_instance->getParameterDisplay(paramId) : "";
    EditorKit::drawKnob(renderer, rect, value, label, valueText, accent(), insetSurface(),
                        EditorKit::KnobLook{large, bipolar});
}

void AestraOTTEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(AestraOTT::kBypass) > 0.5f;
    constexpr float kRadius = 7.0f;
    constexpr float kFont = 10.0f;
    if (bypassed) {
        renderer.fillRoundedRect(m_bypassRect, kRadius,
                                 NUIColor(0.92f, 0.28f, 0.22f).withAlpha(m_bypassHovered ? 0.94f : 0.78f));
        renderer.strokeRoundedRect(m_bypassRect, kRadius, 1.0f, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(0.50f));
        renderer.drawTextCentered("BYPASSED", m_bypassRect, kFont, theme.getColor("textPrimary"));
    } else {
        renderer.fillRoundedRect(m_bypassRect, kRadius,
                                 theme.getColor("success").withAlpha(m_bypassHovered ? 0.30f : 0.18f));
        renderer.strokeRoundedRect(m_bypassRect, kRadius, 1.0f, theme.getColor("success").withAlpha(0.40f));
        renderer.drawTextCentered("ACTIVE", m_bypassRect, kFont, theme.getColor("success"));
    }
}

bool AestraOTTEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;


    // Bypass pill
    if (m_bypassRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(AestraOTT::kBypass);
        m_instance->setParameter(AestraOTT::kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }
    m_bypassHovered = m_bypassRect.contains(event.position);

    // Knob vertical drag (rotary -> cursor capture): AestraPanelWindow::handleKnobDrag.
    const KnobTarget knobs[] = {
        {m_depthRect, AestraOTT::kDepth},   {m_timeRect, AestraOTT::kTime},    {m_inRect, AestraOTT::kInGain},
        {m_outRect, AestraOTT::kOutGain},   {m_lowRect, AestraOTT::kLowGain},  {m_midRect, AestraOTT::kMidGain},
        {m_highRect, AestraOTT::kHighGain}, {m_xloRect, AestraOTT::kXoverLow}, {m_xhiRect, AestraOTT::kXoverHigh},
    };
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs, std::size(knobs), kDragRangePx, get, set))
        return true;

    return consumeInsideBounds(event);
}

void AestraOTTEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
