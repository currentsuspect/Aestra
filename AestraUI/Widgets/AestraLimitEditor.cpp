// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraLimitEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraLimit.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
using Limit = Aestra::Audio::Plugins::AestraLimit;

constexpr float kDragRangePx = 160.0f;
constexpr float kHistoryRangeDb = 12.0f; // the history runs 0 to -12 dB
constexpr double kHistoryStep = 4.0 / 90.0; // seconds per bar: the well shows the last 4 s

float levelDb(float linear) { return linear > 1.0e-8f ? 20.0f * std::log10(linear) : -120.0f; }
float smoothMeter(float current, float target, float attack, float release) {
    return current + (target - current) * (target > current ? attack : release);
}
// The ceiling's range is -24 to 0 dBTP.
float ceilingDb(float norm) { return -24.0f + std::clamp(norm, 0.0f, 1.0f) * 24.0f; }
} // namespace

AestraLimitEditor::AestraLimitEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraLimitEditor");
    setPanelTitle("Limit");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    m_releaseMode.param = Limit::kReleaseMode;
    m_releaseMode.labels = {"Auto", "Manual"};
    layoutControls();
}

void AestraLimitEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraLimitEditor::layoutControls() {
    const auto b = getBounds();
    m_origin = {b.x, b.y + headerHeight()};
    const float ox = m_origin.x, oy = m_origin.y;
    m_historyRect = NUIRect(ox + 12.0f, oy + 36.0f, 388.0f, 144.0f); // a 24 px row above it holds the readout
    m_meterRects[0] = NUIRect(ox + 430.0f, oy + 28.0f, 10.0f, 152.0f);
    m_meterRects[1] = NUIRect(ox + 458.0f, oy + 28.0f, 10.0f, 152.0f);

    m_board.clear();
    m_board.addHeader(NUIRect(ox + 12.0f, oy + 208.0f, 96.0f, 12.0f), "Ceiling");
    m_board.place({Limit::kCeiling, "Ceiling", EditorKit::KnobSize::Large}, {ox + 20.0f, oy + 228.0f});
    m_board.addHeader(NUIRect(ox + 124.0f, oy + 208.0f, 384.0f, 12.0f), "Release");
    m_board.place({Limit::kRelease, "Release"}, {ox + 316.0f, oy + 228.0f});
    m_releaseMode.rect = NUIRect(ox + 124.0f, oy + 228.0f, 176.0f, 24.0f);
}

void AestraLimitEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    m_historyTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    const double elapsed = m_meterTimer;
    m_meterTimer = 0.0;
    if (auto limit = std::dynamic_pointer_cast<Limit>(m_instance)) {
        const float gr = std::clamp(limit->getGainReductionDb(), 0.0f, 48.0f);
        m_grDisplayDb = smoothMeter(m_grDisplayDb, gr, 0.60f, 0.15f);
        m_inputDisplay = smoothMeter(m_inputDisplay, std::clamp(limit->getInputLevel(), 0.0f, 16.0f), 0.55f, 0.16f);
        m_outputDisplay = smoothMeter(m_outputDisplay, std::clamp(limit->getOutputLevel(), 0.0f, 16.0f), 0.55f, 0.16f);
    }
    (void)elapsed;
    while (m_historyTimer >= kHistoryStep) {
        m_historyTimer -= kHistoryStep;
        std::rotate(m_history.begin(), m_history.begin() + 1, m_history.end());
        m_history.back() = m_grDisplayDb;
    }
    repaint();
}

void AestraLimitEditor::drawHistory(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_historyRect;
    EditorKit::drawWell(renderer, w);
    auto sy = [&](float db) { return w.y + 8.0f + (-db / kHistoryRangeDb) * (w.height - 28.0f); };
    for (int d : {0, -3, -6, -9, -12}) {
        const float y = std::round(sy(static_cast<float>(d)));
        renderer.fillRect(NUIRect(w.x + 28.0f, y, w.width - 36.0f, 1.0f), c.gridMinor);
        EditorKit::drawTextRight(renderer, std::to_string(d), w.x + 22.0f, y, 9.0f, c.inkMuted);
    }
    for (int i = 0; i < kHistoryBars; ++i) {
        const float g = -std::min(m_history[static_cast<size_t>(i)], kHistoryRangeDb);
        if (bypassed || g > -0.01f) continue;
        const float y0 = sy(0.0f), y1 = sy(g);
        renderer.fillRect(NUIRect(w.x + 30.0f + static_cast<float>(i) * 4.0f, y0, 3.0f, y1 - y0), c.warn);
    }
    EditorKit::drawTextLeft(renderer, "4 s", w.x + 30.0f, w.bottom() - 9.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextRight(renderer, "now", w.right() - 10.0f, w.bottom() - 9.0f, 9.0f, c.inkMuted);

    // The current reduction sits in the row above the well, never on the bars.
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.1f dB", bypassed ? 0.0 : -static_cast<double>(m_grDisplayDb));
    const float rowY = w.y - 14.0f;
    EditorKit::drawTextRight(renderer, buf, w.right(), rowY, 16.0f, c.ink);
    const float vw = renderer.measureText(buf, 16.0f).width;
    const std::string caption = "GAIN REDUCTION";
    const float capsW = renderer.measureText(caption, 9.0f).width + 9.0f * 0.12f * static_cast<float>(caption.size());
    EditorKit::drawCaps(renderer, caption, w.right() - vw - 10.0f - capsW, rowY, 9.0f, c.inkMuted);
}

void AestraLimitEditor::drawMeters(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const float inDb = levelDb(m_inputDisplay), outDb = levelDb(m_outputDisplay);
    EditorKit::drawVerticalMeter(renderer, m_meterRects[0], inDb, EditorKit::MeterKind::Level, bypassed);
    EditorKit::drawVerticalMeter(renderer, m_meterRects[1], outDb, EditorKit::MeterKind::Level, bypassed);
    const char* names[2] = {"In", "Out"};
    const float values[2] = {std::max(inDb, -99.9f), std::max(outDb, -99.9f)};
    for (int i = 0; i < 2; ++i) {
        const NUIRect& m = m_meterRects[i];
        const float cx = m.x + m.width * 0.5f;
        renderer.drawTextCentered(names[i], NUIRect(cx - 14.0f, m.y - 19.0f, 28.0f, 20.0f), 9.0f, c.inkMuted);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(values[i]));
        renderer.drawTextCentered(buf, NUIRect(cx - 14.0f, m.bottom() + 4.0f, 28.0f, 20.0f), 10.0f, c.ink);
    }
    for (int t : {0, -12, -24, -48}) {
        const float y = m_meterRects[0].bottom() - 1.0f - (m_meterRects[0].height - 2.0f) * (static_cast<float>(t) + 48.0f) / 48.0f;
        EditorKit::drawTextRight(renderer, std::to_string(t), m_meterRects[0].x - 6.0f, y, 9.0f, c.inkMuted);
    }
    // The ceiling as a line across the Out meter.
    const float ceil = ceilingDb(m_instance->getParameter(Limit::kCeiling));
    const NUIRect& o = m_meterRects[1];
    const float cy = o.bottom() - 1.0f - 150.0f * ((ceil + 48.0f) / 48.0f);
    renderer.fillRect(NUIRect(o.x - 4.0f, std::round(cy) - 1.0f, 18.0f, 2.0f), bypassed ? c.inkOff : c.secondary);
    EditorKit::drawTextLeft(renderer, "ceiling", o.right() + 8.0f, cy, 9.0f, c.inkMuted);
}

void AestraLimitEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const auto c = EditorKit::emberColors();
    const bool bypassed = EditorKit::isBypassed(*m_instance, Limit::kBypass);
    const bool manual = m_instance->getParameter(Limit::kReleaseMode) > 0.5f;
    m_board.setEnabled(Limit::kRelease, manual);
    drawHistory(renderer, bypassed);
    drawMeters(renderer, bypassed);
    m_releaseMode.draw(renderer, *m_instance, hoverPoint(), bypassed);
    EditorKit::drawTextLeft(renderer, manual ? "Release applies" : "Release applies in Manual", m_origin.x + 124.0f,
                            m_origin.y + 266.0f, 11.0f, c.inkMuted);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
}

bool AestraLimitEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    if (event.pressed && event.button == NUIMouseButton::Left && m_releaseMode.onPress(*m_instance, event.position)) {
        setDirty();
        return true;
    }

    // Knob vertical drag (rotary -> cursor capture); Release is only live in Manual.
    const bool manual = m_instance->getParameter(Limit::kReleaseMode) > 0.5f;
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells())
        if (cell.spec.param != Limit::kRelease || manual) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    return consumeInsideBounds(event);
}

void AestraLimitEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
