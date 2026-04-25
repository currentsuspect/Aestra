// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraCompEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraComp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;
NUIColor compAccent() { return NUIColor(1.0f, 0.62f, 0.20f, 1.0f); }
NUIColor compPurple() { return NUIColor(0.48f, 0.34f, 0.78f, 1.0f); }
NUIColor panelBg() { return NUIColor(0.052f, 0.050f, 0.070f, 0.992f); }
NUIColor cardBg() { return NUIColor(0.090f, 0.080f, 0.110f, 0.94f); }

float linearToDbNorm(float linear, float minDb = -60.0f, float maxDb = 6.0f) {
    const float db = linear > 1.0e-8f ? 20.0f * std::log10(linear) : minDb;
    return std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f);
}
}

AestraCompEditor::AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraCompEditor");
    setSize(kWinW, kWinH);
    buildControls();
}

void AestraCompEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;

    struct Meta { const char* label; uint32_t id; } meta[] = {
        {"Threshold", Aestra::Audio::Plugins::AestraComp::kThreshold},
        {"Ratio", Aestra::Audio::Plugins::AestraComp::kRatio},
        {"Attack", Aestra::Audio::Plugins::AestraComp::kAttack},
        {"Release", Aestra::Audio::Plugins::AestraComp::kRelease},
        {"Makeup", Aestra::Audio::Plugins::AestraComp::kMakeup},
        {"Knee", Aestra::Audio::Plugins::AestraComp::kKnee},
        {"Mix", Aestra::Audio::Plugins::AestraComp::kMix},
    };

    for (const auto& item : meta) {
        Knob k;
        k.label = item.label;
        k.paramId = item.id;
        k.value = m_instance->getParameter(item.id);
        m_knobs.push_back(k);
    }
    layoutControls();
}

void AestraCompEditor::layoutControls() {
    auto b = getBounds();
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    const float modeY = b.y + kTitleH + 16.0f;
    const float modeH = 36.0f;
    m_peakModeRect = {contentX, modeY, contentW * 0.5f, modeH};
    m_rmsModeRect = {contentX + contentW * 0.5f, modeY, contentW * 0.5f, modeH};

    const float gridY = modeY + modeH + 20.0f;
    const float cellGap = 12.0f;
    const float cellW = (contentW - cellGap * 3.0f) / 4.0f;
    const float cellH = 96.0f;

    for (size_t i = 0; i < m_knobs.size(); ++i) {
        const int row = static_cast<int>(i / 4);
        const int col = static_cast<int>(i % 4);
        const float x = contentX + static_cast<float>(col) * (cellW + cellGap);
        const float y = gridY + static_cast<float>(row) * (cellH + 14.0f);
        m_knobs[i].bounds = {x, y, cellW, cellH};
        m_knobs[i].knobRect = {x + (cellW - kKnobSize) * 0.5f, y + 18.0f, kKnobSize, kKnobSize};
    }

    const float meterX = contentX + 3.0f * (cellW + cellGap);
    const float meterY = gridY + cellH + 14.0f;
    m_grMeterRect = {meterX, meterY, cellW, cellH};
}

void AestraCompEditor::drawTitleBar(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor accent = compAccent();

    renderer.fillRoundedRect({b.x, b.y, b.width, kTitleH + 10.0f}, kRadius, NUIColor(0.12f, 0.09f, 0.08f, 0.92f));
    renderer.fillCircle({b.x + kPad + 10.0f, b.y + 24.0f}, 8.0f, accent.withAlpha(0.45f));
    renderer.fillCircle({b.x + kPad + 10.0f, b.y + 24.0f}, 4.0f, accent);
    renderer.drawText("Aestra Comp", {b.x + kPad + 28.0f, b.y + 14.0f}, 14.0f, theme.getColor("textPrimary"));
    renderer.drawText("Peak/RMS dynamics processor", {b.x + kPad + 28.0f, b.y + 31.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.72f));

    NUIRect chip(b.right() - 96.0f, b.y + 16.0f, 46.0f, 20.0f);
    renderer.fillRoundedRect(chip, 10.0f, NUIColor(0.10f, 0.08f, 0.06f, 0.8f));
    renderer.strokeRoundedRect(chip, 10.0f, 1.0f, accent.withAlpha(0.36f));
    renderer.drawText("DYN", {chip.x + 12.0f, chip.y + 5.0f}, 8.0f, accent.withAlpha(0.9f));

    const NUIRect closeRect(b.right() - 36.0f, b.y + 15.0f, 22.0f, 22.0f);
    renderer.fillRoundedRect(closeRect, 11.0f, NUIColor(1, 1, 1, 0.08f));
    renderer.drawLine({closeRect.x + 7.0f, closeRect.y + 7.0f}, {closeRect.x + 15.0f, closeRect.y + 15.0f}, 1.6f,
                      theme.getColor("textPrimary").withAlpha(0.72f));
    renderer.drawLine({closeRect.x + 15.0f, closeRect.y + 7.0f}, {closeRect.x + 7.0f, closeRect.y + 15.0f}, 1.6f,
                      theme.getColor("textPrimary").withAlpha(0.72f));
}

void AestraCompEditor::drawModeSwitcher(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const float mode = m_instance ? m_instance->getParameter(Aestra::Audio::Plugins::AestraComp::kDetectorMode) : 0.0f;
    const bool rms = mode > 0.5f;
    const NUIRect outer(m_peakModeRect.x, m_peakModeRect.y, m_peakModeRect.width + m_rmsModeRect.width, m_peakModeRect.height);
    renderer.fillRoundedRect(outer, 10.0f, NUIColor(0.034f, 0.032f, 0.044f, 0.96f));
    renderer.strokeRoundedRect(outer, 10.0f, 1.0f, compPurple().withAlpha(0.45f));

    auto drawSegment = [&](const NUIRect& rect, const char* label, bool active) {
        renderer.fillRoundedRect(rect, 9.0f, active ? compPurple().withAlpha(0.96f) : NUIColor(0, 0, 0, 0));
        renderer.drawText(label, {rect.center().x - 14.0f, rect.y + 11.0f}, 10.0f,
                          active ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.75f));
    };
    drawSegment(m_peakModeRect, "Peak", !rms);
    drawSegment(m_rmsModeRect, "RMS", rms);
}

std::string AestraCompEditor::formattedValue(uint32_t paramId) const {
    if (!m_instance) return {};
    const float v = m_instance->getParameter(paramId);
    char buf[32]{};
    using Comp = Aestra::Audio::Plugins::AestraComp;
    switch (paramId) {
    case Comp::kThreshold:
        std::snprintf(buf, sizeof(buf), "%ddB", static_cast<int>(std::round(-60.0f + v * 60.0f)));
        break;
    case Comp::kRatio:
        std::snprintf(buf, sizeof(buf), "%.1f:1", 1.0f + v * 19.0f);
        break;
    case Comp::kAttack:
        std::snprintf(buf, sizeof(buf), "%.1fms", 0.1f + v * 99.9f);
        break;
    case Comp::kRelease:
        std::snprintf(buf, sizeof(buf), "%.1fms", 10.0f + v * 990.0f);
        break;
    case Comp::kMakeup:
        std::snprintf(buf, sizeof(buf), "%.1fdB", v * 24.0f);
        break;
    case Comp::kKnee:
        std::snprintf(buf, sizeof(buf), "%.1fdB", v * 24.0f);
        break;
    case Comp::kMix:
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(v * 100.0f)));
        break;
    default:
        return m_instance->getParameterDisplay(paramId);
    }
    return buf;
}

void AestraCompEditor::drawKnob(NUIRenderer& renderer, const Knob& k) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor accent = compAccent();
    const bool hot = k.hovered || k.dragging;

    renderer.fillRoundedRect(k.bounds, 12.0f, hot ? NUIColor(0.135f, 0.105f, 0.095f, 0.98f) : cardBg());
    renderer.strokeRoundedRect(k.bounds, 12.0f, 1.0f, hot ? accent.withAlpha(0.48f) : NUIColor(1, 1, 1, 0.07f));

    const float cx = k.knobRect.center().x;
    const float cy = k.knobRect.center().y;
    const float r = kKnobSize * 0.38f;
    renderer.fillCircle({cx, cy}, r + 7.0f, accent.withAlpha(hot ? 0.16f : 0.08f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.035f, 0.032f, 0.040f, 0.98f));
    renderer.strokeCircle({cx, cy}, r + 4.0f, 2.0f, NUIColor(1, 1, 1, 0.08f));

    const float startAngle = kPi * 0.75f;
    const float endAngle = startAngle + k.value * kPi * 1.5f;
    for (int i = 0; i < 34; ++i) {
        const float a1 = startAngle + (endAngle - startAngle) * static_cast<float>(i) / 34.0f;
        const float a2 = startAngle + (endAngle - startAngle) * static_cast<float>(i + 1) / 34.0f;
        renderer.drawLine({cx + std::cos(a1) * (r + 5.0f), cy + std::sin(a1) * (r + 5.0f)},
                          {cx + std::cos(a2) * (r + 5.0f), cy + std::sin(a2) * (r + 5.0f)},
                          3.0f, accent.withAlpha(0.88f));
    }

    const float ptrAngle = startAngle + k.value * kPi * 1.5f;
    renderer.drawLine({cx, cy}, {cx + std::cos(ptrAngle) * (r - 6.0f), cy + std::sin(ptrAngle) * (r - 6.0f)},
                      2.0f, theme.getColor("textPrimary").withAlpha(0.85f));
    renderer.fillCircle({cx + std::cos(ptrAngle) * (r - 1.0f), cy + std::sin(ptrAngle) * (r - 1.0f)}, 3.0f, accent);

    renderer.drawText(k.label, {k.bounds.x + 10.0f, k.bounds.y + 7.0f}, 9.0f, theme.getColor("textPrimary").withAlpha(0.82f));
    renderer.drawText(formattedValue(k.paramId), {k.bounds.x + 10.0f, k.bounds.bottom() - 16.0f}, 9.0f, accent.withAlpha(0.92f));
}

void AestraCompEditor::drawGainReductionMeter(NUIRenderer& renderer, const NUIRect& bounds) {
    auto& theme = NUIThemeManager::getInstance();
    float grDb = 0.0f;
    if (auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(m_instance)) {
        grDb = comp->getCurrentGainReductionDb();
    }
    const float norm = std::clamp(grDb / 24.0f, 0.0f, 1.0f);
    renderer.fillRoundedRect(bounds, 12.0f, cardBg());
    renderer.strokeRoundedRect(bounds, 12.0f, 1.0f, NUIColor(1, 1, 1, 0.07f));
    renderer.drawText("GR", {bounds.x + 10.0f, bounds.y + 7.0f}, 9.0f, theme.getColor("textPrimary").withAlpha(0.82f));

    const NUIRect well(bounds.x + bounds.width * 0.5f - 10.0f, bounds.y + 24.0f, 20.0f, bounds.height - 44.0f);
    renderer.fillRoundedRect(well, 8.0f, NUIColor(0.028f, 0.025f, 0.032f, 0.95f));
    const float fillH = well.height * norm;
    renderer.fillRoundedRect({well.x, well.bottom() - fillH, well.width, fillH}, 8.0f, compAccent().withAlpha(0.88f));

    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%.1fdB", grDb);
    renderer.drawText(buf, {bounds.x + 10.0f, bounds.bottom() - 16.0f}, 9.0f, compAccent().withAlpha(0.92f));
}

void AestraCompEditor::drawHorizontalMeter(NUIRenderer& renderer, const NUIRect& bounds, const std::string& label, float level) {
    auto& theme = NUIThemeManager::getInstance();
    const float norm = linearToDbNorm(level);
    renderer.fillRoundedRect(bounds, 8.0f, NUIColor(0.032f, 0.030f, 0.040f, 0.96f));
    renderer.fillRoundedRect({bounds.x, bounds.y, bounds.width * norm, bounds.height}, 8.0f,
                             (label == "IN" ? compAccent() : compPurple()).withAlpha(0.78f));
    renderer.strokeRoundedRect(bounds, 8.0f, 1.0f, NUIColor(1, 1, 1, 0.07f));
    renderer.drawText(label, {bounds.x + 8.0f, bounds.y + 4.0f}, 8.0f, theme.getColor("textPrimary").withAlpha(0.82f));
}

void AestraCompEditor::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    renderer.fillRoundedRect(b, kRadius, panelBg());
    renderer.strokeRoundedRect(b, kRadius, 1.0f, compAccent().withAlpha(0.20f));
    drawTitleBar(renderer);
    drawModeSwitcher(renderer);
    for (const auto& k : m_knobs) drawKnob(renderer, k);
    drawGainReductionMeter(renderer, m_grMeterRect);

    float inLevel = 0.0f;
    float outLevel = 0.0f;
    if (auto comp = std::dynamic_pointer_cast<Aestra::Audio::Plugins::AestraComp>(m_instance)) {
        inLevel = comp->getInputLevel();
        outLevel = comp->getOutputLevel();
    }
    const float meterY = b.bottom() - 36.0f;
    const float meterW = (b.width - kPad * 2.0f - 12.0f) * 0.5f;
    drawHorizontalMeter(renderer, {b.x + kPad, meterY, meterW, 16.0f}, "IN", inLevel);
    drawHorizontalMeter(renderer, {b.x + kPad + meterW + 12.0f, meterY, meterW, 16.0f}, "OUT", outLevel);
}

int AestraCompEditor::hitTestKnob(float x, float y) const {
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraCompEditor::hitTestCloseButton(float x, float y) const {
    auto b = getBounds();
    return NUIRect(b.right() - 36.0f, b.y + 15.0f, 22.0f, 22.0f).contains({x, y});
}

bool AestraCompEditor::hitTestTitleBar(float x, float y) const {
    auto b = getBounds();
    return NUIRect(b.x, b.y, b.width - 42.0f, kTitleH).contains({x, y});
}

void AestraCompEditor::updateKnobValue(int idx, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_knobs.size()) || !m_instance) return;
    v = std::clamp(v, 0.0f, 1.0f);
    m_knobs[idx].value = v;
    m_instance->setParameter(m_knobs[idx].paramId, v);
    setDirty(true);
}

bool AestraCompEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    auto b = getBounds();
    const bool isDraggingKnob = std::any_of(m_knobs.begin(), m_knobs.end(),
                                            [](const Knob& knob) { return knob.dragging; });
    const bool contains = b.contains(event.position);
    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !isDraggingKnob) {
        if (m_onClose) m_onClose();
        return false;
    }
    if (!contains && !m_isDraggingWindow && !isDraggingKnob) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hitTestCloseButton(event.position.x, event.position.y)) {
            if (m_onClose) m_onClose();
            return true;
        }
        if (m_peakModeRect.contains(event.position) && m_instance) {
            m_instance->setParameter(Aestra::Audio::Plugins::AestraComp::kDetectorMode, 0.0f);
            setDirty(true);
            return true;
        }
        if (m_rmsModeRect.contains(event.position) && m_instance) {
            m_instance->setParameter(Aestra::Audio::Plugins::AestraComp::kDetectorMode, 1.0f);
            setDirty(true);
            return true;
        }
        if (hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true;
            m_dragStartPos = event.position;
            m_windowStartPos = {b.x, b.y};
            return true;
        }
        const int kIdx = hitTestKnob(event.position.x, event.position.y);
        if (kIdx >= 0) {
            m_knobs[kIdx].dragging = true;
            m_knobs[kIdx].dragStartY = event.position.y;
            m_knobs[kIdx].dragStartValue = m_knobs[kIdx].value;
            return true;
        }
    }

    if (m_isDraggingWindow) {
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_isDraggingWindow = false;
            return true;
        }
        const float dx = event.position.x - m_dragStartPos.x;
        const float dy = event.position.y - m_dragStartPos.y;
        setBounds(m_windowStartPos.x + dx, m_windowStartPos.y + dy, b.width, b.height);
        layoutControls();
        return true;
    }

    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].dragging) {
            const float delta = (m_knobs[i].dragStartY - event.position.y) / 150.0f;
            updateKnobValue(static_cast<int>(i), std::clamp(m_knobs[i].dragStartValue + delta, 0.0f, 1.0f));
            if (!event.pressed && event.button == NUIMouseButton::Left) m_knobs[i].dragging = false;
            return true;
        }
    }

    if (!event.pressed && !event.released) {
        const int h = contains ? hitTestKnob(event.position.x, event.position.y) : -1;
        if (h != m_hoveredKnob) {
            m_hoveredKnob = h;
            for (size_t i = 0; i < m_knobs.size(); ++i) m_knobs[i].hovered = (static_cast<int>(i) == h);
            setDirty(true);
        }
    }
    return contains;
}

} // namespace AestraUI
