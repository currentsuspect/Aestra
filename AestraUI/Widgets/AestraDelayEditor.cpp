// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDelayEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDelay.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace AestraUI {

namespace {
using Delay = Aestra::Audio::Plugins::AestraDelay;

constexpr float kDragRangePx = 160.0f;
constexpr float kLaneH = 44.0f;

struct Tap {
    int lane;     // 0 = L, 1 = R
    float ms;
    float db;
};

} // namespace

AestraDelayEditor::AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDelayEditor");
    setPanelTitle("Delay");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    m_sync.param = Delay::kSyncMode;
    m_sync.labels = {"Free", "Sync"};
    m_stereoMode.param = Delay::kStereoMode;
    m_stereoMode.labels = {"Stereo", "Ping-Pong"};
    m_division.param = Delay::kNoteDivision;
    m_division.labels = EditorKit::noteDivisionLabels();
    m_division.order = EditorKit::noteDivisionMenuOrder();
    m_mix.param = Delay::kMix;
    m_mix.label = "Mix";
    m_mix.labelWidth = 40.0f;
    layoutControls();
}

void AestraDelayEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraDelayEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_well = NUIRect(ox + 12.0f, oy + 12.0f, 656.0f, 132.0f);
    m_sync.rect = NUIRect(ox + 12.0f, oy + 156.0f, 128.0f, 24.0f);
    m_division.rect = NUIRect(ox + 148.0f, oy + 156.0f, 96.0f, 24.0f);
    m_stereoMode.rect = NUIRect(ox + 256.0f, oy + 156.0f, 176.0f, 24.0f);
    m_mix.rect = NUIRect(ox + 12.0f, oy + 302.0f, 656.0f, 24.0f);

    using K = EditorKit::EmberKnobBoard;
    constexpr auto kMedium = EditorKit::KnobSize::Medium;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 192.0f}, 656.0f,
                      {K::Group{"Time", {{Delay::kTime, "Time"}}},
                       K::Group{"Repeats", {{Delay::kFeedback, "Feedback"}, {Delay::kDamping, "Damping"},
                                            {Delay::kFeedbackHighpass, "Low Cut"}}},
                       K::Group{"Stereo", {{Delay::kStereoShift, "Stereo", kMedium, true}}},
                       K::Group{"Motion", {{Delay::kModDepth, "Mod Depth"}, {Delay::kModRate, "Mod Rate"}}},
                       K::Group{"Output", {{Delay::kOutputTrim, "Output", kMedium, true}}}});
}

float AestraDelayEditor::effectiveMs() const {
    if (auto d = std::dynamic_pointer_cast<Delay>(m_instance)) return d->getEffectiveDelayMs();
    return 10.0f + m_instance->getParameter(Delay::kTime) * 1990.0f;
}

void AestraDelayEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_repaintTimer += deltaTime;
    if (m_repaintTimer < 1.0 / 20.0) return; // the tempo can move under a synced delay
    m_repaintTimer = 0.0;
    repaint();
}

void AestraDelayEditor::drawTaps(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_well;
    EditorKit::drawWell(renderer, w);

    const bool synced = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    const float t = effectiveMs();
    const float shift = m_instance->getParameter(Delay::kStereoShift) * 2.0f - 1.0f;
    const float off = shift * std::min(t * 0.5f, 500.0f);
    const float tL = std::max(t - off, 10.0f), tR = std::max(t + off, 10.0f);
    const float fb = m_instance->getParameter(Delay::kFeedback) * 0.95f;
    const bool pingPong = m_instance->getParameter(Delay::kStereoMode) > 0.5f;

    // Each repeat n sits at n x the lane's time and is fb^(n-1) as loud, cut off at -48 dB.
    std::vector<Tap> taps;
    auto levelDb = [&](int n) { return 20.0f * std::log10(std::max(1.0e-9f, std::pow(fb, static_cast<float>(n - 1)))); };
    if (pingPong) {
        const float per = (tL + tR) * 0.5f;
        for (int n = 1; n <= 24; ++n) {
            const float db = levelDb(n);
            if (db < -48.0f) break;
            taps.push_back({n % 2 ? 0 : 1, static_cast<float>(n) * per, db});
        }
    } else {
        for (int n = 1; n <= 24; ++n) {
            const float db = levelDb(n);
            if (db < -48.0f) break;
            taps.push_back({0, static_cast<float>(n) * tL, db});
            taps.push_back({1, static_cast<float>(n) * tR, db});
        }
    }
    float lastMs = t;
    for (const auto& tap : taps) lastMs = std::max(lastMs, tap.ms);
    const float win = std::min(std::max(lastMs * 1.08f, t * 4.2f), 12000.0f);
    const float step = win > 6000.0f ? 1000.0f : win > 2400.0f ? 500.0f : win > 1000.0f ? 250.0f : 100.0f;
    auto tx = [&](float ms) { return w.x + 28.0f + (ms / win) * (w.width - 40.0f); };

    for (float ms = step; ms < win; ms += step) {
        const float gx = std::round(tx(ms));
        renderer.fillRect(NUIRect(gx, w.y + 1.0f, 1.0f, w.height - 19.0f), c.gridMinor);
        char buf[16];
        if (ms >= 1000.0f) std::snprintf(buf, sizeof(buf), "%g s", static_cast<double>(ms / 1000.0f));
        else std::snprintf(buf, sizeof(buf), "%d ms", static_cast<int>(ms));
        const float tw = renderer.measureText(buf, 9.0f).width;
        EditorKit::drawTextLeft(renderer, buf, gx - tw * 0.5f, w.bottom() - 9.0f, 9.0f, c.inkMuted);
    }
    const float laneY[2] = {w.y + 12.0f, w.y + 62.0f};
    for (int i = 0; i < 2; ++i) {
        renderer.fillRect(NUIRect(w.x + 28.0f, laneY[i] + kLaneH, w.width - 40.0f, 1.0f), c.border);
        EditorKit::drawCaps(renderer, i ? "R" : "L", w.x + 12.0f, laneY[i] + kLaneH - 8.0f, 10.0f, c.inkMuted);
        renderer.fillRect(NUIRect(tx(0.0f) - 1.0f, laneY[i], 2.0f, kLaneH), c.inkMuted);
    }
    for (const auto& tap : taps) {
        if (tap.ms > win) continue;
        const float hh = std::max(2.0f, kLaneH * (tap.db + 48.0f) / 48.0f);
        renderer.fillRect(NUIRect(std::round(tx(tap.ms)) - 2.0f, laneY[tap.lane] + kLaneH - hh, 4.0f, hh),
                          bypassed ? c.inkOff : c.secondary);
    }

    char head[96];
    if (synced) {
        const auto idx = static_cast<size_t>(Delay::noteDivisionIndexFromParam(m_instance->getParameter(Delay::kNoteDivision)));
        std::snprintf(head, sizeof(head), "%s - %d ms", EditorKit::noteDivisionLabels()[idx].c_str(),
                      static_cast<int>(std::lround(t)));
    } else {
        std::snprintf(head, sizeof(head), "%d ms - Free", static_cast<int>(std::lround(t)));
    }
    EditorKit::drawTextRight(renderer, head, w.right() - 12.0f, w.y + 12.0f, 12.0f, c.ink);
    char sub[64];
    if (pingPong) std::snprintf(sub, sizeof(sub), "Ping-pong, alternating");
    else if (std::fabs(off) > 0.5f)
        std::snprintf(sub, sizeof(sub), "L %d ms - R %d ms", static_cast<int>(std::lround(tL)), static_cast<int>(std::lround(tR)));
    else std::snprintf(sub, sizeof(sub), "L and R together");
    EditorKit::drawTextRight(renderer, sub, w.right() - 12.0f, w.y + 28.0f, 11.0f, c.inkMuted);
}

void AestraDelayEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, Delay::kBypass);
    const bool synced = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    drawTaps(renderer, bypassed);

    m_sync.draw(renderer, *m_instance, hoverPoint(), bypassed);
    m_division.enabled = synced;
    m_division.draw(renderer, *m_instance, hoverPoint(), bypassed);
    m_stereoMode.draw(renderer, *m_instance, hoverPoint(), bypassed);

    // Time and Division are both on screen; the one that does not apply is greyed, and a greyed Time shows the
    // synced length.
    m_board.setEnabled(Delay::kTime, !synced);
    m_board.setValueText(Delay::kTime, synced ? std::to_string(static_cast<int>(std::lround(effectiveMs()))) + "ms" : "");
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
    m_mix.draw(renderer, *m_instance, hoverPoint(), bypassed);
}

void AestraDelayEditor::drawOverlay(NUIRenderer& renderer) {
    if (m_instance) m_division.drawMenu(renderer, *m_instance, hoverPoint(), getBounds());
}

bool AestraDelayEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    if (m_division.onMouse(event, *m_instance, getBounds())) {
        setDirty();
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left &&
        (m_sync.onPress(*m_instance, event.position) || m_stereoMode.onPress(*m_instance, event.position))) {
        setDirty();
        return true;
    }

    const bool synced = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells())
        if (cell.spec.param != Delay::kTime || !synced) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    if (m_mix.onMouse(event, *m_instance)) {
        setDirty();
        return true;
    }
    return consumeInsideBounds(event);
}

void AestraDelayEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
