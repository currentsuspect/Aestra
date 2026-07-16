// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraFilterEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraFilter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraFilter;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kKnobSweep = kPi * 1.5f;
constexpr float kKnobStart = kPi * 0.75f; // pointing down-left, sweeping clockwise
constexpr float kDragRangePx = 160.0f;    // full param range per drag distance

NUIColor accent() {
    return NUIColor(0.28f, 0.72f, 0.62f, 1.0f);
} // filter teal
NUIColor panelSurface() {
    return NUIColor(0.018f, 0.021f, 0.022f, 0.98f);
}
NUIColor insetSurface() {
    return NUIColor(0.028f, 0.034f, 0.035f, 0.98f);
}
NUIColor gridColor() {
    return NUIColor(0.45f, 0.58f, 0.56f, 0.12f);
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

AestraFilterEditor::AestraFilterEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraFilterEditor");
    setPanelTitle("Aestra Filter");
    setBadgeText("Filter");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraFilterEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraFilterEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    if (!isVisible())
        return;

    // The response display follows the audio-thread envelope meter. Refresh at
    // a bounded UI rate so motion is readable without forcing full-frame redraws.
    m_visualRefreshTimer += deltaTime;
    if (m_visualRefreshTimer >= (1.0 / 30.0)) {
        m_visualRefreshTimer = 0.0;
        setDirty(true);
    }
}

void AestraFilterEditor::layoutControls() {
    const auto b = getBounds();
    const float contentTop = b.y + AestraPanelWindow::TITLE_BAR_H;

    constexpr float kSegW = 82.0f;
    constexpr float kSegH = 28.0f;
    for (size_t i = 0; i < m_typeRects.size(); ++i) {
        m_typeRects[i] =
            NUIRect(b.x + 28.0f + static_cast<float>(i) * (kSegW + 6.0f), contentTop + 20.0f, kSegW, kSegH);
    }

    constexpr float kBypassW = 96.0f;
    constexpr float kBypassH = 28.0f;
    m_bypassRect = NUIRect(b.right() - 28.0f - kBypassW, contentTop + 20.0f, kBypassW, kBypassH);

    const float upperTop = contentTop + 72.0f;
    m_responseRect = NUIRect(b.x + 28.0f, upperTop, 410.0f, 192.0f);
    m_cutoffRect = NUIRect(b.x + 470.0f, upperTop + 5.0f, 132.0f, 132.0f);
    m_resoRect = NUIRect(b.x + 618.0f, upperTop + 8.0f, 58.0f, 58.0f);
    m_driveRect = NUIRect(b.x + 618.0f, upperTop + 99.0f, 58.0f, 58.0f);

    const float lowerTop = contentTop + 278.0f;
    m_envRect = NUIRect(b.x + 44.0f, lowerTop + 8.0f, 62.0f, 62.0f);
    m_attackRect = NUIRect(b.x + 145.0f, lowerTop + 8.0f, 62.0f, 62.0f);
    m_releaseRect = NUIRect(b.x + 246.0f, lowerTop + 8.0f, 62.0f, 62.0f);
    m_mixRect = NUIRect(b.x + 370.0f, lowerTop + 22.0f, b.right() - (b.x + 370.0f) - 28.0f, 38.0f);
}

void AestraFilterEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    if (!m_instance)
        return;

    const NUIRect workArea{contentRect.x + 12.0f, contentRect.y + 10.0f, contentRect.width - 24.0f,
                           contentRect.height - 18.0f};
    renderer.fillRoundedRect(workArea, 14.0f, panelSurface());
    renderer.strokeRoundedRect(workArea, 14.0f, 1.0f, accent().withAlpha(0.16f));

    drawTypeSelector(renderer);
    drawBypassPill(renderer);
    drawResponseDisplay(renderer);
    drawKnob(renderer, m_cutoffRect, AestraFilter::kCutoff, "Cutoff", true, false);
    drawKnob(renderer, m_resoRect, AestraFilter::kReso, "Resonance", false, false);
    drawKnob(renderer, m_driveRect, AestraFilter::kDrive, "Drive", false, false);
    drawSectionLabel(renderer, "ENVELOPE MOTION", NUIRect(m_envRect.x, m_envRect.y - 22.0f, 266.0f, 14.0f));
    drawKnob(renderer, m_envRect, AestraFilter::kEnvAmount, "Amount", false, true);
    drawKnob(renderer, m_attackRect, AestraFilter::kEnvAttack, "Attack", false, false);
    drawKnob(renderer, m_releaseRect, AestraFilter::kEnvRelease, "Release", false, false);
    drawSectionLabel(renderer, "BLEND", NUIRect(m_mixRect.x, m_mixRect.y - 22.0f, m_mixRect.width, 14.0f));
    drawMixSlider(renderer);
}

void AestraFilterEditor::drawSectionLabel(NUIRenderer& renderer, const char* label, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.drawText(label, {bounds.x, bounds.y}, 9.0f, theme.getColor("textSecondary").withAlpha(0.62f));
    const float labelWidth = renderer.measureText(label, 9.0f).width;
    renderer.drawLine({bounds.x + labelWidth + 9.0f, bounds.y + 6.0f}, {bounds.right(), bounds.y + 6.0f}, 1.0f,
                      accent().withAlpha(0.16f));
}

void AestraFilterEditor::drawResponseDisplay(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect(m_responseRect, 12.0f, insetSurface());
    renderer.strokeRoundedRect(m_responseRect, 12.0f, 1.0f, accent().withAlpha(0.22f));

    renderer.drawText("FILTER RESPONSE", {m_responseRect.x + 14.0f, m_responseRect.y + 10.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.68f));

    const auto filter = std::dynamic_pointer_cast<AestraFilter>(m_instance);
    const float envelope = filter ? std::clamp(filter->getEnvelopeLevel(), 0.0f, 1.0f) : 0.0f;
    const float cutoffNorm = m_instance->getParameter(AestraFilter::kCutoff);
    const float baseCutoff = AestraFilter::cutoffHzFromNorm(cutoffNorm);
    const float envAmount = 2.0f * m_instance->getParameter(AestraFilter::kEnvAmount) - 1.0f;
    const float modCutoff =
        std::clamp(baseCutoff * std::exp2(envAmount * AestraFilter::kEnvModOctaves * envelope), 20.0f, 20000.0f);
    const float q = AestraFilter::qFromNorm(m_instance->getParameter(AestraFilter::kReso));
    const auto type = AestraFilter::typeFromNorm(m_instance->getParameter(AestraFilter::kType));

    const int envPct = static_cast<int>(std::round(envelope * 100.0f));
    const std::string envLabel = "ENV " + std::to_string(envPct) + "%";
    const float envLabelW = renderer.measureText(envLabel, 9.0f).width;
    renderer.drawText(envLabel, {m_responseRect.right() - envLabelW - 14.0f, m_responseRect.y + 10.0f}, 9.0f,
                      accent().withAlpha(0.82f));

    // Keep a dedicated scale gutter below the plot so frequency labels never
    // sit on the response border or compete with the live envelope meter.
    const NUIRect graph{m_responseRect.x + 14.0f, m_responseRect.y + 31.0f, m_responseRect.width - 28.0f,
                        m_responseRect.height - 65.0f};
    renderer.fillRoundedRect(graph, 7.0f, NUIColor(0.006f, 0.010f, 0.011f, 0.98f));

    auto xForHz = [&graph](float hz) {
        const float norm = std::log10(std::clamp(hz, 20.0f, 20000.0f) / 20.0f) / 3.0f;
        return graph.x + norm * graph.width;
    };
    auto yForDb = [&graph](float db) {
        constexpr float kTopDb = 18.0f;
        constexpr float kBottomDb = -36.0f;
        return graph.y + (kTopDb - std::clamp(db, kBottomDb, kTopDb)) / (kTopDb - kBottomDb) * graph.height;
    };

    for (float db : {-24.0f, -12.0f, 0.0f, 12.0f}) {
        const float y = yForDb(db);
        renderer.drawLine({graph.x, y}, {graph.right(), y}, db == 0.0f ? 1.2f : 1.0f,
                          gridColor().withAlpha(db == 0.0f ? 0.22f : 0.12f));
    }

    struct FrequencyGuide {
        float hz;
        const char* label;
    };
    static constexpr FrequencyGuide kGuides[] = {
        {20.0f, "20"}, {100.0f, "100"}, {1000.0f, "1k"}, {10000.0f, "10k"}, {20000.0f, "20k"},
    };
    for (const auto& guide : kGuides) {
        const float x = xForHz(guide.hz);
        renderer.drawLine({x, graph.y}, {x, graph.bottom()}, 1.0f, gridColor());
        const float labelW = renderer.measureText(guide.label, 8.0f).width;
        renderer.drawText(guide.label,
                          {std::clamp(x - labelW * 0.5f, graph.x, graph.right() - labelW), graph.bottom() + 5.0f},
                          8.0f, theme.getColor("textSecondary").withAlpha(0.58f));
    }

    auto drawResponse = [&](float cutoff, NUIColor color, float thickness) {
        std::array<NUIPoint, 129> points{};
        for (size_t i = 0; i < points.size(); ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(points.size() - 1);
            const float freq = 20.0f * std::pow(1000.0f, t);
            const float ratio = freq / std::max(20.0f, cutoff);
            const float ratio2 = ratio * ratio;
            const float damping = ratio / q;
            const float denominator = std::sqrt((1.0f - ratio2) * (1.0f - ratio2) + damping * damping);
            float magnitude = 1.0f / std::max(1.0e-5f, denominator);
            if (type == AestraFilter::kTypeBandPass)
                magnitude *= ratio;
            else if (type == AestraFilter::kTypeHighPass)
                magnitude *= ratio2;
            const float db = 20.0f * std::log10(std::max(1.0e-5f, magnitude));
            points[i] = {graph.x + t * graph.width, yForDb(db)};
        }
        renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
    };

    renderer.setClipRect(graph);
    const bool modulationActive = std::abs(envAmount) > 0.005f;
    if (modulationActive) {
        drawResponse(baseCutoff, theme.getColor("textSecondary").withAlpha(0.28f), 1.5f);
        renderer.drawLine({xForHz(baseCutoff), graph.y}, {xForHz(baseCutoff), graph.bottom()}, 1.0f,
                          theme.getColor("textSecondary").withAlpha(0.20f));
    }
    drawResponse(modCutoff, accent().withAlpha(0.96f), 2.4f);
    renderer.drawLine({xForHz(modCutoff), graph.y}, {xForHz(modCutoff), graph.bottom()}, 1.2f,
                      accent().withAlpha(0.34f));
    renderer.clearClipRect();

    const NUIRect envTrack{graph.x, m_responseRect.bottom() - 8.0f, graph.width, 3.0f};
    renderer.fillRoundedRect(envTrack, 1.5f, NUIColor(1.0f, 1.0f, 1.0f, 0.06f));
    renderer.fillRoundedRect({envTrack.x, envTrack.y, envTrack.width * envelope, envTrack.height}, 1.5f,
                             accent().withAlpha(0.72f));
}

void AestraFilterEditor::drawKnob(NUIRenderer& renderer, const NUIRect& rect, uint32_t paramId, const char* label,
                                  bool large, bool bipolar) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = m_instance ? m_instance->getParameter(paramId) : 0.0f;
    const NUIPoint c = rect.center();
    const float r = rect.width * 0.5f - 4.0f;
    const float angle = kKnobStart + value * kKnobSweep;

    renderer.fillCircle(c, r + 4.0f, insetSurface());
    renderer.strokeCircle(c, r + 4.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.060f));

    drawArc(renderer, c, r - 3.0f, kKnobStart, kKnobStart + kKnobSweep, large ? 4.0f : 3.0f,
            NUIColor(0.199f, 0.199f, 0.199f, 1.0f));
    if (bipolar) {
        // Bipolar knobs fill from the top-center detent toward the value.
        const float centerAngle = kKnobStart + 0.5f * kKnobSweep;
        drawArc(renderer, c, r - 3.0f, std::min(centerAngle, angle), std::max(centerAngle, angle), 3.0f,
                accent().withAlpha(0.92f));
    } else {
        drawArc(renderer, c, r - 3.0f, kKnobStart, angle, large ? 4.0f : 3.0f, accent().withAlpha(0.92f));
    }

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

void AestraFilterEditor::drawTypeSelector(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const auto type = m_instance ? AestraFilter::typeFromNorm(m_instance->getParameter(AestraFilter::kType))
                                 : AestraFilter::kTypeLowPass;
    static constexpr const char* kLabels[] = {"LOW PASS", "BAND PASS", "HIGH PASS"};
    for (size_t i = 0; i < m_typeRects.size(); ++i) {
        const bool selected = static_cast<uint32_t>(type) == i;
        if (selected) {
            renderer.fillRoundedRect(m_typeRects[i], 7.0f, accent().withAlpha(0.26f));
            renderer.strokeRoundedRect(m_typeRects[i], 7.0f, 1.0f, accent().withAlpha(0.62f));
            renderer.drawTextCentered(kLabels[i], m_typeRects[i], 9.0f, accent().withAlpha(0.98f));
        } else {
            renderer.fillRoundedRect(m_typeRects[i], 7.0f, insetSurface());
            renderer.strokeRoundedRect(m_typeRects[i], 7.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
            renderer.drawTextCentered(kLabels[i], m_typeRects[i], 9.0f,
                                      theme.getColor("textPrimary").withAlpha(0.62f));
        }
    }
}

void AestraFilterEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(AestraFilter::kBypass) > 0.5f;
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

NUIRect AestraFilterEditor::mixTrackRect() const {
    return NUIRect(m_mixRect.x + 38.0f, m_mixRect.y + 6.0f, m_mixRect.width - 78.0f, m_mixRect.height - 12.0f);
}

void AestraFilterEditor::drawMixSlider(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = m_instance ? m_instance->getParameter(AestraFilter::kMix) : 1.0f;
    const NUIRect hit = mixTrackRect();
    const NUIRect track(hit.x, m_mixRect.y + 12.0f, hit.width, 8.0f);

    renderer.fillRoundedRect(m_mixRect, 10.0f, insetSurface());
    renderer.strokeRoundedRect(m_mixRect, 10.0f, 1.0f, accent().withAlpha(m_draggingMix ? 0.62f : 0.34f));
    renderer.drawText("Mix", {m_mixRect.x + 14.0f, m_mixRect.y + 11.0f}, 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.95f));
    renderer.fillRoundedRect(track, 4.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 4.0f, accent().withAlpha(0.92f));
    const NUIPoint thumb{track.x + track.width * mix, track.center().y};
    renderer.fillCircle(thumb, 10.0f, accent().withAlpha(0.18f));
    renderer.fillCircle(thumb, 7.0f, theme.getColor("textPrimary"));
    const std::string pctStr = std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%";
    const float pctW = renderer.measureText(pctStr, 10.0f).width;
    renderer.drawText(pctStr, {m_mixRect.right() - 14.0f - pctW, m_mixRect.y + 10.0f}, 10.0f,
                      accent().withAlpha(0.96f));
}

bool AestraFilterEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;

    const float mx = event.position.x;
    const float my = event.position.y;

    // Bypass pill
    if (m_bypassRect.contains(event.position) && event.pressed && event.button == NUIMouseButton::Left) {
        const float cur = m_instance->getParameter(AestraFilter::kBypass);
        m_instance->setParameter(AestraFilter::kBypass, cur > 0.5f ? 0.0f : 1.0f);
        setDirty();
        return true;
    }
    m_bypassHovered = m_bypassRect.contains(event.position);

    // Type segments
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < m_typeRects.size(); ++i) {
            if (m_typeRects[i].contains(event.position)) {
                m_instance->setParameter(AestraFilter::kType,
                                         AestraFilter::normFromType(static_cast<AestraFilter::Type>(i)));
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
        const struct {
            NUIRect rect;
            uint32_t param;
        } knobs[] = {
            {m_cutoffRect, AestraFilter::kCutoff},    {m_resoRect, AestraFilter::kReso},
            {m_driveRect, AestraFilter::kDrive},      {m_envRect, AestraFilter::kEnvAmount},
            {m_attackRect, AestraFilter::kEnvAttack}, {m_releaseRect, AestraFilter::kEnvRelease},
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

    // Mix slider drag
    const NUIRect mixTrack = mixTrackRect();
    if (m_draggingMix) {
        if (event.released) {
            m_draggingMix = false;
            return true;
        }
        if (event.button == NUIMouseButton::None) {
            const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
            m_instance->setParameter(AestraFilter::kMix, t);
            setDirty();
            return true;
        }
    }
    if (event.pressed && event.button == NUIMouseButton::Left && mixTrack.contains(event.position)) {
        m_draggingMix = true;
        const float t = std::clamp((mx - mixTrack.x) / mixTrack.width, 0.0f, 1.0f);
        m_instance->setParameter(AestraFilter::kMix, t);
        setDirty();
        return true;
    }

    return consumeInsideBounds(event);
}

void AestraFilterEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
