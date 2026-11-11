// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraOTTEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraOTT.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraOTT;

constexpr float kDragRangePx = 160.0f;    // full param range per drag distance
constexpr float kPlotRangeDb = 24.0f;     // the band display runs +-24 dB
constexpr float kHandleHit = 6.0f;

float lowHz(float norm) { return AestraOTT::xoverLowHzFromNorm(norm); }
float highHz(float norm) { return AestraOTT::xoverHighHzFromNorm(norm); }

} // namespace

AestraOTTEditor::AestraOTTEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraOTTEditor");
    setPanelTitle("OTT");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraOTTEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraOTTEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_well = NUIRect(ox + 12.0f, oy + 12.0f, 616.0f, 160.0f);
    m_plot = NUIRect(m_well.x + 28.0f, m_well.y + 8.0f, m_well.width - 36.0f, m_well.height - 28.0f);

    using K = EditorKit::EmberKnobBoard;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 184.0f}, 616.0f,
                      {K::Group{"Global",
                                {{AestraOTT::kDepth, "Depth"},
                                 {AestraOTT::kTime, "Time", EditorKit::KnobSize::Medium, true},
                                 {AestraOTT::kInGain, "Input", EditorKit::KnobSize::Medium, true},
                                 {AestraOTT::kOutGain, "Output", EditorKit::KnobSize::Medium, true}}},
                       K::Group{"Band trim",
                                {{AestraOTT::kLowGain, "Low", EditorKit::KnobSize::Medium, true},
                                 {AestraOTT::kMidGain, "Mid", EditorKit::KnobSize::Medium, true},
                                 {AestraOTT::kHighGain, "High", EditorKit::KnobSize::Medium, true}}},
                       K::Group{"Crossover",
                                {{AestraOTT::kXoverLow, "Low X"}, {AestraOTT::kXoverHigh, "High X"}}}});
}

float AestraOTTEditor::crossoverX(int which) const {
    const float norm = m_instance ? m_instance->getParameter(which == 0 ? AestraOTT::kXoverLow : AestraOTT::kXoverHigh)
                                  : 0.0f;
    return EditorKit::logFrequencyX(which == 0 ? lowHz(norm) : highHz(norm), m_plot.x, m_plot.width);
}

void AestraOTTEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;
    if (auto ott = std::dynamic_pointer_cast<AestraOTT>(m_instance))
        for (uint32_t i = 0; i < 3; ++i) m_bandGainDb[i] += (ott->getBandGainDb(i) - m_bandGainDb[i]) * 0.4f;
    repaint();
}

void AestraOTTEditor::drawBands(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& p = m_plot;
    EditorKit::drawWell(renderer, m_well);
    EditorKit::drawFrequencyGrid(renderer, NUIRect(p.x, p.y, p.width, p.height + 20.0f));

    auto gy = [&](float db) { return p.y + p.height * 0.5f - db / kPlotRangeDb * (p.height * 0.5f); };
    for (int d = 24; d >= -24; d -= 12) {
        const float y = std::round(gy(static_cast<float>(d)));
        renderer.fillRect(NUIRect(p.x, y, p.width, 1.0f), d == 0 ? c.border : c.gridMinor);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%s%d", d > 0 ? "+" : "", d);
        EditorKit::drawTextRight(renderer, buf, m_well.x + 22.0f, y, 9.0f, c.inkMuted);
    }

    const float xs[4] = {p.x, crossoverX(0), crossoverX(1), p.right()};
    const char* names[3] = {"Low", "Mid", "High"};
    for (int i = 0; i < 3; ++i) {
        const float cx = (xs[i] + xs[i + 1]) * 0.5f;
        const float bw = std::min(28.0f, (xs[i + 1] - xs[i]) * 0.4f);
        const float g = bypassed ? 0.0f : std::clamp(m_bandGainDb[i], -kPlotRangeDb, kPlotRangeDb);
        const float y0 = gy(0.0f), y1 = gy(g);
        renderer.fillRect(NUIRect(cx - bw * 0.5f, std::min(y0, y1), bw, std::abs(y1 - y0)),
                          bypassed ? c.inkOff : g >= 0.0f ? c.meter : c.warn);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s%.1f dB", g >= 0.0f ? "+" : "", static_cast<double>(g));
        const float tw = renderer.measureText(buf, 11.0f).width;
        EditorKit::drawTextLeft(renderer, buf, cx - tw * 0.5f, p.y + 10.0f, 11.0f, c.ink);
        const float nw = renderer.measureText(names[i], 9.0f).width + 8.0f;
        EditorKit::drawCaps(renderer, names[i], cx - nw * 0.5f, p.bottom() - 8.0f, 9.0f, c.inkMuted);
    }

    // Crossover handles: 2 px line and a 10 px head, the same parameters the two Crossover knobs set.
    const NUIPoint* mouse = hoverPoint();
    for (int i = 0; i < 2; ++i) {
        const float xx = std::round(xs[i + 1]);
        const bool hot = m_dragCrossover == i ||
                         (mouse && std::abs(mouse->x - xx) <= kHandleHit && mouse->y >= p.y - 2.0f &&
                          mouse->y <= p.bottom());
        const NUIColor col = hot ? c.primaryHover : bypassed ? c.inkOff : c.primary;
        renderer.fillRect(NUIRect(xx - 1.0f, p.y, 2.0f, p.height), col);
        renderer.fillRect(NUIRect(xx - 5.0f, p.y - 2.0f, 10.0f, 10.0f), col);
    }
}

void AestraOTTEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, AestraOTT::kBypass);
    drawBands(renderer, bypassed);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
}

bool AestraOTTEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;
    layoutControls();

    // Crossover handles
    if (m_dragCrossover >= 0 && !event.pressed) {
        if (event.released) {
            m_dragCrossover = -1;
        } else {
            const float t = std::clamp((event.position.x - m_plot.x) / m_plot.width, 0.0f, 1.0f);
            const float hz = 20.0f * std::pow(1000.0f, t);
            const bool low = m_dragCrossover == 0;
            const float lo = low ? 60.0f : 1000.0f, hi = low ? 500.0f : 8000.0f;
            const float norm = std::clamp(std::log(hz / lo) / std::log(hi / lo), 0.0f, 1.0f);
            m_instance->setParameter(low ? AestraOTT::kXoverLow : AestraOTT::kXoverHigh, norm);
            setDirty();
        }
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left && event.position.y >= m_plot.y - 2.0f &&
        event.position.y <= m_plot.bottom()) {
        for (int i = 0; i < 2; ++i)
            if (std::abs(event.position.x - crossoverX(i)) <= kHandleHit) {
                m_dragCrossover = i;
                return true;
            }
    }

    // Knob vertical drag (rotary -> cursor capture): AestraPanelWindow::handleKnobDrag.
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells()) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set))
        return true;

    return consumeInsideBounds(event);
}

void AestraOTTEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
