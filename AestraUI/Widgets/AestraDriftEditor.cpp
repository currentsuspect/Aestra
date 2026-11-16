// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDriftEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDrift.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace AestraUI {

namespace {
using Drift = Aestra::Audio::Plugins::AestraDrift;

constexpr float kDragRangePx = 160.0f;
constexpr std::array<int, 7> kIntervals{-12, -7, -5, 0, 5, 7, 12};

std::string signedInt(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }

} // namespace

AestraDriftEditor::AestraDriftEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDriftEditor");
    setPanelTitle("Drift");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    for (int q : kIntervals) m_intervals.labels.push_back(signedInt(q));
    m_mix.param = Drift::kMix;
    m_mix.label = "Mix";
    m_mix.labelWidth = 40.0f;
    layoutControls();
}

void AestraDriftEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraDriftEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_well = NUIRect(ox + 12.0f, oy + 12.0f, kWinW - 24.0f, 108.0f);
    m_intervals.rect = NUIRect(ox + 12.0f, oy + 128.0f, kWinW - 24.0f, 24.0f);
    m_mix.rect = NUIRect(ox + 12.0f, oy + 274.0f, kWinW - 24.0f, 24.0f);

    using K = EditorKit::EmberKnobBoard;
    constexpr auto kMedium = EditorKit::KnobSize::Medium;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 164.0f}, kWinW - 24.0f,
                      {K::Group{"Pitch", {{Drift::kPitch, "Pitch", kMedium, true}, {Drift::kFine, "Fine", kMedium, true}}},
                       K::Group{"Grain", {{Drift::kGrain, "Grain"}, {Drift::kTexture, "Texture"}, {Drift::kSpread, "Spread"}}},
                       K::Group{"Motion", {{Drift::kMotion, "Motion"}, {Drift::kMotionRate, "Motion Rate"}}},
                       K::Group{"Output", {{Drift::kOutput, "Output", kMedium, true}}}});
}

void AestraDriftEditor::drawRuler(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_well;
    EditorKit::drawWell(renderer, w);

    const float pitchSt = -12.0f + m_instance->getParameter(Drift::kPitch) * 24.0f;
    const float fineCt = m_instance->getParameter(Drift::kFine) * 200.0f - 100.0f;
    const float motionV = m_instance->getParameter(Drift::kMotion);
    const float motionCt = motionV * motionV * 25.0f;
    const float st = pitchSt + fineCt / 100.0f, mot = motionCt / 100.0f;

    auto sx = [&](float q) { return w.x + 24.0f + (q + 12.0f) / 24.0f * (w.width - 48.0f); };
    const float ry = w.y + 64.0f;
    for (int q = -12; q <= 12; ++q) {
        const bool major = q == -12 || q == -7 || q == -5 || q == 0 || q == 5 || q == 7 || q == 12;
        const float gx = std::round(sx(static_cast<float>(q)));
        renderer.fillRect(NUIRect(gx, ry - (major ? 16.0f : 8.0f), 1.0f, major ? 16.0f : 8.0f), major ? c.edge : c.border);
        if (major) {
            const std::string t = signedInt(q);
            const float tw = renderer.measureText(t, 9.0f).width;
            EditorKit::drawTextLeft(renderer, t, gx - tw * 0.5f, ry + 12.0f, 9.0f, c.inkMuted);
        }
    }
    renderer.fillRect(NUIRect(sx(-12.0f), ry, sx(12.0f) - sx(-12.0f), 1.0f), c.edge);
    if (mot > 0.0f)
        renderer.fillRect(NUIRect(sx(st - mot), ry - 26.0f, sx(st + mot) - sx(st - mot), 26.0f), c.primary.withAlpha(0.12f));
    const float mx = std::round(sx(std::clamp(st, -12.0f, 12.0f)));
    const NUIColor marker = bypassed ? c.inkOff : c.secondary;
    renderer.fillRect(NUIRect(mx - 1.0f, ry - 30.0f, 3.0f, 30.0f), marker);
    renderer.fillRect(NUIRect(mx - 5.0f, ry - 34.0f, 11.0f, 5.0f), marker);

    const std::string pitchText = m_instance->getParameterDisplay(Drift::kPitch);
    EditorKit::drawTextLeft(renderer, pitchText, w.x + 12.0f, w.y + 16.0f, 16.0f, c.ink);
    std::string detail = m_instance->getParameterDisplay(Drift::kFine);
    if (mot > 0.0f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), " - motion +/-%d ct", static_cast<int>(std::lround(mot * 100.0f)));
        detail += buf;
    }
    EditorKit::drawTextLeft(renderer, detail, w.x + 12.0f + renderer.measureText(pitchText, 16.0f).width + 10.0f,
                            w.y + 17.0f, 11.0f, c.inkMuted);

    // Clean until Texture, Motion or Spread colours the sound.
    const bool clean = m_instance->getParameter(Drift::kTexture) < 0.001f && motionV < 0.001f &&
                       m_instance->getParameter(Drift::kSpread) < 0.001f;
    const std::string chip = clean ? "CLEAN" : "COLOUR";
    const float cw = renderer.measureText(chip, 10.0f).width + 28.0f;
    const NUIRect chipRect(w.right() - 12.0f - cw, w.y + 8.0f, cw, 18.0f);
    renderer.fillRect(chipRect, c.raised);
    EditorKit::drawCaps(renderer, chip, chipRect.x + 10.0f, chipRect.y + 9.0f, 10.0f, clean ? c.meter : c.warn);
}

void AestraDriftEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, Drift::kBypass);
    drawRuler(renderer, bypassed);

    // The interval row shows the interval Pitch is on, if it is on one.
    const float pitchSt = -12.0f + m_instance->getParameter(Drift::kPitch) * 24.0f;
    int cur = -1;
    for (size_t i = 0; i < kIntervals.size(); ++i)
        if (std::fabs(pitchSt - static_cast<float>(kIntervals[i])) < 0.25f) cur = static_cast<int>(i);
    EditorKit::drawSegmented(renderer, m_intervals.rect, m_intervals.labels.data(), static_cast<int>(m_intervals.labels.size()),
                             cur, hoverPoint() ? m_intervals.hit(*hoverPoint()) : -1, bypassed);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
    m_mix.draw(renderer, *m_instance, hoverPoint(), bypassed);
}

bool AestraDriftEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    // An interval shortcut writes Pitch, as it always has.
    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int i = m_intervals.hit(event.position);
        if (i >= 0) {
            m_instance->setParameter(Drift::kPitch, (static_cast<float>(kIntervals[static_cast<size_t>(i)]) + 12.0f) / 24.0f);
            setDirty();
            return true;
        }
    }

    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells())
        if (cell.enabled) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    if (m_mix.onMouse(event, *m_instance)) {
        setDirty();
        return true;
    }
    return consumeInsideBounds(event);
}

void AestraDriftEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
