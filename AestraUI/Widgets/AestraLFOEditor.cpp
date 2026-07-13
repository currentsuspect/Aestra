// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLFOEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraLFO.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraLFO;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kKnobSweep = kPi * 1.5f;
constexpr float kKnobStart = kPi * 0.75f; // pointing down-left, sweeping clockwise
constexpr float kDragRangePx = 160.0f;    // full param range per drag distance

NUIColor accent() {
    return NUIColor(0.36f, 0.62f, 0.92f, 1.0f);
} // LFO blue
NUIColor panelSurface() {
    return NUIColor(0.027f, 0.027f, 0.027f, 0.96f);
}
NUIColor insetSurface() {
    return NUIColor(0.038f, 0.038f, 0.038f, 0.96f);
}

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color) {
    if (endAngle < startAngle)
        std::swap(startAngle, endAngle);
    if (endAngle - startAngle <= 0.001f)
        return;
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

AestraLFOEditor::AestraLFOEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraLFOEditor");
    setPanelTitle("Aestra LFO");
    setBadgeText("Modulation");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraLFOEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraLFOEditor::layoutControls() {
    const auto b = getBounds();
    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;

    // Target selector: three segments across the top-left
    constexpr float kSegW = 62.0f;
    constexpr float kSegH = 26.0f;
    for (size_t i = 0; i < m_targetRects.size(); ++i) {
        m_targetRects[i] =
            NUIRect(b.x + 28.0f + static_cast<float>(i) * (kSegW + 6.0f), contentTop + 14.0f, kSegW, kSegH);
    }

    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 26.0f;
    m_bypassRect = NUIRect(b.right() - 44.0f - kBypassW, contentTop + 14.0f, kBypassW, kBypassH);
    m_syncRect = NUIRect(m_bypassRect.x - 70.0f - 12.0f, contentTop + 14.0f, 70.0f, kBypassH);

    // Wave selector: six segments on the second row
    constexpr float kWaveW = 52.0f;
    constexpr float kWaveH = 24.0f;
    for (size_t i = 0; i < m_waveRects.size(); ++i) {
        m_waveRects[i] =
            NUIRect(b.x + 28.0f + static_cast<float>(i) * (kWaveW + 6.0f), contentTop + 52.0f, kWaveW, kWaveH);
    }

    // Large rate knob on the left; small knobs to its right
    const float knobTop = contentTop + 96.0f;
    m_rateRect = NUIRect(b.x + 44.0f, knobTop, 104.0f, 104.0f);
    m_depthRect = NUIRect(b.x + 216.0f, knobTop + 16.0f, 64.0f, 64.0f);
    m_phaseRect = NUIRect(b.x + 330.0f, knobTop + 16.0f, 64.0f, 64.0f);
    m_smoothRect = NUIRect(b.x + 444.0f, knobTop + 16.0f, 64.0f, 64.0f);
}

void AestraLFOEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance)
        return;

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f, contentRect.width - 24.0f,
                           contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.055f));

    drawTargetSelector(renderer);
    drawWaveSelector(renderer);
    drawSyncPill(renderer);
    drawBypassPill(renderer);
    drawRateKnob(renderer);
    drawKnob(renderer, m_depthRect, AestraLFO::kDepth, "Depth", false);
    drawKnob(renderer, m_phaseRect, AestraLFO::kPhase, "Phase", false);
    drawKnob(renderer, m_smoothRect, AestraLFO::kSmooth, "Smooth", false);
}

void AestraLFOEditor::drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                               bool large) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance ? m_instance->getParameter(paramId) : 0.0f;
    const NUIPoint c = rect.center();
    const float r = rect.width * 0.5f - 4.0f;
    const float angle = kKnobStart + value * kKnobSweep;

    renderer.fillCircle(c, r + 4.0f, insetSurface());
    renderer.strokeCircle(c, r + 4.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.060f));

    drawArc(renderer, c, r - 3.0f, kKnobStart, kKnobStart + kKnobSweep, large ? 4.0f : 3.0f,
            NUIColor(0.199f, 0.199f, 0.199f, 1.0f));
    drawArc(renderer, c, r - 3.0f, kKnobStart, angle, large ? 4.0f : 3.0f, accent().withAlpha(0.92f));

    const float needleLen = r - (large ? 14.0f : 9.0f);
    const NUIPoint tip(c.x + std::cos(angle) * needleLen, c.y + std::sin(angle) * needleLen);
    renderer.drawLine(c, tip, 2.0f, accent().withAlpha(0.85f));
    renderer.fillCircle(tip, large ? 3.5f : 2.5f, accent());

    const float wellR = r * (large ? 0.34f : 0.28f);
    renderer.fillCircle(c, wellR, NUIColor(0.045f, 0.045f, 0.045f, 0.96f));
    renderer.strokeCircle(c, wellR, 1.2f, accent().withAlpha(0.36f));

    renderer.drawTextCentered(label, NUIRect(rect.x, rect.bottom() + 4.0f, rect.width, 14.0f), 10.5f,
                              theme.getColor("textPrimary").withAlpha(0.95f));
    const std::string valStr = m_instance ? m_instance->getParameterDisplay(paramId) : "";
    renderer.drawTextCentered(valStr, NUIRect(rect.x - 14.0f, rect.bottom() + 19.0f, rect.width + 28.0f, 13.0f),
                              large ? 10.0f : 9.0f, accent().withAlpha(0.96f));
}

void AestraLFOEditor::drawRateKnob(NUIRenderer& renderer) {
    // The rate knob edits Hz in free mode and the note division when synced.
    const bool sync = m_instance && m_instance->getParameter(AestraLFO::kSyncMode) > 0.5f;
    drawKnob(renderer, m_rateRect, sync ? AestraLFO::kNoteDivision : AestraLFO::kRateHz, sync ? "Rate (Sync)" : "Rate",
             true);
}

void AestraLFOEditor::drawTargetSelector(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto target =
        m_instance ? AestraLFO::targetFromNorm(m_instance->getParameter(AestraLFO::kTarget)) : AestraLFO::kTargetVolume;
    static constexpr const char* kLabels[] = {"VOL", "PAN", "CUT"};
    for (size_t i = 0; i < m_targetRects.size(); ++i) {
        const bool selected = static_cast<uint32_t>(target) == i;
        if (selected) {
            renderer.fillRoundedRect(m_targetRects[i], 7.0f, accent().withAlpha(0.26f));
            renderer.strokeRoundedRect(m_targetRects[i], 7.0f, 1.0f, accent().withAlpha(0.62f));
            renderer.drawTextCentered(kLabels[i], m_targetRects[i], 10.5f, accent().withAlpha(0.98f));
        } else {
            renderer.fillRoundedRect(m_targetRects[i], 7.0f, insetSurface());
            renderer.strokeRoundedRect(m_targetRects[i], 7.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
            renderer.drawTextCentered(kLabels[i], m_targetRects[i], 10.5f,
                                      theme.getColor("textPrimary").withAlpha(0.62f));
        }
    }
}

void AestraLFOEditor::drawWaveSelector(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto wave =
        m_instance ? AestraLFO::waveFromNorm(m_instance->getParameter(AestraLFO::kWave)) : AestraLFO::kWaveSine;
    static constexpr const char* kLabels[] = {"SIN", "TRI", "SAW", "RMP", "SQR", "S&H"};
    for (size_t i = 0; i < m_waveRects.size(); ++i) {
        const bool selected = static_cast<uint32_t>(wave) == i;
        if (selected) {
            renderer.fillRoundedRect(m_waveRects[i], 7.0f, accent().withAlpha(0.26f));
            renderer.strokeRoundedRect(m_waveRects[i], 7.0f, 1.0f, accent().withAlpha(0.62f));
            renderer.drawTextCentered(kLabels[i], m_waveRects[i], 10.0f, accent().withAlpha(0.98f));
        } else {
            renderer.fillRoundedRect(m_waveRects[i], 7.0f, insetSurface());
            renderer.strokeRoundedRect(m_waveRects[i], 7.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
            renderer.drawTextCentered(kLabels[i], m_waveRects[i], 10.0f,
                                      theme.getColor("textPrimary").withAlpha(0.62f));
        }
    }
}

void AestraLFOEditor::drawSyncPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool sync = m_instance && m_instance->getParameter(AestraLFO::kSyncMode) > 0.5f;
    constexpr float kRadius = 7.0f;
    if (sync) {
        renderer.fillRoundedRect(m_syncRect, kRadius, accent().withAlpha(0.26f));
        renderer.strokeRoundedRect(m_syncRect, kRadius, 1.0f, accent().withAlpha(0.62f));
        renderer.drawTextCentered("SYNC", m_syncRect, 10.0f, accent().withAlpha(0.98f));
    } else {
        renderer.fillRoundedRect(m_syncRect, kRadius, insetSurface());
        renderer.strokeRoundedRect(m_syncRect, kRadius, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
        renderer.drawTextCentered("FREE", m_syncRect, 10.0f, theme.getColor("textPrimary").withAlpha(0.62f));
    }
}

void AestraLFOEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(AestraLFO::kBypass) > 0.5f;
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

bool AestraLFOEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;

    const float my = event.position.y;

    // Bypass pill
    if (m_bypassRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(AestraLFO::kBypass);
        m_instance->setParameter(AestraLFO::kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }
    m_bypassHovered = m_bypassRect.contains(event.position);

    // Sync pill
    if (m_syncRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(AestraLFO::kSyncMode);
        m_instance->setParameter(AestraLFO::kSyncMode, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }

    // Target / wave segments
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_targetRects.size(); ++i) {
            if (m_targetRects[i].contains(event.position)) {
                m_instance->setParameter(AestraLFO::kTarget,
                                         AestraLFO::normFromTarget(static_cast<AestraLFO::Target>(i)));
                setDirty();
                return true;
            }
        }
        for (size_t i = 0; i < m_waveRects.size(); ++i) {
            if (m_waveRects[i].contains(event.position)) {
                m_instance->setParameter(AestraLFO::kWave, AestraLFO::normFromWave(static_cast<AestraLFO::Wave>(i)));
                setDirty();
                return true;
            }
        }
    }

    // Knob vertical drag
    if (m_draggingParam >= 0) {
        if (event.released) {
            m_draggingParam = -1;
            return true;
        }
        if (event.button == NUIMouseButton::None) {
            const float delta = (m_dragStartY - my) / kDragRangePx;
            m_instance->setParameter(static_cast<uint32_t>(m_draggingParam),
                                     std::clamp(m_dragStartValue + delta, 0.0f, 1.0f));
            setDirty();
            return true;
        }
    }
    if (event.pressed && event.button == NUIMouseButton::Left) {
        const bool sync = m_instance->getParameter(AestraLFO::kSyncMode) > 0.5f;
        const uint32_t rateParam = sync ? AestraLFO::kNoteDivision : AestraLFO::kRateHz;
        const struct {
            NUIRect rect;
            uint32_t param;
        } knobs[] = {
            {m_rateRect, rateParam},
            {m_depthRect, AestraLFO::kDepth},
            {m_phaseRect, AestraLFO::kPhase},
            {m_smoothRect, AestraLFO::kSmooth},
        };
        for (const auto& k : knobs) {
            if (k.rect.contains(event.position)) {
                m_draggingParam = static_cast<int>(k.param);
                m_dragStartY = my;
                m_dragStartValue = m_instance->getParameter(k.param);
                return true;
            }
        }
    }

    return consumeInsideBounds(event);
}

void AestraLFOEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
