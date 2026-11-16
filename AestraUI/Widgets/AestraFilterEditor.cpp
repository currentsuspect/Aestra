// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraFilterEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraFilter.h"
#include "PluginEditorKit.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace AestraUI {

namespace {
using Aestra::Audio::Plugins::AestraFilter;

constexpr float kDragRangePx = 160.0f;
constexpr float kTopDb = 24.0f;
constexpr float kBottomDb = -48.0f;
constexpr float kEnvOctaves = 4.0f; // AestraFilter::kEnvModOctaves

// Magnitude of a 2-pole state-variable response at @p f for cutoff @p fc and quality @p q, in dB.
float magnitudeDb(float f, float fc, float q, int type) {
    const float x = f / fc;
    const float den = std::sqrt((1.0f - x * x) * (1.0f - x * x) + (x / q) * (x / q));
    const float num = type == 0 ? 1.0f : type == 1 ? x / q : x * x;
    return 20.0f * std::log10(std::max(1.0e-9f, num / den));
}

} // namespace

AestraFilterEditor::AestraFilterEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraFilterEditor");
    setPanelTitle("Filter");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    m_type.param = AestraFilter::kType;
    m_type.labels = {"Low Pass", "Band Pass", "High Pass"};
    layoutControls();
}

void AestraFilterEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
}

void AestraFilterEditor::layoutControls() {
    const auto b = getBounds();
    const float ox = b.x, oy = b.y + headerHeight();
    m_type.rect = NUIRect(ox + 12.0f, oy + 12.0f, 264.0f, 24.0f);
    m_well = NUIRect(ox + 12.0f, oy + 44.0f, 576.0f, 152.0f);

    using K = EditorKit::EmberKnobBoard;
    constexpr auto kLarge = EditorKit::KnobSize::Large;
    constexpr auto kMedium = EditorKit::KnobSize::Medium;
    m_board.clear();
    m_board.layoutRow({ox + 12.0f, oy + 208.0f}, 576.0f,
                      {K::Group{"Filter",
                                {{AestraFilter::kCutoff, "Cutoff", kLarge},
                                 {AestraFilter::kReso, "Resonance"},
                                 {AestraFilter::kDrive, "Drive"}}},
                       K::Group{"Envelope",
                                {{AestraFilter::kEnvAmount, "Envelope", kMedium, true},
                                 {AestraFilter::kEnvAttack, "Attack"},
                                 {AestraFilter::kEnvRelease, "Release"}}},
                       K::Group{"Output", {{AestraFilter::kMix, "Mix"}}}});
}

void AestraFilterEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;
    if (auto f = std::dynamic_pointer_cast<AestraFilter>(m_instance))
        m_envLevel += (std::clamp(f->getEnvelopeLevel(), 0.0f, 1.0f) - m_envLevel) * 0.5f;
    repaint();
}

void AestraFilterEditor::drawResponse(NUIRenderer& renderer, bool bypassed) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_well;
    EditorKit::drawWell(renderer, w);
    const float gx0 = w.x + 28.0f, gw = w.width - 36.0f, gy0 = w.y + 8.0f, gh = w.height - 28.0f;
    auto fx = [&](float f) { return EditorKit::logFrequencyX(f, gx0, gw); };
    auto gy = [&](float db) { return gy0 + (kTopDb - db) / (kTopDb - kBottomDb) * gh; };

    EditorKit::drawFrequencyGrid(renderer, NUIRect(gx0, gy0, gw, gh + 20.0f));
    for (int d = 24; d >= -48; d -= 12) {
        const float y = std::round(gy(static_cast<float>(d)));
        renderer.fillRect(NUIRect(gx0, y, gw, 1.0f), d == 0 ? c.border : c.gridMinor);
        EditorKit::drawTextRight(renderer, std::to_string(d), w.x + 22.0f, y, 9.0f, c.inkMuted);
    }

    const int type = std::clamp(static_cast<int>(std::lround(m_instance->getParameter(AestraFilter::kType) * 2.0f)), 0, 2);
    const float fc = AestraFilter::cutoffHzFromNorm(m_instance->getParameter(AestraFilter::kCutoff));
    const float q = AestraFilter::qFromNorm(m_instance->getParameter(AestraFilter::kReso));
    const float env = m_instance->getParameter(AestraFilter::kEnvAmount) * 2.0f - 1.0f;

    constexpr int kSteps = 300;
    static NUIPoint pts[kSteps + 1];
    auto curve = [&](float cutoff, NUIColor color, float width) {
        for (int i = 0; i <= kSteps; ++i) {
            const float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / kSteps);
            const float db = std::clamp(magnitudeDb(f, cutoff, q, type), kBottomDb, kTopDb);
            pts[i] = {fx(f), gy(db)};
        }
        renderer.drawPolyline(pts, kSteps + 1, width, color);
    };

    if (std::fabs(env) > 0.005f) {
        // Where the envelope can push the cutoff: Cutoff x 2^(Envelope x 4 octaves).
        const float fe = std::clamp(fc * std::pow(2.0f, env * kEnvOctaves), 20.0f, 20000.0f);
        const float x0 = std::min(fx(fc), fx(fe)), x1 = std::max(fx(fc), fx(fe));
        renderer.fillRect(NUIRect(x0, gy0 + gh - 6.0f, x1 - x0, 4.0f), c.guide);
        curve(fe, c.inkOff, 1.0f);
        const std::string label = "envelope reach";
        if (env > 0.0f) EditorKit::drawTextRight(renderer, label, fx(fe) - 6.0f, gy0 + gh - 14.0f, 10.0f, c.inkMuted);
        else EditorKit::drawTextLeft(renderer, label, fx(fe) + 6.0f, gy0 + gh - 14.0f, 10.0f, c.inkMuted);
    }
    curve(fc, bypassed ? c.inkOff : c.secondary, 2.0f);

    // The tick on the axis follows the envelope follower: where the cutoff is right now.
    const float live = std::clamp(fc * std::pow(2.0f, env * kEnvOctaves * m_envLevel), 20.0f, 20000.0f);
    renderer.fillRect(NUIRect(std::round(fx(live)) - 1.0f, gy0 + gh - 10.0f, 3.0f, 10.0f), bypassed ? c.inkOff : c.meter);
}

void AestraFilterEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    if (!m_instance) return;
    layoutControls();
    const bool bypassed = EditorKit::isBypassed(*m_instance, AestraFilter::kBypass);
    m_type.draw(renderer, *m_instance, hoverPoint(), bypassed);
    drawResponse(renderer, bypassed);
    m_board.draw(renderer, *m_instance, hoverPoint(), knobDragParam(), bypassed);
}

bool AestraFilterEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;
    if (!m_instance) return false;
    layoutControls();

    if (event.pressed && event.button == NUIMouseButton::Left && m_type.onPress(*m_instance, event.position)) {
        setDirty();
        return true;
    }

    std::vector<KnobTarget> knobs;
    for (const auto& cell : m_board.cells())
        if (cell.spec.param != EditorKit::kNoParam) knobs.push_back({cell.rect, cell.spec.param});
    const auto get = [this](uint32_t id) { return m_instance->getParameter(id); };
    const auto set = [this](uint32_t id, float v) { m_instance->setParameter(id, v); };
    if (handleKnobDrag(event, knobs.data(), knobs.size(), kDragRangePx, get, set)) return true;

    return consumeInsideBounds(event);
}

void AestraFilterEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layoutControls();
}

} // namespace AestraUI
