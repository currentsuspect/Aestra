// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraCompEditor.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PluginEditorKit.h"
#include "Plugin/AestraComp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
using Comp = Aestra::Audio::Plugins::AestraComp;

// Mock geometry (FD-73), relative to the top-left of the content area.
constexpr float kPad = 12.0f;
constexpr float kCurveSize = 196.0f;
constexpr float kMeterW = 10.0f;
constexpr float kMeterTop = 28.0f;
constexpr float kMeterH = 152.0f;
constexpr float kMeterX[3] = {244.0f, 272.0f, 300.0f};
constexpr float kColumnX = 328.0f;
constexpr float kColumnW = 300.0f;
constexpr float kRowH = 24.0f;
constexpr float kKnobRowY = 220.0f;
constexpr float kDbFloor = -60.0f;

float linearToDb(float linear) { return linear > 1.0e-8f ? 20.0f * std::log10(linear) : -120.0f; }

float smoothMeter(float current, float target, float attack, float release) {
    const float coeff = target > current ? attack : release;
    return current + (target - current) * coeff;
}

float thresholdDbFromNorm(float v) { return -60.0f + std::clamp(v, 0.0f, 1.0f) * 60.0f; }
float ratioFromNorm(float v) { return 1.0f + std::clamp(v, 0.0f, 1.0f) * 19.0f; }
float kneeDbFromNorm(float v) { return std::clamp(v, 0.0f, 1.0f) * 24.0f; }
float makeupDbFromNorm(float v) { return std::clamp(v, 0.0f, 1.0f) * 24.0f; }

// Same curve as the plugin's own static (private there): hard or soft knee, in dB.
float gainReductionDb(float inputDb, float thresholdDb, float ratio, float kneeDb) {
    const float over = inputDb - thresholdDb;
    if (ratio <= 1.0001f) return 0.0f;
    const float slope = 1.0f - 1.0f / ratio;
    if (kneeDb <= 0.001f) return over > 0.0f ? -over * slope : 0.0f;
    const float half = kneeDb * 0.5f;
    if (over <= -half) return 0.0f;
    if (over >= half) return -over * slope;
    const float x = over + half;
    return -slope * x * x / (2.0f * kneeDb);
}

// Signed whole-dB label, "+0dB" / "-20dB", the same string the plugin's display uses.
std::string dbLabel(float db) {
    char buf[24]{};
    const int r = static_cast<int>(std::lround(db));
    std::snprintf(buf, sizeof(buf), "%s%ddB", r >= 0 ? "+" : "", r);
    return buf;
}

std::string oneDecimal(float v) {
    char buf[24]{};
    if (std::fabs(v) < 0.05f) v = 0.0f; // never "-0.0"
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}
} // namespace

AestraCompEditor::AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraCompEditor");
    setPanelTitle("Comp");
    setTwoRowHeader(true);
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildControls();
}

void AestraCompEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
    for (auto& control : m_controls) {
        if (control.slider) control.slider->setPlatformBridge(bridge);
    }
}

void AestraCompEditor::buildControls() {
    m_controls.clear();
    if (!m_instance) return;

    struct Meta { const char* label; uint32_t id; bool bipolar; };

    // Compression group, then Gain group: the order the controls sit on screen.
    const Meta controls[] = {
        {"Threshold", Comp::kThreshold,  false},
        {"Ratio",     Comp::kRatio,      false},
        {"Attack",    Comp::kAttack,     false},
        {"Release",   Comp::kRelease,    false},
        {"Knee",      Comp::kKnee,       false},
        {"Input",     Comp::kInputGain,  true},
        {"Makeup",    Comp::kMakeup,     false},
        {"Output",    Comp::kOutputGain, true},
        {"Mix",       Comp::kMix,        false},
    };

    for (const auto& item : controls) {
        KnobControl control;
        control.label = item.label;
        control.paramId = item.id;
        control.bipolar = item.bipolar;

        auto slider = std::make_shared<NUISlider>();
        slider->setStyle(NUISlider::Style::Rotary);
        slider->setRange(0.0, 1.0);
        slider->setValue(std::clamp(m_instance->getParameter(item.id), 0.0f, 1.0f));
        slider->setPlatformBridge(getPlatformBridge());
        slider->setOnValueChange([this, paramId = item.id](double value) {
            if (m_instance) {
                m_instance->setParameter(paramId, static_cast<float>(std::clamp(value, 0.0, 1.0)));
                repaint();
            }
        });

        control.slider = slider;
        addChild(slider);
        m_controls.push_back(control);
    }

    layoutControls();
}

void AestraCompEditor::layoutControls() {
    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }
    m_origin = {b.x, b.y + headerHeight()};
    const float ox = m_origin.x, oy = m_origin.y;

    m_curveRect = NUIRect(ox + kPad, oy + kPad, kCurveSize, kCurveSize);
    for (int i = 0; i < 3; ++i) m_meterRects[i] = NUIRect(ox + kMeterX[i], oy + kMeterTop, kMeterW, kMeterH);

    m_modeRect = NUIRect(ox + kColumnX, oy + 32.0f, kColumnW, kRowH);
    m_osRect = NUIRect(ox + kColumnX + 84.0f, oy + 84.0f, 112.0f, kRowH);
    m_sidechainRect = NUIRect(ox + kColumnX + 204.0f, oy + 84.0f, 96.0f, kRowH);
    m_hpfRect = NUIRect(ox + kColumnX, oy + 120.0f, kColumnW, kRowH);

    // Two groups of knobs, spread across the row with a wider gap between the groups.
    const float cw = EditorKit::emberKnobCellWidth(EditorKit::KnobSize::Medium);
    const float ch = EditorKit::emberKnobCellHeight(EditorKit::KnobSize::Medium);
    const int n = static_cast<int>(m_controls.size());
    const float rowW = kWinW - kPad * 2.0f;
    const float groupGap = 16.0f;
    const float gap = std::max(4.0f, (rowW - cw * static_cast<float>(n) - groupGap) / static_cast<float>(std::max(1, n - 1)));
    float x = ox + kPad;
    for (int i = 0; i < n; ++i) {
        if (i == 5) x += groupGap;
        auto& c = m_controls[static_cast<size_t>(i)];
        c.cell = NUIRect(x, oy + kKnobRowY + 20.0f, cw, ch);
        if (c.slider) {
            const NUIRect want = c.cell;
            const NUIRect have = c.slider->getBounds();
            if (have.x != want.x || have.y != want.y || have.width != want.width || have.height != want.height)
                c.slider->setBounds(want);
        }
        x += cw + gap;
    }
}

void AestraCompEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

float AestraCompEditor::param(uint32_t paramId) const {
    return m_instance ? std::clamp(m_instance->getParameter(paramId), 0.0f, 1.0f) : 0.0f;
}

void AestraCompEditor::setParam(uint32_t paramId, float value) {
    if (!m_instance) return;
    m_instance->setParameter(paramId, std::clamp(value, 0.0f, 1.0f));
    setDirty(true);
    repaint();
}

void AestraCompEditor::syncControlsFromPlugin() {
    if (!m_instance) return;
    for (auto& control : m_controls) {
        if (control.slider)
            control.slider->setValue(std::clamp(m_instance->getParameter(control.paramId), 0.0f, 1.0f));
    }
}

void AestraCompEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_meterTimer += deltaTime;
    if (m_meterTimer < 1.0 / 30.0) return;
    m_meterTimer = 0.0;

    syncControlsFromPlugin();
    if (auto comp = std::dynamic_pointer_cast<Comp>(m_instance)) {
        const float gr = std::clamp(comp->getCurrentGainReductionDb(), 0.0f, 48.0f);
        const float in = std::clamp(comp->getInputLevel(), 0.0f, 16.0f);
        const float out = std::clamp(comp->getOutputLevel(), 0.0f, 16.0f);
        m_grDisplayDb = smoothMeter(m_grDisplayDb, gr, 0.58f, 0.18f);
        m_inputDisplay = smoothMeter(m_inputDisplay, in, 0.55f, 0.16f);
        m_outputDisplay = smoothMeter(m_outputDisplay, out, 0.55f, 0.16f);
    }
    repaint();
}

void AestraCompEditor::drawTransferCurve(NUIRenderer& renderer) {
    const auto c = EditorKit::emberColors();
    const NUIRect& w = m_curveRect;
    const bool bypassed = isBypassed();

    const float thrDb = thresholdDbFromNorm(param(Comp::kThreshold));
    const float ratio = ratioFromNorm(param(Comp::kRatio));
    const float kneeDb = kneeDbFromNorm(param(Comp::kKnee));
    const float makeupDb = makeupDbFromNorm(param(Comp::kMakeup));

    EditorKit::drawWell(renderer, w);
    auto px = [&](float db) { return w.x + (db - kDbFloor) / -kDbFloor * w.width; };
    auto py = [&](float db) { return w.bottom() - (db - kDbFloor) / -kDbFloor * w.height; };
    auto clampX = [&](float x) { return std::clamp(x, w.x + 1.0f, w.right() - 1.0f); };
    auto clampY = [&](float y) { return std::clamp(y, w.y + 1.0f, w.bottom() - 1.0f); };

    for (float d = -48.0f; d < 0.0f; d += 12.0f) {
        renderer.fillRect(NUIRect(std::round(px(d)), w.y + 1.0f, 1.0f, w.height - 2.0f), c.gridMinor);
        renderer.fillRect(NUIRect(w.x + 1.0f, std::round(py(d)), w.width - 2.0f, 1.0f), c.gridMinor);
    }
    if (kneeDb > 0.0f) {
        const float x0 = clampX(px(thrDb - kneeDb * 0.5f)), x1 = clampX(px(thrDb + kneeDb * 0.5f));
        renderer.fillRect(NUIRect(x0, w.y + 1.0f, x1 - x0, w.height - 2.0f), c.primary.withAlpha(0.12f));
    }
    renderer.fillRect(NUIRect(std::round(clampX(px(thrDb))), w.y + 1.0f, 1.0f, w.height - 2.0f), c.guide);
    renderer.drawLine({w.x + 1.0f, w.bottom() - 1.0f}, {w.right() - 1.0f, w.y + 1.0f}, 1.0f, c.inkOff);

    // Curve = input + the plugin's own gain reduction + makeup.
    constexpr int kSteps = 120;
    NUIPoint pts[kSteps + 1];
    for (int i = 0; i <= kSteps; ++i) {
        const float d = kDbFloor + (-kDbFloor) * static_cast<float>(i) / static_cast<float>(kSteps);
        const float out = d + gainReductionDb(d, thrDb, ratio, kneeDb) + makeupDb;
        pts[i] = {px(d), clampY(py(out))};
    }
    renderer.drawPolyline(pts, kSteps + 1, 2.0f, bypassed ? c.inkOff : c.secondary);

    const float inDb = linearToDb(m_inputDisplay);
    if (inDb > kDbFloor) {
        const float gr = bypassed ? 0.0f : gainReductionDb(inDb, thrDb, ratio, kneeDb);
        const float mx = clampX(px(std::min(inDb, 0.0f))), my = clampY(py(inDb + gr + makeupDb));
        renderer.fillRect(NUIRect(mx - 3.0f, my - 3.0f, 6.0f, 6.0f), bypassed ? c.inkOff : c.meter);
    }

    EditorKit::drawTextLeft(renderer, "-60", w.x + 4.0f, w.bottom() - 8.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextRight(renderer, "0 dB in", w.right() - 4.0f, w.bottom() - 8.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextLeft(renderer, "0 dB out", w.x + 4.0f, w.y + 9.0f, 9.0f, c.inkMuted);
    EditorKit::drawTextLeft(renderer, dbLabel(thrDb), std::min(px(thrDb) + 4.0f, w.right() - 40.0f), w.y + 20.0f, 9.0f,
                            c.inkMuted);
}

void AestraCompEditor::drawMeters(NUIRenderer& renderer) {
    const auto c = EditorKit::emberColors();
    const bool bypassed = isBypassed();
    const float inDb = linearToDb(m_inputDisplay);
    const float outDb = linearToDb(m_outputDisplay);
    const float grDb = -m_grDisplayDb;

    EditorKit::drawVerticalMeter(renderer, m_meterRects[0], inDb, EditorKit::MeterKind::Level, bypassed);
    EditorKit::drawVerticalMeter(renderer, m_meterRects[1], grDb, EditorKit::MeterKind::GainReduction, bypassed);
    EditorKit::drawVerticalMeter(renderer, m_meterRects[2], outDb, EditorKit::MeterKind::Level, bypassed);

    const char* names[3] = {"In", "GR", "Out"};
    const float values[3] = {std::max(inDb, -99.9f), grDb, std::max(outDb, -99.9f)};
    for (int i = 0; i < 3; ++i) {
        const NUIRect& m = m_meterRects[i];
        const float cx = m.x + m.width * 0.5f;
        const std::string label = names[i];
        const float tw = renderer.measureText(label, 9.0f).width + 8.0f;
        renderer.drawTextCentered(label, NUIRect(cx - tw * 0.5f - 6.0f, m.y - 19.0f, tw + 12.0f, 20.0f), 9.0f, c.inkMuted);
        renderer.drawTextCentered(oneDecimal(values[i]), NUIRect(cx - 14.0f, m.bottom() + 4.0f, 28.0f, 20.0f), 10.0f,
                                  c.ink);
    }
    EditorKit::drawTextRight(renderer, "dB", m_meterRects[1].x + m_meterRects[1].width * 0.5f + 8.0f,
                             m_meterRects[1].bottom() + 28.0f, 9.0f, c.inkMuted);

    // Scale to the left of the In meter.
    const int ticks[4] = {0, -12, -24, -48};
    for (int t : ticks) {
        const float y = m_meterRects[0].bottom() - 1.0f - (m_meterRects[0].height - 2.0f) * (static_cast<float>(t) + 48.0f) / 48.0f;
        EditorKit::drawTextRight(renderer, std::to_string(t), m_meterRects[0].x - 6.0f, y, 9.0f, c.inkMuted);
    }
}

int AestraCompEditor::segmentAt(const NUIRect& rect, int count, const NUIPoint& p) const {
    if (!rect.contains(p) || count <= 0) return -1;
    return std::clamp(static_cast<int>((p.x - rect.x) / (rect.width / static_cast<float>(count))), 0, count - 1);
}

float AestraCompEditor::hpfFromX(float x) const {
    const auto g = EditorKit::emberSliderGeometry(m_hpfRect, 60.0f);
    return std::clamp((x - g.x0) / (g.x1 - g.x0), 0.0f, 1.0f);
}

void AestraCompEditor::drawRightColumn(NUIRenderer& renderer) {
    const auto c = EditorKit::emberColors();
    const bool bypassed = isBypassed();
    const float x = m_origin.x + kColumnX, y = m_origin.y;

    EditorKit::drawSectionHeader(renderer, x, y + 12.0f, kColumnW, "Character");
    const std::string modes[3] = {"Clean", "Classic", "Optical"};
    const int mode = static_cast<int>(std::round(std::clamp(param(Comp::kCompMode) * 2.0f, 0.0f, 2.0f)));
    EditorKit::drawSegmented(renderer, m_modeRect, modes, 3, mode,
                             m_hasMouse ? segmentAt(m_modeRect, 3, m_mouse) : -1, bypassed);

    EditorKit::drawSectionHeader(renderer, x, y + 68.0f, kColumnW, "Detector");
    EditorKit::drawTextLeft(renderer, "Oversampling", x, m_osRect.y + kRowH * 0.5f, 11.0f, c.inkSoft);
    const std::string os[3] = {"Off", "2x", "4x"};
    const int osIdx = static_cast<int>(std::round(std::clamp(param(Comp::kOversampling) * 2.0f, 0.0f, 2.0f)));
    EditorKit::drawSegmented(renderer, m_osRect, os, 3, osIdx, m_hasMouse ? segmentAt(m_osRect, 3, m_mouse) : -1,
                             bypassed);
    EditorKit::drawToggleButton(renderer, m_sidechainRect, "Sidechain", param(Comp::kSidechain) > 0.5f,
                                hovering(m_sidechainRect), bypassed);

    const auto g = EditorKit::emberSliderGeometry(m_hpfRect, 60.0f);
    const NUIRect track(g.x0 - 4.0f, m_hpfRect.y, g.x1 - g.x0 + 8.0f, m_hpfRect.height);
    const EditorKit::ControlState hs = m_hpfDragging ? EditorKit::ControlState::Drag
                                       : hovering(track) ? EditorKit::ControlState::Hover
                                                         : EditorKit::ControlState::Idle;
    EditorKit::drawEmberSlider(renderer, m_hpfRect, 60.0f, "Det HPF", param(Comp::kDetectorHPF),
                               valueText(Comp::kDetectorHPF), hs, bypassed);
}

void AestraCompEditor::drawKnobs(NUIRenderer& renderer) {
    const bool bypassed = isBypassed();
    const float cw = EditorKit::emberKnobCellWidth(EditorKit::KnobSize::Medium);
    for (const auto& control : m_controls) {
        if (!control.slider) continue;
        const EditorKit::ControlState state = control.slider->isDragging() ? EditorKit::ControlState::Drag
                                              : hovering(control.cell)     ? EditorKit::ControlState::Hover
                                                                           : EditorKit::ControlState::Idle;
        EditorKit::drawEmberKnob(renderer, {control.cell.x, control.cell.y}, EditorKit::KnobSize::Medium,
                                 static_cast<float>(control.slider->getValue()), control.label,
                                 valueText(control.paramId), state, control.bipolar, bypassed);
    }

    // Section headers span their group's knobs.
    if (m_controls.size() >= 9) {
        const float y = m_origin.y + kKnobRowY;
        auto span = [&](size_t a, size_t b, const char* label) {
            const float x0 = m_controls[a].cell.x, x1 = m_controls[b].cell.x + cw;
            EditorKit::drawSectionHeader(renderer, x0, y, x1 - x0, label);
        };
        span(0, 4, "Compression");
        span(5, 8, "Gain");
    }
}

std::string AestraCompEditor::valueText(uint32_t paramId) const {
    if (!m_instance) return {};
    return m_instance->getParameterDisplay(paramId);
}

void AestraCompEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    layoutControls();
    drawTransferCurve(renderer);
    drawMeters(renderer);
    drawRightColumn(renderer);
    drawKnobs(renderer);
}

bool AestraCompEditor::isBypassed() const {
    return m_instance && m_instance->getParameter(Comp::kBypass) > 0.5f;
}

bool AestraCompEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    if (AestraPanelWindow::onMouseEvent(event)) return true;

    layoutControls();
    auto b = getBounds();
    const bool contains = b.contains(event.position);
    if (!contains && !isDraggingWindow() && !m_hpfDragging) {
        if (m_hasMouse) {
            m_hasMouse = false; // the pointer left: nothing stays lit
            repaint();
        }
        return false;
    }

    // Detector HPF is a horizontal slider the editor drives itself.
    if (m_hpfDragging && !event.pressed) {
        if (event.released) m_hpfDragging = false;
        else setParam(Comp::kDetectorHPF, hpfFromX(event.position.x));
        return true;
    }

    // Click handlers
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (const int i = segmentAt(m_modeRect, 3, event.position); i >= 0) {
            setParam(Comp::kCompMode, static_cast<float>(i) / 2.0f);
            return true;
        }
        if (const int i = segmentAt(m_osRect, 3, event.position); i >= 0) {
            setParam(Comp::kOversampling, static_cast<float>(i) / 2.0f);
            return true;
        }
        if (m_sidechainRect.contains(event.position)) {
            setParam(Comp::kSidechain, param(Comp::kSidechain) > 0.5f ? 0.0f : 1.0f);
            return true;
        }
        const auto g = EditorKit::emberSliderGeometry(m_hpfRect, 60.0f);
        const NUIRect track(g.x0 - 4.0f, m_hpfRect.y, g.x1 - g.x0 + 8.0f, m_hpfRect.height);
        if (track.contains(event.position)) {
            if (event.doubleClick) {
                setParam(Comp::kDetectorHPF, m_instance->getParameters()[Comp::kDetectorHPF].defaultValue);
            } else {
                m_hpfDragging = true;
                setParam(Comp::kDetectorHPF, hpfFromX(event.position.x));
            }
            return true;
        }
        if (m_curveRect.contains(event.position)) return true;
    }

    // Double-click reset
    if (event.pressed && event.button == NUIMouseButton::Left && event.doubleClick) {
        for (auto& c : m_controls) {
            if (c.slider && c.slider->getBounds().contains({event.position.x, event.position.y})) {
                const float def = m_instance->getParameters()[c.paramId].defaultValue;
                m_instance->setParameter(c.paramId, def);
                c.slider->setValue(def);
                setDirty(true);
                return true;
            }
        }
    }

    // Hover tracking
    if (!event.pressed && !event.released) {
        if (contains != m_hasMouse || event.position.x != m_mouse.x || event.position.y != m_mouse.y) {
            m_hasMouse = contains;
            m_mouse = event.position;
            repaint();
        }
    }

    return NUIComponent::onMouseEvent(event) || consumeInsideBounds(event);
}

} // namespace AestraUI
