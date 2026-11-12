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
constexpr float kCurveSize = 200.0f;

float levelDb(float linear) { return linear > 1.0e-8f ? 20.0f * std::log10(linear) : -120.0f; }
float smooth(float current, float target) { return current + (target - current) * (target > current ? 0.55f : 0.16f); }

// The plugin's own shaper (AestraSat::shape is private): tanh, asymmetric tanh, hard clip.
float shape(float x, int mode) {
    if (mode == 0) return std::tanh(x);
    if (mode == 1) return x >= 0.0f ? std::tanh(x) : std::tanh(1.5f * x) / 1.5f;
    return std::clamp(x, -1.0f, 1.0f);
}

} // namespace

AestraSatEditor::AestraSatEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraSatEditor");
    setPanelTitle("Sat");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    m_mode.param = AestraSat::kMode;
    m_mode.labels = {"Tape", "Tube", "Hard"};
    m_mix.param = AestraSat::kMix;
    m_mix.label = "Mix";
    m_mix.labelWidth = 28.0f;
    layoutControls();
}

void AestraSatEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraSatEditor::layoutControls() {
    const auto b = getBounds();
    m_origin = {b.x, b.y + headerHeight()};
    const float ox = m_origin.x, oy = m_origin.y;
    m_mode.rect = NUIRect(ox + 12.0f, oy + 12.0f, 240.0f, 24.0f);
    m_curveRect = NUIRect(ox + 12.0f, oy + 44.0f, kCurveSize, kCurveSize);
    m_meterRects[0] = NUIRect(ox + 236.0f, oy + 60.0f, 10.0f, 168.0f);
    m_meterRects[1] = NUIRect(ox + 256.0f, oy + 60.0f, 10.0f, 168.0f);

    m_board.clear();
    m_board.addHeader(NUIRect(ox + 284.0f, oy + 44.0f, 224.0f, 12.0f), "Shape");
    m_board.place({AestraSat::kDrive, "Drive", EditorKit::KnobSize::Large}, {ox + 284.0f, oy + 64.0f});
    m_board.place({AestraSat::kTone, "Tone"}, {ox + 372.0f, oy + 64.0f});
    m_board.place({AestraSat::kOutput, "Output", EditorKit::KnobSize::Medium, true}, {ox + 444.0f, oy + 64.0f});
    m_board.addHeader(NUIRect(ox + 284.0f, oy + 176.0f, 224.0f, 12.0f), "Blend");
    m_mix.rect = NUIRect(ox + 284.0f, oy + 196.0f, 224.0f, 24.0f);
}

void AestraSatEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;
    if (auto sat = std::dynamic_pointer_cast<AestraSat>(m_instance)) {
        m_inputDisplay = smooth(m_inputDisplay, std::clamp(sat->getInputLevel(), 0.0f, 16.0f));
        m_outputDisplay = smooth(m_outputDisplay, std::clamp(sat->getOutputLevel(), 0.0f, 16.0f));
    }
    repaint();
}

void AestraSatEditor::drawTransferCurve(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_curveRect;
    EditorKit::drawWell(renderer, w);
    auto px = [&](float x) { return w.x + 8.0f + (x + 1.0f) * 0.5f * (w.width - 16.0f); };
    auto py = [&](float y) { return w.y + 8.0f + (1.0f - y) * 0.5f * (w.height - 16.0f); };
    for (float t : {-0.5f, 0.0f, 0.5f}) {
        const NUIColor col = t == 0.0f ? c.gridMajor : c.gridMinor;
        renderer.fillRect(NUIRect(std::round(px(t)), w.y + 1.0f, 1.0f, w.height - 2.0f), col);
        renderer.fillRect(NUIRect(w.x + 1.0f, std::round(py(t)), w.width - 2.0f, 1.0f), col);
    }
    renderer.drawLine({px(-1.0f), py(-1.0f)}, {px(1.0f), py(1.0f)}, 1.0f, c.inkOff);

    const int mode = std::clamp(static_cast<int>(std::lround(m_instance->getParameter(AestraSat::kMode) * 2.0f)), 0, 2);
    const float gain = std::pow(10.0f, AestraSat::driveDbFromNorm(m_instance->getParameter(AestraSat::kDrive)) / 20.0f);
    constexpr int kSteps = 200;
    NUIPoint pts[kSteps + 1];
    for (int i = 0; i <= kSteps; ++i) {
        const float x = -1.0f + static_cast<float>(i) / 100.0f;
        pts[i] = {px(x), py(bypassed ? x : shape(x * gain, mode))};
    }
    renderer.drawPolyline(pts, kSteps + 1, 2.0f, bypassed ? c.inkOff : c.secondary);

    EditorKit::drawTextRight(renderer, "in", w.right() - 8.0f, w.y + w.height * 0.5f + 8.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextLeft(renderer, "out", w.x + w.width * 0.5f + 6.0f, w.y + 12.0f, 9.0f, c.inkMuted);
    const char* names[3] = {"Tape - tanh", "Tube - asymmetric", "Hard - clip"};
    EditorKit::drawTextLeft(renderer, names[mode], w.x + 8.0f, w.bottom() - 10.0f, 10.0f, c.inkMuted);
}

void AestraSatEditor::drawMeters(NUIRenderer& renderer, bool bypassed) {
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
    for (int t : {0, -12, -24, -48}) {
        const float y = m_meterRects[0].bottom() - 1.0f - (m_meterRects[0].height - 2.0f) * (static_cast<float>(t) + 48.0f) / 48.0f;
        EditorKit::drawTextRight(renderer, std::to_string(t), m_meterRects[0].x - 6.0f, y, 9.0f, c.inkMuted);
    }
}

void AestraSatEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance)
        return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, AestraSat::kBypass);
    m_mode.draw(renderer, *m_instance, hoverPoint(), bypassed);
    drawTransferCurve(renderer, bypassed);
    drawMeters(renderer, bypassed);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
    m_mix.draw(renderer, *m_instance, hoverPoint(), bypassed);
}

bool AestraSatEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;
    if (AestraPanelWindow::onMouseEvent(event))
        return true;
    if (!m_instance)
        return false;
    layoutControls();

    if (event.pressed && event.button == NUIMouseButton::Left && m_mode.onPress(*m_instance, event.position)) {
        setDirty();
        return true;
    }

    // Knob vertical drag (rotary -> cursor capture): AestraPanelWindow::handleKnobDrag.
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells()) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set))
        return true;

    if (m_mix.onMouse(event, *m_instance)) {
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
