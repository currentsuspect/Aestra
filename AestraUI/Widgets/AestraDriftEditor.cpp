// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDriftEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDrift.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kWheelSweep = kPi * 1.5f;
constexpr float kWheelStart = -kWheelSweep * 0.5f; // -135°
constexpr float kWheelEnd = kWheelSweep * 0.5f;    // +135°
constexpr int kNumTicks = 25; // -12 to +12 inclusive

NUIColor accent() { return NUIColor(0.55f, 0.40f, 0.92f, 1.0f); }
NUIColor panelSurface() { return NUIColor(0.027f, 0.027f, 0.027f, 0.96f); }
NUIColor insetSurface() { return NUIColor(0.038f, 0.038f, 0.038f, 0.96f); }

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle,
             float thickness, NUIColor color) {
    if (endAngle < startAngle) std::swap(startAngle, endAngle);
    if (endAngle - startAngle <= 0.001f) return;
    std::array<NUIPoint, 49> pts{};
    const float div = static_cast<float>(pts.size() - 1);
    for (size_t i = 0; i < pts.size(); ++i) {
        const float t = static_cast<float>(i) / div;
        const float a = startAngle + (endAngle - startAngle) * t;
        pts[i] = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
    }
    renderer.drawPolyline(pts.data(), static_cast<int>(pts.size()), thickness, color);
}
}

AestraDriftEditor::AestraDriftEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDriftEditor");
    setPanelTitle("Aestra Drift");
    setBadgeText("Pitch Shifter");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraDriftEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraDriftEditor::layoutControls() {
    const auto b = getBounds();

    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;
    const float availableH = std::max(0.0f, b.height - AestraPanelWindow::TITLE_BAR_H);
    m_wheelRadius = std::min(b.width - 180.0f, availableH - 84.0f) * 0.38f;
    m_wheelRadius = std::clamp(m_wheelRadius, 52.0f, 94.0f);
    m_wheelRect = NUIRect(b.x + b.width * 0.5f - m_wheelRadius,
                          contentTop + 28.0f,
                          m_wheelRadius * 2.0f,
                          m_wheelRadius * 2.0f);

    // Bypass lives inside the plugin surface, not tight against the title bar.
    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 26.0f;
    constexpr float kBypassRightPad = 44.0f;
    m_bypassRect = NUIRect(b.right() - kBypassRightPad - kBypassW,
                           contentTop + 16.0f,
                           kBypassW, kBypassH);

    const float sliderY = std::min(m_wheelRect.bottom() + 28.0f, b.bottom() - 44.0f);
    m_mixRect = NUIRect(b.x + 58.0f, sliderY, b.width - 116.0f, 34.0f);
}

void AestraDriftEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance) return;

    const float cx = contentRect.x + contentRect.width * 0.5f;
    const float wheelCenterY = m_wheelRect.center().y;

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f,
                           contentRect.width - 24.0f, contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.055f));

    drawPitchWheel(renderer, cx, wheelCenterY);
    drawMixSlider(renderer);
    drawBypassPill(renderer);
}

void AestraDriftEditor::drawPitchWheel(NUIRenderer& renderer, float cx, float cy) {
    const float val = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraDrift::kPitch) : 0.5f;
    const float semitones = -12.0f + val * 24.0f;
    const float angle = kWheelStart + (semitones + 12.0f) / 24.0f * kWheelSweep;
    const float r = m_wheelRadius;

    renderer.fillCircle({cx, cy}, r + 15.0f, NUIColor(0.011f, 0.011f, 0.011f, 0.44f));
    renderer.fillCircle({cx, cy}, r + 9.0f, insetSurface());
    renderer.strokeCircle({cx, cy}, r + 9.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.060f));

    drawArc(renderer, {cx, cy}, r - 2.0f, kWheelStart, kWheelEnd, 4.0f, NUIColor(0.199f, 0.199f, 0.199f, 1.0f));
    drawArc(renderer, {cx, cy}, r - 2.0f, kWheelStart, angle, 4.0f, accent().withAlpha(0.92f));

    // Tick marks
    for (int i = 0; i < kNumTicks; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
        const float a = kWheelStart + t * kWheelSweep;
        const bool major = (i == 0 || i == 6 || i == 12 || i == 18 || i == 24);
        const float tickLen = major ? 9.0f : 5.0f;
        const float inner = r - 11.0f;
        const float outer = inner - tickLen;
        const bool centered = std::abs(semitones) < 0.5f && i == 12;
        const NUIColor col = centered ? accent().withAlpha(0.92f) : NUIColor(0.43f, 0.41f, 0.56f, 0.70f);
        renderer.drawLine(
            {cx + std::cos(a) * inner, cy + std::sin(a) * inner},
            {cx + std::cos(a) * outer, cy + std::sin(a) * outer},
            major ? 2.0f : 1.0f, col);
    }

    // Tick labels sit inside the wheel so they do not crowd the panel frame.
    const float labelR = r - 4.0f;
    const char* labels[] = {"-12", "-6", "0", "+6", "+12"};
    const int labelIndices[] = {0, 6, 12, 18, 24};
    for (int li = 0; li < 5; ++li) {
        const float t = static_cast<float>(labelIndices[li]) / static_cast<float>(kNumTicks - 1);
        const float a = kWheelStart + t * kWheelSweep;
        const float lx = cx + std::cos(a) * labelR;
        const float ly = cy + std::sin(a) * labelR;
        renderer.drawTextCentered(labels[li], NUIRect(lx - 14.0f, ly - 7.0f, 28.0f, 14.0f), 8.5f,
                                  NUIColor(0.54f, 0.52f, 0.70f, 0.88f));
    }

    // Needle indicator — Aestra standard style (thin line + small dot)
    const float needleLen = r - 20.0f;
    const NUIPoint needleTip(cx + std::cos(angle) * needleLen, cy + std::sin(angle) * needleLen);
    renderer.drawLine({cx, cy}, needleTip, 2.0f, accent().withAlpha(0.85f));
    renderer.fillCircle(needleTip, 3.5f, accent());

    // Center well — Aestra deep recessed knob body
    const float wellR = r * 0.30f;
    renderer.fillCircle({cx, cy}, wellR + 5.0f, accent().withAlpha(0.08f));
    renderer.fillCircle({cx, cy}, wellR, NUIColor(0.045f, 0.045f, 0.045f, 0.96f));
    renderer.strokeCircle({cx, cy}, wellR, 1.2f, accent().withAlpha(0.36f));

    // Value text in center well
    const std::string valStr = pitchValueString();
    renderer.drawTextCentered(valStr, NUIRect(cx - wellR, cy - 11.0f, wellR * 2.0f, 22.0f), 18.0f,
                              accent().withAlpha(0.96f));
}

void AestraDriftEditor::drawMixSlider(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraDrift::kMix) : 1.0f;
    const NUIRect track(m_mixRect.x + 38.0f, m_mixRect.y + 12.0f, m_mixRect.width - 78.0f, 8.0f);

    renderer.fillRoundedRect(m_mixRect, 10.0f, insetSurface());
    renderer.strokeRoundedRect(m_mixRect, 10.0f, 1.0f, accent().withAlpha(m_draggingMix ? 0.62f : 0.34f));
    renderer.drawText("Mix", {m_mixRect.x + 14.0f, m_mixRect.y + 11.0f}, 10.5f, theme.getColor("textPrimary").withAlpha(0.95f));
    renderer.fillRoundedRect(track, 4.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 4.0f, accent().withAlpha(0.92f));
    const NUIPoint thumb{track.x + track.width * mix, track.center().y};
    renderer.fillCircle(thumb, 10.0f, accent().withAlpha(0.18f));
    renderer.fillCircle(thumb, 7.0f, theme.getColor("textPrimary"));
    const std::string pctStr = std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%";
    const float pctW = renderer.measureText(pctStr, 10.0f).width;
    renderer.drawText(pctStr, {m_mixRect.right() - 14.0f - pctW, m_mixRect.y + 10.0f}, 10.0f, accent().withAlpha(0.96f));
}

void AestraDriftEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraDrift::kBypass) > 0.5f;
    constexpr float kRadius = 7.0f;
    constexpr float kFont = 10.0f;
    if (bypassed) {
        renderer.fillRoundedRect(m_bypassRect, kRadius, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(m_bypassHovered ? 0.94f : 0.78f));
        renderer.strokeRoundedRect(m_bypassRect, kRadius, 1.0f, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(0.50f));
        renderer.drawTextCentered("BYPASSED", m_bypassRect, kFont, theme.getColor("textPrimary"));
    } else {
        renderer.fillRoundedRect(m_bypassRect, kRadius, theme.getColor("success").withAlpha(m_bypassHovered ? 0.30f : 0.18f));
        renderer.strokeRoundedRect(m_bypassRect, kRadius, 1.0f, theme.getColor("success").withAlpha(0.40f));
        renderer.drawTextCentered("ACTIVE", m_bypassRect, kFont, theme.getColor("success"));
    }
}

bool AestraDriftEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;

    const float mx = event.position.x;
    const float my = event.position.y;

    // Bypass pill
    if (m_bypassRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(Aestra::Audio::Plugins::AestraDrift::kBypass);
        m_instance->setParameter(Aestra::Audio::Plugins::AestraDrift::kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }

    // Mix slider drag
    const NUIRect mixTrack(m_mixRect.x + 38.0f, m_mixRect.y + 6.0f, m_mixRect.width - 78.0f, m_mixRect.height - 12.0f);
    if (m_draggingMix) {
        if (event.released) { m_draggingMix = false; return true; }
        if (event.button == NUIMouseButton::None) {
            const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
            m_instance->setParameter(Aestra::Audio::Plugins::AestraDrift::kMix, t);
            setDirty();
            return true;
        }
    }
    if (event.pressed && event.button == NUIMouseButton::Left && mixTrack.contains(event.position)) {
        m_draggingMix = true;
        const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
        m_instance->setParameter(Aestra::Audio::Plugins::AestraDrift::kMix, t);
        setDirty();
        return true;
    }

    // Bypass hover
    m_bypassHovered = m_bypassRect.contains(event.position);

    // Pitch wheel drag
    if (m_wheelRect.contains(event.position)) {
        const float cx = m_wheelRect.center().x;
        const float cy = m_wheelRect.center().y;
        const float dist = std::sqrt((mx - cx) * (mx - cx) + (my - cy) * (my - cy));

        if (event.pressed && event.button == NUIMouseButton::Left && dist < m_wheelRadius + 10.0f) {
            m_draggingPitch = true;
            return true;
        }
        if (m_draggingPitch) {
            if (event.released) { m_draggingPitch = false; return true; }
            if (event.button == NUIMouseButton::None) {
                const float angle = angleFromPosition({cx, cy}, event.position);
                const float st = semitonesFromAngle(angle);
                const float norm = (st + 12.0f) / 24.0f;
                m_instance->setParameter(Aestra::Audio::Plugins::AestraDrift::kPitch, std::clamp(norm, 0.0f, 1.0f));
                setDirty();
                return true;
            }
        }
    } else if (m_draggingPitch && event.released) {
        m_draggingPitch = false;
        return true;
    }

    return false;
}

void AestraDriftEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

std::string AestraDriftEditor::pitchValueString() const {
    if (!m_instance) return "0";
    const float v = m_instance->getParameter(Aestra::Audio::Plugins::AestraDrift::kPitch);
    const int st = static_cast<int>(std::round(-12.0f + v * 24.0f));
    if (st > 0) return "+" + std::to_string(st);
    if (st == 0) return "0";
    return std::to_string(st);
}

float AestraDriftEditor::semitonesFromAngle(float angle) const {
    float t = (angle - kWheelStart) / kWheelSweep;
    t = std::clamp(t, 0.0f, 1.0f);
    return -12.0f + t * 24.0f;
}

float AestraDriftEditor::angleFromPosition(NUIPoint center, NUIPoint pos) const {
    return std::atan2(pos.y - center.y, pos.x - center.x);
}

} // namespace AestraUI
