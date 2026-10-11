// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraSatEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraSat.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraSat;

constexpr float kDragRangePx = 160.0f;    // full param range per drag distance

NUIColor accent() {
    return NUIColor(0.94f, 0.52f, 0.22f, 1.0f);
} // heat orange
NUIColor panelSurface() {
    return editorNeutral(0.027f, 0.96f);
}
NUIColor insetSurface() {
    return editorNeutral(0.038f, 0.96f);
}

} // namespace

AestraSatEditor::AestraSatEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraSatEditor");
    setPanelTitle("Aestra Sat");
    setBadgeText("Saturator");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraSatEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraSatEditor::layoutControls() {
    const auto b = getBounds();
    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;

    // Mode selector: three segments across the top-left
    constexpr float kSegW = 62.0f;
    constexpr float kSegH = 26.0f;
    for (size_t i = 0; i < m_modeRects.size(); ++i) {
        m_modeRects[i] =
            NUIRect(b.x + 28.0f + static_cast<float>(i) * (kSegW + 6.0f), contentTop + 16.0f, kSegW, kSegH);
    }

    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 26.0f;
    m_bypassRect = NUIRect(b.right() - 44.0f - kBypassW, contentTop + 16.0f, kBypassW, kBypassH);

    // Large drive knob on the left, tone/output stacked on the right
    const float knobTop = contentTop + 56.0f;
    // 116 (was 128) so knob + label (+4, 14) + value (+19, 13) end at +148 and the
    // Mix slider fits below them inside the 300px window. x shifts 6 to keep the centre.
    m_driveRect = NUIRect(b.x + 58.0f, knobTop, 116.0f, 116.0f);
    m_toneRect = NUIRect(b.x + 232.0f, knobTop + 4.0f, 84.0f, 84.0f);
    m_outputRect = NUIRect(b.x + 344.0f, knobTop + 4.0f, 84.0f, 84.0f);

    // Below the Drive value (ends knobTop + 148), with a gap.
    const float sliderY = std::min(knobTop + 154.0f, b.bottom() - 34.0f);
    m_mixRect = NUIRect(b.x + 58.0f, sliderY, b.width - 116.0f, 34.0f);
}

void AestraSatEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance)
        return;

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f, contentRect.width - 24.0f,
                           contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, editorInk(0.055f));

    drawModeSelector(renderer);
    drawBypassPill(renderer);
    drawKnob(renderer, m_driveRect, AestraSat::kDrive, "Drive", true);
    drawKnob(renderer, m_toneRect, AestraSat::kTone, "Tone", false);
    drawKnob(renderer, m_outputRect, AestraSat::kOutput, "Output", false);
    drawMixSlider(renderer);
}

void AestraSatEditor::drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                               bool large) {
    const float value = m_instance ? m_instance->getParameter(paramId) : 0.0f;
    const std::string valueText = m_instance ? m_instance->getParameterDisplay(paramId) : "";
    EditorKit::drawKnob(renderer, rect, value, label, valueText, accent(), insetSurface(),
                        EditorKit::KnobLook{large, false, 10.0f, 0.30f, 10.0f});
}

void AestraSatEditor::drawModeSelector(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto mode =
        m_instance ? AestraSat::modeFromNorm(m_instance->getParameter(AestraSat::kMode)) : AestraSat::kModeTape;
    static constexpr const char* kLabels[] = {"TAPE", "TUBE", "HARD"};
    for (size_t i = 0; i < m_modeRects.size(); ++i) {
        const bool selected = static_cast<uint32_t>(mode) == i;
        if (selected) {
            renderer.fillRoundedRect(m_modeRects[i], 7.0f, accent().withAlpha(0.26f));
            renderer.strokeRoundedRect(m_modeRects[i], 7.0f, 1.0f, accent().withAlpha(0.62f));
            renderer.drawTextCentered(kLabels[i], m_modeRects[i], 10.0f, accent().withAlpha(0.98f));
        } else {
            renderer.fillRoundedRect(m_modeRects[i], 7.0f, insetSurface());
            renderer.strokeRoundedRect(m_modeRects[i], 7.0f, 1.0f, editorInk(0.08f));
            renderer.drawTextCentered(kLabels[i], m_modeRects[i], 10.0f,
                                      theme.getColor("textPrimary").withAlpha(0.62f));
        }
    }
}

void AestraSatEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(AestraSat::kBypass) > 0.5f;
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

void AestraSatEditor::drawMixSlider(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = m_instance ? m_instance->getParameter(AestraSat::kMix) : 1.0f;
    // Track stops short of the right-aligned percentage so the thumb never covers it.
    const NUIRect track(m_mixRect.x + 38.0f, m_mixRect.y + 12.0f, m_mixRect.width - 104.0f, 8.0f);

    renderer.fillRoundedRect(m_mixRect, 10.0f, insetSurface());
    renderer.strokeRoundedRect(m_mixRect, 10.0f, 1.0f, accent().withAlpha(m_draggingMix ? 0.62f : 0.34f));
    renderer.drawText("Mix", {m_mixRect.x + 14.0f, m_mixRect.y + 11.0f}, 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.95f));
    renderer.fillRoundedRect(track, 4.0f, editorInk(0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 4.0f, accent().withAlpha(0.92f));
    const NUIPoint thumb{track.x + track.width * mix, track.center().y};
    renderer.fillCircle(thumb, 10.0f, accent().withAlpha(0.18f));
    renderer.fillCircle(thumb, 7.0f, theme.getColor("textPrimary"));
    const std::string pctStr = std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%";
    const float pctW = renderer.measureText(pctStr, 10.0f).width;
    renderer.drawText(pctStr, {m_mixRect.right() - 14.0f - pctW, m_mixRect.y + 10.0f}, 10.0f,
                      accent().withAlpha(0.96f));
}

bool AestraSatEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;

    const float mx = event.position.x;

    // Bypass pill
    if (m_bypassRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(AestraSat::kBypass);
        m_instance->setParameter(AestraSat::kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }
    m_bypassHovered = m_bypassRect.contains(event.position);

    // Mode segments
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_modeRects.size(); ++i) {
            if (m_modeRects[i].contains(event.position)) {
                m_instance->setParameter(AestraSat::kMode, AestraSat::normFromMode(static_cast<AestraSat::Mode>(i)));
                setDirty();
                return true;
            }
        }
    }

    // Knob vertical drag (rotary -> cursor capture): AestraPanelWindow::handleKnobDrag.
    const KnobTarget knobs[] = {
        {m_driveRect, AestraSat::kDrive},
        {m_toneRect, AestraSat::kTone},
        {m_outputRect, AestraSat::kOutput},
    };
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs, std::size(knobs), kDragRangePx, get, set))
        return true;

    // Mix slider drag
    const NUIRect mixTrack(m_mixRect.x + 38.0f, m_mixRect.y + 6.0f, m_mixRect.width - 104.0f, m_mixRect.height - 12.0f);
    if (m_draggingMix) {
        if (event.released) {
            m_draggingMix = false;
            return true;
        }
        if (event.button == NUIMouseButton::None) {
            const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
            m_instance->setParameter(AestraSat::kMix, t);
            setDirty();
            return true;
        }
    }
    if (event.pressed && event.button == NUIMouseButton::Left && mixTrack.contains(event.position)) {
        m_draggingMix = true;
        const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
        m_instance->setParameter(AestraSat::kMix, t);
        setDirty();
        return true;
    }

    return consumeInsideBounds(event);
}

void AestraSatEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
