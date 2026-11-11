// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLFOEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraLFO.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace AestraUI {

namespace {
using LFO = Aestra::Audio::Plugins::AestraLFO;

constexpr float kDragRangePx = 160.0f;
constexpr float kPi = 3.14159265358979323846f;

// One cycle of each wave at phase p in 0..1, -1..+1 (S&H is four held steps).
float waveAt(int wave, float p) {
    p = p - std::floor(p);
    switch (wave) {
    case 0: return std::cos(2.0f * kPi * p);
    case 1: return 1.0f - 4.0f * std::min(p, 1.0f - p);
    case 2: return 1.0f - 2.0f * p;
    case 3: return -1.0f + 2.0f * p;
    case 4: return p < 0.5f ? 1.0f : -1.0f;
    default: {
        static const float kSteps[4] = {0.6f, -0.4f, 0.9f, -0.8f};
        return kSteps[std::min(3, static_cast<int>(p * 4.0f))];
    }
    }
}

// Divisions in beats, in the plugins' index order (AestraDelay / AestraLFO share the table).
float divisionBeats(int index) {
    static const float kBeats[13] = {4.0f, 2.0f, 1.0f, 0.25f, 0.5f, 0.125f, 3.0f, 1.5f, 0.75f, 0.375f,
                                     2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f};
    return kBeats[std::clamp(index, 0, 12)];
}

} // namespace

AestraLFOEditor::AestraLFOEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraLFOEditor");
    setPanelTitle("LFO");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    m_target.param = LFO::kTarget;
    m_target.labels = {"Volume", "Pan", "Cutoff"};
    m_sync.param = LFO::kSyncMode;
    m_sync.labels = {"Free", "Sync"};
    m_wave.param = LFO::kWave;
    m_wave.labels = {"Sine", "Triangle", "Saw Down", "Saw Up", "Square", "S&H"};
    m_wave.glyph = [](NUIRenderer& r, int wave, float x, float cy, NUIColor col) {
        NUIPoint pts[17];
        for (int k = 0; k <= 16; ++k) pts[k] = {x + static_cast<float>(k), cy - waveAt(wave, static_cast<float>(k) / 16.0f) * 4.0f};
        r.drawPolyline(pts, 17, 1.5f, col);
    };
    m_division.param = LFO::kNoteDivision;
    m_division.labels = EditorKit::noteDivisionLabels();
    m_division.order = EditorKit::noteDivisionMenuOrder();
    layoutControls();
}

void AestraLFOEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraLFOEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_target.rect = NUIRect(ox + 12.0f, oy + 12.0f, 240.0f, 24.0f);
    m_sync.rect = NUIRect(ox + 420.0f, oy + 12.0f, 128.0f, 24.0f);
    m_wave.rect = NUIRect(ox + 12.0f, oy + 44.0f, 536.0f, 24.0f);
    m_well = NUIRect(ox + 12.0f, oy + 76.0f, 536.0f, 100.0f);

    using K = EditorKit::EmberKnobBoard;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 188.0f}, 536.0f,
                      {K::Group{"Rate", {{LFO::kRateHz, "Rate"}, K::slot(96.0f)}},
                       K::Group{"Shape", {{LFO::kDepth, "Depth"}, {LFO::kPhase, "Phase"}, {LFO::kSmooth, "Smooth"}}}});
    // The Division menu sits in the slot after the Rate knob: a caption, then the control.
    const NUIRect slot = m_board.rectOf(EditorKit::kNoParam);
    m_division.rect = NUIRect(slot.x, slot.y + 16.0f, slot.width, 24.0f);
}

void AestraLFOEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;
    if (auto lfo = std::dynamic_pointer_cast<LFO>(m_instance))
        m_level += (std::clamp(lfo->getLfoLevel(), 0.0f, 1.0f) - m_level) * 0.5f;
    repaint();
}

void AestraLFOEditor::drawShape(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_well;
    EditorKit::drawWell(renderer, w);

    const bool synced = m_instance->getParameter(LFO::kSyncMode) > 0.5f;
    const int target = std::clamp(static_cast<int>(std::lround(m_instance->getParameter(LFO::kTarget) * 2.0f)), 0, 2);
    const int wave = std::clamp(static_cast<int>(std::lround(m_instance->getParameter(LFO::kWave) * 5.0f)), 0, 5);
    const int div = LFO::noteDivisionIndexFromParam(m_instance->getParameter(LFO::kNoteDivision));
    const float bpm = 120.0f; // the mock's reference tempo for the readout
    const float hz = synced ? 1.0f / (divisionBeats(div) * 60.0f / bpm)
                            : LFO::rateHzFromNorm(m_instance->getParameter(LFO::kRateHz));
    const float period = 1.0f / hz;
    const float depth = m_instance->getParameter(LFO::kDepth);
    const float phase = m_instance->getParameter(LFO::kPhase);
    const float tau = LFO::smoothMsFromNorm(m_instance->getParameter(LFO::kSmooth)) * 0.001f;

    const float gx0 = w.x + 44.0f, gw = w.width - 56.0f, gy0 = w.y + 10.0f, gh = w.height - 28.0f;
    const char* top[3] = {"100%", "L", "20 kHz"};
    const char* bottom[3] = {"0%", "R", "-6 oct"};
    EditorKit::drawTextRight(renderer, top[target], w.x + 38.0f, gy0 + 2.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextRight(renderer, bottom[target], w.x + 38.0f, gy0 + gh - 2.0f, 9.0f, c.inkMuted);
    renderer.fillRect(NUIRect(gx0, std::round(gy0 + gh * 0.5f), gw, 1.0f), c.gridMinor);
    renderer.fillRect(NUIRect(std::round(gx0 + gw * 0.5f), gy0, 1.0f, gh), c.gridMinor);

    // Two cycles: the wave offset by Phase, slewed by Smooth, scaled by Depth. Volume and Cutoff hang down from the
    // top by (1 - u) x Depth; Pan swings about the middle.
    constexpr int kSteps = 400;
    static NUIPoint pts[kSteps + 1];
    float y = waveAt(wave, phase);
    for (int i = 0; i <= kSteps; ++i) {
        const float p = static_cast<float>(i) / kSteps * 2.0f + phase;
        const float t = waveAt(wave, p);
        const float dt = period * 2.0f / kSteps;
        const float a = tau > 0.0f ? 1.0f - std::exp(-dt / tau) : 1.0f;
        y += (t - y) * a;
        float out;
        if (target == 1) out = 0.5f - 0.5f * y * depth;
        else out = 1.0f - depth * (1.0f - (0.5f + 0.5f * y));
        pts[i] = {gx0 + static_cast<float>(i) / kSteps * gw, target == 1 ? gy0 + out * gh : gy0 + (1.0f - out) * gh};
    }
    renderer.drawPolyline(pts, kSteps + 1, 2.0f, bypassed ? c.inkOff : c.secondary);
    // The live position: a square on the curve, where its height matches the LFO's current level.
    const float liveOut = target == 1 ? 0.5f - 0.5f * (2.0f * m_level - 1.0f) * depth : 1.0f - depth * (1.0f - m_level);
    const float liveY = target == 1 ? gy0 + liveOut * gh : gy0 + (1.0f - liveOut) * gh;
    int best = 0;
    for (int i = 0; i <= kSteps / 2; ++i)
        if (std::fabs(pts[i].y - liveY) < std::fabs(pts[best].y - liveY)) best = i;
    renderer.fillRect(NUIRect(pts[best].x - 3.0f, pts[best].y - 3.0f, 6.0f, 6.0f), bypassed ? c.inkOff : c.meter);

    char buf[64];
    if (synced) std::snprintf(buf, sizeof(buf), "%s - %.2f s at %d BPM", EditorKit::noteDivisionLabels()[static_cast<size_t>(div)].c_str(),
                              static_cast<double>(period), static_cast<int>(bpm));
    else std::snprintf(buf, sizeof(buf), "%.2f Hz - %.2f s", static_cast<double>(hz), static_cast<double>(period));
    EditorKit::drawTextRight(renderer, buf, w.right() - 10.0f, w.bottom() - 9.0f, 11.0f, c.inkMuted);
    EditorKit::drawTextLeft(renderer, "2 cycles", gx0, w.bottom() - 9.0f, 11.0f, c.inkMuted);
}

void AestraLFOEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const auto c = EditorKit::emberColors();
    const bool bypassed = EditorKit::isBypassed(*m_instance, LFO::kBypass);
    const bool synced = m_instance->getParameter(LFO::kSyncMode) > 0.5f;
    m_target.draw(renderer, *m_instance, hoverPoint(), bypassed);
    m_sync.draw(renderer, *m_instance, hoverPoint(), bypassed);
    m_wave.draw(renderer, *m_instance, hoverPoint(), bypassed);
    drawShape(renderer, bypassed);

    m_board.setEnabled(LFO::kRateHz, !synced);
    m_board.setValueText(LFO::kRateHz, synced ? "Sync" : "");
    m_division.enabled = synced;
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
    EditorKit::drawTextLeft(renderer, "Division", m_division.rect.x, m_division.rect.y - 10.0f, 11.0f,
                            c.inkSoft);
    m_division.draw(renderer, *m_instance, hoverPoint(), bypassed);
}

void AestraLFOEditor::drawOverlay(NUIRenderer& renderer) {
    if (m_instance) m_division.drawMenu(renderer, *m_instance, hoverPoint(), getBounds());
}

bool AestraLFOEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    if (m_division.onMouse(event, *m_instance, getBounds())) {
        setDirty();
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left &&
        (m_target.onPress(*m_instance, event.position) || m_sync.onPress(*m_instance, event.position) ||
         m_wave.onPress(*m_instance, event.position))) {
        setDirty();
        return true;
    }

    const bool synced = m_instance->getParameter(LFO::kSyncMode) > 0.5f;
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells())
        if (cell.spec.param != EditorKit::kNoParam && (cell.spec.param != LFO::kRateHz || !synced))
            knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    return consumeInsideBounds(event);
}

void AestraLFOEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
