// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDriftEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDrift.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace AestraUI {

namespace {
using Drift = Aestra::Audio::Plugins::AestraDrift;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kWheelStart = kPi * 0.75f;
constexpr float kWheelSweep = kPi * 1.5f;
constexpr std::array<int, 7> kIntervals{-12, -7, -5, 0, 5, 7, 12};

NUIColor pitchAccent() {
    return NUIColor(0.67f, 0.35f, 1.0f, 1.0f);
}
NUIColor grainAccent() {
    return NUIColor(0.20f, 0.82f, 0.91f, 1.0f);
}
NUIColor motionAccent() {
    return NUIColor(1.0f, 0.45f, 0.64f, 1.0f);
}
NUIColor shellSurface() {
    return editorNeutral(NUIColor(0.024f, 0.023f, 0.031f, 0.99f));
}
NUIColor panelSurface() {
    return editorNeutral(NUIColor(0.040f, 0.038f, 0.050f, 0.99f));
}
NUIColor insetSurface() {
    return editorNeutral(NUIColor(0.018f, 0.017f, 0.024f, 0.99f));
}

NUIColor parameterAccent(uint32_t parameterId) {
    switch (parameterId) {
    case Drift::kGrain:
    case Drift::kTexture:
    case Drift::kSpread:
        return grainAccent();
    case Drift::kMotion:
    case Drift::kMotionRate:
        return motionAccent();
    default:
        return pitchAccent();
    }
}

void drawArc(NUIRenderer& renderer, NUIPoint center, float radius, float startAngle, float endAngle, float thickness,
             NUIColor color) {
    if (endAngle <= startAngle + 0.001f)
        return;
    std::array<NUIPoint, 49> points{};
    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(points.size() - 1);
        const float angle = startAngle + (endAngle - startAngle) * t;
        points[i] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
}
} // namespace

AestraDriftEditor::AestraDriftEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDriftEditor");
    setPanelTitle("Aestra Drift");
    setBadgeText("Pitch Shifter");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildControls();
}

void AestraDriftEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraDriftEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance)
        return;
    struct Meta {
        uint32_t id;
        const char* label;
        const char* subtitle;
    };
    constexpr Meta meta[] = {
        {Drift::kGrain, "GRAIN", "Window length"},
        {Drift::kTexture, "TEXTURE", "Overlap color"},
        {Drift::kFine, "FINE", "Cents"},
        {Drift::kSpread, "SPREAD", "Stereo phase"},
        {Drift::kMotion, "MOTION", "Optional drift"},
        {Drift::kMotionRate, "RATE", "Motion speed"},
        {Drift::kOutput, "OUTPUT", "Final level"},
    };
    const auto parameters = m_instance->getParameters();
    for (const auto& item : meta) {
        if (item.id >= parameters.size())
            continue;
        m_knobs.push_back({item.id, item.label, item.subtitle, parameters[item.id].defaultValue, {}, {}});
    }
    layoutControls();
}

void AestraDriftEditor::layoutControls() {
    const auto bounds = getBounds();
    const float top = bounds.y + AestraPanelWindow::TITLE_BAR_H + kPad;
    const float bottomBarHeight = 52.0f;
    const float mainHeight = std::max(0.0f, bounds.bottom() - kPad - bottomBarHeight - top);
    const float contentWidth = std::max(0.0f, bounds.width - kPad * 2.0f);
    const float gap = 12.0f;
    const float pitchWidth = std::clamp(contentWidth * 0.43f, 286.0f, 310.0f);
    m_pitchPanelBounds = {bounds.x + kPad, top, pitchWidth, mainHeight};
    m_texturePanelBounds = {m_pitchPanelBounds.right() + gap, top, contentWidth - pitchWidth - gap, mainHeight};

    const float wheelSize = std::min(188.0f, m_pitchPanelBounds.width - 56.0f);
    m_pitchWheelBounds = {m_pitchPanelBounds.center().x - wheelSize * 0.5f, m_pitchPanelBounds.y + 48.0f, wheelSize,
                          wheelSize};

    const float intervalGap = 5.0f;
    const float intervalPad = 12.0f;
    const float intervalWidth =
        (m_pitchPanelBounds.width - intervalPad * 2.0f - intervalGap * 6.0f) / static_cast<float>(kIntervals.size());
    for (size_t i = 0; i < m_intervalBounds.size(); ++i) {
        m_intervalBounds[i] = {m_pitchPanelBounds.x + intervalPad +
                                   static_cast<float>(i) * (intervalWidth + intervalGap),
                               m_pitchPanelBounds.bottom() - 42.0f, intervalWidth, 28.0f};
    }

    const float controlPad = 10.0f;
    const float controlGap = 8.0f;
    const float controlTop = m_texturePanelBounds.y + 40.0f;
    const float cellWidth = (m_texturePanelBounds.width - controlPad * 2.0f - controlGap * 3.0f) / 4.0f;
    const float cellHeight = (m_texturePanelBounds.bottom() - controlTop - controlPad - controlGap) / 2.0f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        const size_t row = i < 4 ? 0 : 1;
        const size_t column = i < 4 ? i : i - 4;
        const float rowOffset = row == 1 ? (cellWidth + controlGap) * 0.5f : 0.0f;
        auto& control = m_knobs[i];
        control.bounds = {m_texturePanelBounds.x + controlPad + rowOffset +
                              static_cast<float>(column) * (cellWidth + controlGap),
                          controlTop + static_cast<float>(row) * (cellHeight + controlGap), cellWidth, cellHeight};
        const float knobSize = std::min(68.0f, cellWidth - 22.0f);
        control.knobBounds = {control.bounds.center().x - knobSize * 0.5f, control.bounds.y + 12.0f, knobSize,
                              knobSize};
    }

    const float bottomY = m_pitchPanelBounds.bottom() + 10.0f;
    m_mixBounds = {bounds.x + kPad, bottomY, contentWidth - 112.0f, 42.0f};
    m_bypassBounds = {m_mixBounds.right() + 10.0f, bottomY, 102.0f, 42.0f};
}

void AestraDriftEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance)
        return;
    const auto bounds = getBounds();
    renderer.fillRect({bounds.x, bounds.y + AestraPanelWindow::TITLE_BAR_H, bounds.width,
                       bounds.height - AestraPanelWindow::TITLE_BAR_H},
                      shellSurface());
    drawPitchPanel(renderer);
    drawTexturePanel(renderer);
    drawMixBar(renderer);
    drawBypassPill(renderer);
}

void AestraDriftEditor::drawPitchPanel(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_pitchPanelBounds, 16.0f, panelSurface());
    renderer.strokeRoundedRect(m_pitchPanelBounds, 16.0f, 1.0f, pitchAccent().withAlpha(0.34f));
    renderer.fillRoundedRect({m_pitchPanelBounds.x + 12.0f, m_pitchPanelBounds.y + 13.0f, 4.0f, 16.0f}, 2.0f,
                             pitchAccent());
    renderer.drawText("PITCH", {m_pitchPanelBounds.x + 24.0f, m_pitchPanelBounds.y + 14.0f}, 10.0f,
                      theme.getColor("textPrimary"));
    renderer.drawText("COARSE SHIFT", {m_pitchPanelBounds.right() - 92.0f, m_pitchPanelBounds.y + 15.0f}, 8.0f,
                      theme.getColor("textSecondary").withAlpha(0.62f));
    drawPitchWheel(renderer);
    renderer.drawText("INTERVALS", {m_pitchPanelBounds.x + 14.0f, m_pitchPanelBounds.bottom() - 58.0f}, 7.5f,
                      theme.getColor("textSecondary").withAlpha(0.58f));

    const float currentSemitones = -12.0f + m_instance->getParameter(Drift::kPitch) * 24.0f;
    for (size_t i = 0; i < m_intervalBounds.size(); ++i) {
        const bool active = std::fabs(currentSemitones - static_cast<float>(kIntervals[i])) < 0.25f;
        const bool hovered = static_cast<int>(i) == m_hoveredInterval;
        renderer.fillRoundedRect(m_intervalBounds[i], 7.0f,
                                 active    ? pitchAccent().withAlpha(0.28f)
                                 : hovered ? pitchAccent().withAlpha(0.13f)
                                           : insetSurface());
        renderer.strokeRoundedRect(m_intervalBounds[i], 7.0f, 1.0f,
                                   active || hovered ? pitchAccent().withAlpha(0.56f)
                                                     : editorInk(0.05f));
        const std::string label =
            kIntervals[i] > 0 ? "+" + std::to_string(kIntervals[i]) : std::to_string(kIntervals[i]);
        renderer.drawTextCentered(label, m_intervalBounds[i], 9.0f,
                                  active ? pitchAccent() : theme.getColor("textSecondary"));
    }
}

void AestraDriftEditor::drawPitchWheel(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance->getParameter(Drift::kPitch);
    const float angle = kWheelStart + value * kWheelSweep;
    const NUIPoint center = m_pitchWheelBounds.center();
    const float radius = m_pitchWheelBounds.width * 0.5f;

    renderer.fillCircle(center, radius, insetSurface());
    renderer.strokeCircle(center, radius, 1.0f,
                          m_pitchHovered || m_draggingPitch ? pitchAccent().withAlpha(0.54f)
                                                            : editorInk(0.08f));
    drawArc(renderer, center, radius - 11.0f, kWheelStart, kWheelStart + kWheelSweep, 6.0f,
            editorInk(0.09f));
    drawArc(renderer, center, radius - 11.0f, kWheelStart, angle, 6.0f, pitchAccent().withAlpha(0.94f));
    for (int semitones : kIntervals) {
        const float t = (static_cast<float>(semitones) + 12.0f) / 24.0f;
        const float tickAngle = kWheelStart + t * kWheelSweep;
        const NUIPoint outer{center.x + std::cos(tickAngle) * (radius - 20.0f),
                             center.y + std::sin(tickAngle) * (radius - 20.0f)};
        const NUIPoint inner{center.x + std::cos(tickAngle) * (radius - 29.0f),
                             center.y + std::sin(tickAngle) * (radius - 29.0f)};
        renderer.drawLine(inner, outer, semitones == 0 ? 2.0f : 1.0f,
                          semitones == 0 ? pitchAccent().withAlpha(0.70f)
                                         : theme.getColor("textSecondary").withAlpha(0.36f));
    }
    const NUIPoint needle{center.x + std::cos(angle) * (radius - 35.0f), center.y + std::sin(angle) * (radius - 35.0f)};
    renderer.drawLine(center, needle, 2.3f, pitchAccent().withAlpha(0.88f));
    renderer.fillCircle(center, 28.0f, editorNeutral(NUIColor(0.048f, 0.045f, 0.060f, 1.0f)));
    renderer.strokeCircle(center, 28.0f, 1.0f, pitchAccent().withAlpha(0.30f));
    renderer.drawTextCentered(pitchValueString(), {center.x - 34.0f, center.y - 11.0f, 68.0f, 22.0f}, 18.0f,
                              pitchAccent());
    renderer.drawTextCentered("SEMITONES", {center.x - 42.0f, center.y + 17.0f, 84.0f, 11.0f}, 7.0f,
                              theme.getColor("textSecondary").withAlpha(0.54f));
}

void AestraDriftEditor::drawTexturePanel(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_texturePanelBounds, 16.0f, panelSurface());
    renderer.strokeRoundedRect(m_texturePanelBounds, 16.0f, 1.0f, grainAccent().withAlpha(0.28f));
    renderer.fillRoundedRect({m_texturePanelBounds.x + 12.0f, m_texturePanelBounds.y + 13.0f, 4.0f, 16.0f}, 2.0f,
                             grainAccent());
    renderer.drawText("TEXTURE + MOTION", {m_texturePanelBounds.x + 24.0f, m_texturePanelBounds.y + 14.0f}, 10.0f,
                      theme.getColor("textPrimary"));
    const bool clean = m_instance->getParameter(Drift::kTexture) < 0.001f &&
                       m_instance->getParameter(Drift::kMotion) < 0.001f &&
                       m_instance->getParameter(Drift::kSpread) < 0.001f;
    const NUIRect modePill(m_texturePanelBounds.right() - 76.0f, m_texturePanelBounds.y + 10.0f, 62.0f, 22.0f);
    renderer.fillRoundedRect(modePill, 11.0f, clean ? grainAccent().withAlpha(0.13f) : motionAccent().withAlpha(0.13f));
    renderer.strokeRoundedRect(modePill, 11.0f, 1.0f, (clean ? grainAccent() : motionAccent()).withAlpha(0.36f));
    renderer.drawTextCentered(clean ? "CLEAN" : "COLOR", modePill, 7.5f, clean ? grainAccent() : motionAccent());
    for (size_t i = 0; i < m_knobs.size(); ++i)
        drawKnob(renderer, m_knobs[i], static_cast<int>(i));
}

void AestraDriftEditor::drawKnob(NUIRenderer& renderer, const KnobControl& control, int index) {
    auto& theme = NUIThemeManager::getInstance();
    const bool hovered = m_hoveredKnob == index;
    const bool dragging = m_draggingKnob == index;
    const bool rateInactive =
        control.parameterId == Drift::kMotionRate && m_instance->getParameter(Drift::kMotion) < 0.001f;
    const NUIColor color = parameterAccent(control.parameterId);
    renderer.fillRoundedRect(control.bounds, 10.0f,
                             hovered || dragging ? editorNeutral(NUIColor(0.070f, 0.064f, 0.086f, 0.98f)) : insetSurface());
    renderer.strokeRoundedRect(control.bounds, 10.0f, 1.0f,
                               hovered || dragging ? color.withAlpha(0.48f) : editorInk(0.045f));
    const float value = m_instance->getParameter(control.parameterId);
    const NUIPoint center = control.knobBounds.center();
    const float radius = control.knobBounds.width * 0.5f;
    const float angle = kWheelStart + value * kWheelSweep;
    renderer.fillCircle(center, radius, editorNeutral(NUIColor(0.030f, 0.029f, 0.039f, 1.0f)));
    renderer.strokeCircle(center, radius, 1.0f, editorInk(0.07f));
    drawArc(renderer, center, radius - 6.0f, kWheelStart, kWheelStart + kWheelSweep, 3.5f,
            editorInk(0.08f));
    drawArc(renderer, center, radius - 6.0f, kWheelStart, angle, 3.5f, color.withAlpha(rateInactive ? 0.30f : 0.92f));
    const NUIPoint needle{center.x + std::cos(angle) * (radius - 14.0f), center.y + std::sin(angle) * (radius - 14.0f)};
    renderer.drawLine(center, needle, 1.8f, color.withAlpha(rateInactive ? 0.28f : 0.86f));
    renderer.fillCircle(center, 4.5f, editorNeutral(NUIColor(0.070f, 0.066f, 0.086f, 1.0f)));
    renderer.fillCircle(center, 1.8f, color.withAlpha(rateInactive ? 0.30f : 0.90f));

    const float labelY = control.knobBounds.bottom() + 5.0f;
    renderer.drawTextCentered(control.label, {control.bounds.x, labelY, control.bounds.width, 12.0f}, 8.5f,
                              theme.getColor("textPrimary").withAlpha(rateInactive ? 0.42f : 0.90f));
    renderer.drawTextCentered(m_instance->getParameterDisplay(control.parameterId),
                              {control.bounds.x, labelY + 14.0f, control.bounds.width, 11.0f}, 8.0f,
                              color.withAlpha(rateInactive ? 0.32f : 0.92f));
    renderer.drawTextCentered(control.subtitle, {control.bounds.x, labelY + 28.0f, control.bounds.width, 10.0f}, 7.0f,
                              theme.getColor("textSecondary").withAlpha(rateInactive ? 0.28f : 0.50f));
}

void AestraDriftEditor::drawMixBar(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = m_instance->getParameter(Drift::kMix);
    renderer.fillRoundedRect(m_mixBounds, 11.0f, panelSurface());
    renderer.strokeRoundedRect(m_mixBounds, 11.0f, 1.0f, pitchAccent().withAlpha(m_draggingMix ? 0.56f : 0.18f));
    renderer.drawText("BLEND", {m_mixBounds.x + 14.0f, m_mixBounds.y + 15.0f}, 9.0f, theme.getColor("textPrimary"));
    const NUIRect track(m_mixBounds.x + 70.0f, m_mixBounds.y + 17.0f, m_mixBounds.width - 132.0f, 8.0f);
    renderer.fillRoundedRect(track, 4.0f, editorInk(0.09f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 4.0f, pitchAccent().withAlpha(0.90f));
    const NUIPoint thumb{track.x + track.width * mix, track.center().y};
    renderer.fillCircle(thumb, 8.0f, pitchAccent().withAlpha(0.20f));
    renderer.fillCircle(thumb, 5.0f, theme.getColor("textPrimary"));
    renderer.drawText(m_instance->getParameterDisplay(Drift::kMix),
                      {m_mixBounds.right() - 45.0f, m_mixBounds.y + 15.0f}, 9.0f, pitchAccent());
}

void AestraDriftEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance->getParameter(Drift::kBypass) > 0.5f;
    const NUIColor color = bypassed ? NUIColor(0.94f, 0.30f, 0.24f, 1.0f) : theme.getColor("success");
    renderer.fillRoundedRect(m_bypassBounds, 11.0f, color.withAlpha(m_bypassHovered ? 0.24f : 0.13f));
    renderer.strokeRoundedRect(m_bypassBounds, 11.0f, 1.0f, color.withAlpha(m_bypassHovered ? 0.62f : 0.38f));
    renderer.drawTextCentered(bypassed ? "BYPASSED" : "ACTIVE", m_bypassBounds, 9.0f, color);
}

void AestraDriftEditor::setParameter(uint32_t parameterId, float value) {
    if (!m_instance)
        return;
    m_instance->setParameter(parameterId, std::clamp(value, 0.0f, 1.0f));
    setDirty();
}

int AestraDriftEditor::hitTestKnob(NUIPoint position) const {
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].bounds.contains(position))
            return static_cast<int>(i);
    }
    return -1;
}

bool AestraDriftEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;

    const NUIRect mixTrack(m_mixBounds.x + 70.0f, m_mixBounds.y + 7.0f, m_mixBounds.width - 132.0f,
                           m_mixBounds.height - 14.0f);
    if (event.released) {
        const bool wasDragging = m_draggingPitch || m_draggingMix || m_draggingKnob >= 0;
        // Pitch wheel + knobs capture the cursor; the mix track does not.
        if (m_draggingPitch || m_draggingKnob >= 0)
            endKnobCapture();
        m_draggingPitch = false;
        m_draggingMix = false;
        m_draggingKnob = -1;
        if (wasDragging)
            return true;
    }

    if (event.type == NUIMouseEventType::DoubleClick || event.doubleClick) {
        if (m_pitchWheelBounds.contains(event.position)) {
            setParameter(Drift::kPitch, 0.5f);
            return true;
        }
        const int knob = hitTestKnob(event.position);
        if (knob >= 0) {
            setParameter(m_knobs[static_cast<size_t>(knob)].parameterId,
                         m_knobs[static_cast<size_t>(knob)].defaultValue);
            return true;
        }
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_bypassBounds.contains(event.position)) {
            setParameter(Drift::kBypass, m_instance->getParameter(Drift::kBypass) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        for (size_t i = 0; i < m_intervalBounds.size(); ++i) {
            if (m_intervalBounds[i].contains(event.position)) {
                setParameter(Drift::kPitch, (static_cast<float>(kIntervals[i]) + 12.0f) / 24.0f);
                return true;
            }
        }
        if (m_pitchWheelBounds.contains(event.position)) {
            m_draggingPitch = true;
            beginKnobCapture(m_pitchWheelBounds.center(), event.position);
            return true;
        }
        if (mixTrack.contains(event.position)) {
            m_draggingMix = true;
            setParameter(Drift::kMix, (event.position.x - mixTrack.x) / mixTrack.width);
            return true;
        }
        const int knob = hitTestKnob(event.position);
        if (knob >= 0) {
            m_draggingKnob = knob;
            beginKnobCapture(m_knobs[static_cast<size_t>(knob)].bounds.center(), event.position);
            return true;
        }
    }

    if (event.type == NUIMouseEventType::Move || event.button == NUIMouseButton::None) {
        if (m_draggingPitch) {
            // Service-owned frame delta (up = increase), accumulated into value.
            setParameter(Drift::kPitch,
                         m_instance->getParameter(Drift::kPitch) + knobDragStep(event, kDragRangePixels));
            return true;
        }
        if (m_draggingMix) {
            setParameter(Drift::kMix, (event.position.x - mixTrack.x) / mixTrack.width);
            return true;
        }
        if (m_draggingKnob >= 0) {
            const auto& knob = m_knobs[static_cast<size_t>(m_draggingKnob)];
            setParameter(knob.parameterId,
                         m_instance->getParameter(knob.parameterId) + knobDragStep(event, kDragRangePixels));
            return true;
        }
        m_pitchHovered = m_pitchWheelBounds.contains(event.position);
        m_bypassHovered = m_bypassBounds.contains(event.position);
        m_hoveredKnob = hitTestKnob(event.position);
        m_hoveredInterval = -1;
        for (size_t i = 0; i < m_intervalBounds.size(); ++i) {
            if (m_intervalBounds[i].contains(event.position)) {
                m_hoveredInterval = static_cast<int>(i);
                break;
            }
        }
        setDirty();
    }

    if (event.type == NUIMouseEventType::Scroll) {
        if (m_pitchWheelBounds.contains(event.position)) {
            setParameter(Drift::kPitch, m_instance->getParameter(Drift::kPitch) + event.wheelDelta * 0.01f);
            return true;
        }
        if (mixTrack.contains(event.position)) {
            setParameter(Drift::kMix, m_instance->getParameter(Drift::kMix) + event.wheelDelta * 0.01f);
            return true;
        }
        const int knob = hitTestKnob(event.position);
        if (knob >= 0) {
            const uint32_t id = m_knobs[static_cast<size_t>(knob)].parameterId;
            setParameter(id, m_instance->getParameter(id) + event.wheelDelta * 0.01f);
            return true;
        }
    }

    return consumeInsideBounds(event);
}

void AestraDriftEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

std::string AestraDriftEditor::pitchValueString() const {
    if (!m_instance)
        return "0";
    const int semitones = static_cast<int>(std::round(-12.0f + m_instance->getParameter(Drift::kPitch) * 24.0f));
    if (semitones > 0)
        return "+" + std::to_string(semitones);
    return std::to_string(semitones);
}

} // namespace AestraUI
