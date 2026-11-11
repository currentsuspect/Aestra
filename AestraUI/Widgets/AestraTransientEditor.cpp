// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraTransientEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraTransient.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>

namespace AestraUI {

namespace {
constexpr float kDragRangePx = 160.0f;

float levelDb(float linear) { return linear > 1.0e-8f ? 20.0f * std::log10(linear) : -120.0f; }
float smooth(float current, float target) { return current + (target - current) * (target > current ? 0.55f : 0.16f); }
} // namespace

AestraTransientEditor::AestraTransientEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraTransientEditor");
    setPanelTitle("Transient");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    layoutControls();
}

void AestraTransientEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraTransientEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_sketchRect = NUIRect(ox + 12.0f, oy + 12.0f, 452.0f, 148.0f);
    m_meterRects[0] = NUIRect(ox + 478.0f, oy + 28.0f, 10.0f, 132.0f);
    m_meterRects[1] = NUIRect(ox + 496.0f, oy + 28.0f, 10.0f, 132.0f);

    using K = EditorKit::EmberKnobBoard;
    constexpr auto kLarge = EditorKit::KnobSize::Large;
    constexpr auto kMedium = EditorKit::KnobSize::Medium;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 172.0f}, 496.0f,
                      {K::Group{"Shape", {{kAttack, "Attack", kLarge, true}, {kSustain, "Sustain", kLarge, true}}},
                       K::Group{"Output", {{kOutput, "Output", kMedium, true}, {kMix, "Mix", kMedium, false}}}});
}

void AestraTransientEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;
    if (auto t = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraTransient>(m_instance)) {
        m_inputDisplay = smooth(m_inputDisplay, std::clamp(t->getInputLevel(), 0.0f, 16.0f));
        m_outputDisplay = smooth(m_outputDisplay, std::clamp(t->getOutputLevel(), 0.0f, 16.0f));
    }
    repaint();
}

void AestraTransientEditor::drawEnvelopeSketch(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_sketchRect;
    EditorKit::drawWell(renderer, w);
    const float gx0 = w.x + 12.0f, gw = w.width - 24.0f, gy0 = w.y + 14.0f, gh = w.height - 36.0f;
    const float atk = m_instance->getParameter(kAttack) * 2.0f - 1.0f;
    const float sus = m_instance->getParameter(kSustain) * 2.0f - 1.0f;
    auto env = [](float t) { return t < 6.0f ? t / 6.0f : std::exp(-(t - 6.0f) / 70.0f); };
    auto proc = [&](float t) {
        const float e = env(t);
        const float onset = t < 18.0f ? 1.0f + atk * 0.9f * (1.0f - t / 18.0f) : 1.0f;
        const float tail = t > 18.0f ? std::pow(e, 1.0f - sus * 0.5f) / e : 1.0f;
        return std::clamp(e * onset * tail, 0.0f, 1.9f);
    };
    auto tx = [&](float t) { return gx0 + t / 300.0f * gw; };
    auto ty = [&](float y) { return gy0 + gh - y / 1.9f * gh; };

    renderer.fillRect(NUIRect(std::round(tx(18.0f)), gy0, 1.0f, gh), c.gridMinor);
    EditorKit::drawCaps(renderer, "Attack", tx(9.0f) - 18.0f, w.bottom() - 10.0f, 9.0f, c.inkMuted);
    EditorKit::drawCaps(renderer, "Sustain", tx(160.0f) - 20.0f, w.bottom() - 10.0f, 9.0f, c.inkMuted);

    constexpr int kSteps = 300;
    static NUIPoint dry[kSteps + 1], wet[kSteps + 1];
    for (int i = 0; i <= kSteps; ++i) {
        dry[i] = {tx(static_cast<float>(i)), ty(env(static_cast<float>(i)))};
        wet[i] = {tx(static_cast<float>(i)), ty(proc(static_cast<float>(i)))};
    }
    renderer.drawPolyline(dry, kSteps + 1, 1.0f, c.inkOff);
    renderer.drawPolyline(wet, kSteps + 1, 2.0f, bypassed ? c.inkOff : c.secondary);
    EditorKit::drawTextRight(renderer, "Sketch from Attack and Sustain; not measured", w.right() - 10.0f, w.y + 12.0f,
                             10.0f, c.inkMuted);
}

void AestraTransientEditor::drawMeters(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    EditorKit::drawVerticalMeter(renderer, m_meterRects[0], levelDb(m_inputDisplay), EditorKit::MeterKind::Level,
                                 bypassed);
    EditorKit::drawVerticalMeter(renderer, m_meterRects[1], levelDb(m_outputDisplay), EditorKit::MeterKind::Level,
                                 bypassed);
    const char* names[2] = {"In", "Out"};
    for (int i = 0; i < 2; ++i) {
        const NUIRect& m = m_meterRects[i];
        renderer.drawTextCentered(names[i], NUIRect(m.x - 10.0f, m.y - 19.0f, m.width + 20.0f, 20.0f), 9.0f, c.inkMuted);
    }
    for (int t : {0, -24, -48}) {
        const float y = m_meterRects[0].bottom() - 1.0f - (m_meterRects[0].height - 2.0f) * (static_cast<float>(t) + 48.0f) / 48.0f;
        EditorKit::drawTextRight(renderer, std::to_string(t), m_meterRects[0].x - 6.0f, y, 9.0f, c.inkMuted);
    }
}

void AestraTransientEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, kBypass);
    drawEnvelopeSketch(renderer, bypassed);
    drawMeters(renderer, bypassed);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
}

bool AestraTransientEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    // Knob vertical drag (rotary -> cursor capture): AestraPanelWindow::handleKnobDrag.
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells()) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    return consumeInsideBounds(event);
}

void AestraTransientEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
