// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraVerbEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace AestraUI {

namespace {
constexpr uint32_t kDecay = 0;
constexpr uint32_t kDamping = 1;
constexpr uint32_t kPredelay = 2;
constexpr uint32_t kWidth = 3;
constexpr uint32_t kMix = 4;
constexpr uint32_t kSize = 6;
constexpr uint32_t kDiffusion = 7;
constexpr uint32_t kModRate = 8;
constexpr uint32_t kModDepth = 9;
constexpr uint32_t kMode = 10;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

NUIColor verbPanelBg() { return NUIColor(0.045f, 0.046f, 0.064f, 0.992f); }
NUIColor verbCardBg() { return NUIColor(0.070f, 0.064f, 0.094f, 0.92f); }
NUIColor verbCardHot() { return NUIColor(0.105f, 0.087f, 0.140f, 0.96f); }
}

AestraVerbEditor::AestraVerbEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraVerbEditor");
    setSize(kWinW, kWinH);
    m_modes = {{
        {"Room", 0, {}, false},
        {"Hall", 1, {}, false},
        {"Plate", 2, {}, false}
    }};
    buildControls();
}

void AestraVerbEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;

    struct Meta { const char* label; uint32_t id; };
    const Meta metas[] = {
        {"Size", kSize},
        {"Decay", kDecay},
        {"Damping", kDamping},
        {"Predelay", kPredelay},
        {"Diffusion", kDiffusion},
        {"Mod Rate", kModRate},
        {"Mod Depth", kModDepth},
        {"Width", kWidth}
    };

    for (const auto& meta : metas) {
        Knob k;
        k.label = meta.label;
        k.paramId = meta.id;
        k.value = getParamValue(meta.id);
        m_knobs.push_back(k);
    }
    layoutControls();
}

float AestraVerbEditor::getParamValue(uint32_t paramId) const {
    return m_instance ? std::clamp(m_instance->getParameter(paramId), 0.0f, 1.0f) : 0.0f;
}

std::string AestraVerbEditor::formatParameterValue(uint32_t paramId) const {
    const float v = getParamValue(paramId);
    std::ostringstream out;
    switch (paramId) {
    case kSize:
        out << std::fixed << std::setprecision(2) << (0.1f + v * 1.9f) << "x";
        return out.str();
    case kDecay:
    {
        const float seconds = 0.3f + v * 9.7f;
        if (seconds < 10.0f) {
            out << std::fixed << std::setprecision(1) << seconds << "s";
        } else {
            out << static_cast<int>(std::round(seconds)) << "s";
        }
        return out.str();
    }
    case kDamping:
    case kDiffusion:
    case kWidth:
    case kMix:
        out << static_cast<int>(std::round(v * 100.0f)) << "%";
        return out.str();
    case kPredelay:
        out << static_cast<int>(std::round(v * 500.0f)) << "ms";
        return out.str();
    case kModRate:
        out << std::fixed << std::setprecision(2) << (v * 2.0f) << "x";
        return out.str();
    case kModDepth:
        out << std::fixed << std::setprecision(1) << (v * 8.0f) << "smp";
        return out.str();
    default:
        return m_instance ? m_instance->getParameterDisplay(paramId) : "0";
    }
}

void AestraVerbEditor::layoutControls() {
    auto b = getBounds();
    const float width = std::max(kWinW, b.width);
    const float height = std::max(kWinH, b.height);
    if (width != b.width || height != b.height) {
        setBounds(b.x, b.y, width, height);
        b = getBounds();
    }

    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    const float modeY = b.y + kTitleH + 12.0f;
    const float modeH = 42.0f;
    const float modeW = contentW / 3.0f;
    for (size_t i = 0; i < m_modes.size(); ++i) {
        m_modes[i].bounds = NUIRect(contentX + modeW * static_cast<float>(i) - (i > 0 ? 1.0f : 0.0f),
                                    modeY,
                                    modeW + (i > 0 ? 1.0f : 0.0f),
                                    modeH);
    }

    const float knobAreaY = modeY + modeH + 38.0f;
    const float knobCellW = contentW / 4.0f;
    const float knobCellH = 104.0f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        auto& k = m_knobs[i];
        const int row = static_cast<int>(i / 4);
        const int col = static_cast<int>(i % 4);
        const float cellX = contentX + knobCellW * static_cast<float>(col);
        const float cellY = knobAreaY + knobCellH * static_cast<float>(row);
        const bool primary = k.paramId == kSize || k.paramId == kDecay;
        const float baseDiameter = primary ? 68.0f : 58.0f;
        const float knobSize = std::min(baseDiameter + std::max(0.0f, (b.width - kWinW) * 0.04f), primary ? 78.0f : 66.0f);
        const float knobX = cellX + (knobCellW - knobSize) * 0.5f;
        k.bounds = NUIRect(cellX + 7.0f, cellY, knobCellW - 14.0f, knobCellH - 10.0f);
        k.knobRect = NUIRect(knobX, cellY + (primary ? 7.0f : 12.0f), knobSize, knobSize);
    }

    const float mixY = b.bottom() - 68.0f;
    m_mixBounds = NUIRect(contentX, mixY, contentW, 48.0f);
    m_mixTrack = NUIRect(contentX + 76.0f, mixY + 21.0f, contentW - 150.0f, 9.0f);
}

void AestraVerbEditor::drawTitleBar(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    NUIRect titleBar(b.x, b.y, b.width, kTitleH);
    renderer.fillRoundedRect({titleBar.x + 1.0f, titleBar.y + 1.0f, titleBar.width - 2.0f, titleBar.height + 18.0f},
                             kRadius - 1.0f, NUIColor(0.17f, 0.11f, 0.27f, 0.28f));
    renderer.fillRoundedRect({titleBar.x + 1.0f, titleBar.y + 1.0f, titleBar.width - 2.0f, 34.0f},
                             kRadius - 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.025f));
    renderer.fillCircle({titleBar.x + kPad + 7.0f, titleBar.y + 32.0f}, 8.0f, NUIColor(0.55f, 0.40f, 0.84f, 0.95f));
    renderer.fillCircle({titleBar.x + kPad + 7.0f, titleBar.y + 32.0f}, 18.0f, NUIColor(0.55f, 0.40f, 0.84f, 0.13f));
    renderer.drawText("Aestra Verb", {titleBar.x + kPad + 26.0f, titleBar.y + 18.0f}, 16.0f, theme.getColor("textPrimary"));
    renderer.drawText("Modulated FDN space engine", {titleBar.x + kPad + 26.0f, titleBar.y + 39.0f}, 10.0f,
                      theme.getColor("textSecondary").withAlpha(0.78f));

    NUIRect liveChip(titleBar.right() - 128.0f, titleBar.y + 23.0f, 76.0f, 25.0f);
    renderer.fillRoundedRect(liveChip, 12.0f, NUIColor(0.035f, 0.030f, 0.050f, 0.50f));
    renderer.strokeRoundedRect(liveChip, 12.0f, 1.0f, NUIColor(0.55f, 0.40f, 0.84f, 0.28f));
    renderer.drawText("STEREO FDN", {liveChip.x + 13.0f, liveChip.y + 8.0f}, 8.5f, theme.getColor("textSecondary").withAlpha(0.88f));

    float cx = titleBar.right() - 33.0f, cy = titleBar.y + 27.0f;
    renderer.fillRoundedRect({cx - 8.0f, cy - 8.0f, 30.0f, 30.0f}, 10.0f, NUIColor(1,1,1,0.045f));
    renderer.strokeRoundedRect({cx - 8.0f, cy - 8.0f, 30.0f, 30.0f}, 10.0f, 1.0f, NUIColor(1,1,1,0.060f));
    renderer.drawLine({cx+1, cy+1}, {cx+12, cy+12}, 1.6f, theme.getColor("textSecondary").withAlpha(0.86f));
    renderer.drawLine({cx+12, cy+1}, {cx+1, cy+12}, 1.6f, theme.getColor("textSecondary").withAlpha(0.86f));
}

void AestraVerbEditor::drawModeSelector(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const int activeMode = static_cast<int>(std::round(getParamValue(kMode) * 2.0f));
    if (!m_modes.empty()) {
        const auto outer = NUIRect(m_modes.front().bounds.x, m_modes.front().bounds.y,
                                  m_modes.back().bounds.right() - m_modes.front().bounds.x,
                                  m_modes.front().bounds.height);
        renderer.fillRoundedRect({outer.x - 2.0f, outer.y - 2.0f, outer.width + 4.0f, outer.height + 4.0f},
                                 10.0f, NUIColor(0.025f, 0.024f, 0.035f, 0.48f));
        renderer.strokeRoundedRect({outer.x - 2.0f, outer.y - 2.0f, outer.width + 4.0f, outer.height + 4.0f},
                                   10.0f, 1.0f, accent.withAlpha(0.20f));
    }
    for (const auto& mode : m_modes) {
        const bool active = mode.mode == activeMode;
        const auto fill = active ? accent.withAlpha(0.84f)
                                 : (mode.hovered ? accent.withAlpha(0.12f) : NUIColor::transparent());
        const auto border = active ? accent.withAlpha(0.92f) : accent.withAlpha(mode.hovered ? 0.55f : 0.32f);
        renderer.fillRoundedRect(mode.bounds, 8.0f, fill);
        if (active) {
            renderer.fillRoundedRect({mode.bounds.x + 2.0f, mode.bounds.y + 2.0f, mode.bounds.width - 4.0f, mode.bounds.height * 0.42f},
                                     7.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.08f));
        }
        renderer.strokeRoundedRect(mode.bounds, 8.0f, 1.0f, border);
        renderer.drawText(mode.label,
                          {mode.bounds.center().x - 18.0f, mode.bounds.y + 10.0f},
                          11.5f,
                          active ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.82f));
    }
}

void AestraVerbEditor::drawKnob(NUIRenderer& renderer, const Knob& k, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const bool primary = k.paramId == kSize || k.paramId == kDecay;
    renderer.fillRoundedRect(k.bounds, 10.0f,
        k.hovered || k.dragging ? verbCardHot() : verbCardBg());
    renderer.fillRoundedRect({k.bounds.x + 1.0f, k.bounds.y + 1.0f, k.bounds.width - 2.0f, k.bounds.height * 0.34f},
                             9.0f, NUIColor(1, 1, 1, primary ? 0.046f : 0.028f));
    renderer.strokeRoundedRect(k.bounds, 8.0f, 1.0f,
        k.hovered || k.dragging ? accent.withAlpha(0.54f) : NUIColor(1,1,1,0.07f));

    const float cx = k.knobRect.center().x;
    const float cy = k.knobRect.center().y;
    const float r = k.knobRect.width * 0.40f;
    renderer.fillCircle({cx, cy}, r + 12.0f, accent.withAlpha(primary ? 0.110f : 0.074f));
    renderer.fillCircle({cx, cy}, r + 4.0f, NUIColor(0.020f, 0.019f, 0.031f, 0.86f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.048f, 0.042f, 0.070f, 0.98f));
    renderer.fillCircle({cx - r * 0.22f, cy - r * 0.24f}, r * 0.34f, NUIColor(1, 1, 1, 0.035f));
    renderer.strokeCircle({cx, cy}, r, 1.2f, accent.withAlpha(0.38f));

    const float startAngle = kPi * 0.75f;
    const float endAngle = startAngle + k.value * kPi * 1.5f;
    for (int i = 0; i < 32; ++i) {
        const float a1 = startAngle + (kPi * 1.5f) * i / 32.0f;
        const float a2 = startAngle + (kPi * 1.5f) * (i + 1) / 32.0f;
        renderer.drawLine({cx + std::cos(a1)*(r+5), cy + std::sin(a1)*(r+5)},
                          {cx + std::cos(a2)*(r+5), cy + std::sin(a2)*(r+5)}, 2.0f, NUIColor(1,1,1,0.050f));
    }
    for (int i = 0; i < 32; ++i) {
        const float a1 = startAngle + (endAngle - startAngle) * i / 32.0f;
        const float a2 = startAngle + (endAngle - startAngle) * (i + 1) / 32.0f;
        renderer.drawLine({cx + std::cos(a1)*(r+5), cy + std::sin(a1)*(r+5)},
                          {cx + std::cos(a2)*(r+5), cy + std::sin(a2)*(r+5)}, primary ? 4.0f : 3.4f, accent.withAlpha(0.92f));
    }
    const float pa = startAngle + k.value * kPi * 1.5f;
    renderer.fillCircle({cx, cy}, std::max(2.0f, r * 0.16f), NUIColor(0.15f, 0.13f, 0.20f, 0.95f));
    renderer.drawLine({cx, cy}, {cx + std::cos(pa)*(r-8), cy + std::sin(pa)*(r-8)}, 2.0f, accent.withAlpha(0.75f));
    renderer.fillCircle({cx + std::cos(pa)*(r+5), cy + std::sin(pa)*(r+5)}, primary ? 4.4f : 3.8f,
                        k.dragging ? NUIColor(1,1,1,1.0f) : accent);

    const float textX = k.bounds.x + 11.0f;
    renderer.drawText(k.label, {textX, k.bounds.bottom() - 25.0f}, 10.0f, theme.getColor("textPrimary").withAlpha(primary ? 0.95f : 0.86f));
    renderer.drawText(formatParameterValue(k.paramId),
                      {textX, k.bounds.bottom() - 10.0f}, 9.0f, accent.withAlpha(0.94f));
}

void AestraVerbEditor::drawMixSlider(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = getParamValue(kMix);
    renderer.fillRoundedRect(m_mixBounds, 11.0f, NUIColor(0.060f, 0.055f, 0.080f, 0.97f));
    renderer.fillRoundedRect({m_mixBounds.x + 1.0f, m_mixBounds.y + 1.0f, m_mixBounds.width - 2.0f, m_mixBounds.height * 0.42f},
                             10.0f, NUIColor(1,1,1,0.025f));
    renderer.strokeRoundedRect(m_mixBounds, 11.0f, 1.0f, accent.withAlpha(m_draggingMix ? 0.68f : 0.30f));
    renderer.drawText("Mix", {m_mixBounds.x + 14.0f, m_mixBounds.y + 13.0f}, 11.0f, theme.getColor("textPrimary").withAlpha(0.92f));
    renderer.fillRoundedRect(m_mixTrack, 4.0f, NUIColor(0.020f, 0.020f, 0.030f, 0.82f));
    renderer.fillRoundedRect({m_mixTrack.x, m_mixTrack.y, m_mixTrack.width * mix, m_mixTrack.height}, 4.0f, accent.withAlpha(0.96f));
    const float thumbX = m_mixTrack.x + m_mixTrack.width * mix;
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 10.0f, accent.withAlpha(0.18f));
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 6.5f, theme.getColor("textPrimary"));
    renderer.drawText(formatParameterValue(kMix),
                      {m_mixBounds.right() - 44.0f, m_mixBounds.y + 13.0f}, 10.0f, accent.withAlpha(0.94f));
}

void AestraVerbEditor::drawSectionLabels(NUIRenderer& renderer) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    const float contentX = b.x + kPad;
    const float contentW = b.width - kPad * 2.0f;
    const float y = b.y + kTitleH + 12.0f + 42.0f + 14.0f;
    renderer.drawText("SPACE", {contentX + 8.0f, y}, 8.0f, theme.getColor("textSecondary").withAlpha(0.62f));
    renderer.drawText("MOTION", {contentX + contentW * 0.5f + 8.0f, y}, 8.0f, theme.getColor("textSecondary").withAlpha(0.62f));
    renderer.drawLine({contentX + 52.0f, y + 5.0f}, {contentX + contentW * 0.5f - 10.0f, y + 5.0f},
                      1.0f, NUIColor(1,1,1,0.055f));
    renderer.drawLine({contentX + contentW * 0.5f + 62.0f, y + 5.0f}, {contentX + contentW - 8.0f, y + 5.0f},
                      1.0f, NUIColor(1,1,1,0.055f));
}

void AestraVerbEditor::onRender(NUIRenderer& renderer) {
    auto b = getBounds();
    NUIColor accent(0.49f, 0.36f, 0.75f, 1.0f);
    renderer.fillRoundedRect({b.x + 8.0f, b.y + 10.0f, b.width - 16.0f, b.height - 10.0f}, kRadius,
                             NUIColor(0, 0, 0, 0.24f));
    renderer.fillRoundedRect(b, kRadius, verbPanelBg());
    renderer.fillRoundedRect({b.x + 1.0f, b.y + kTitleH - 3.0f, b.width - 2.0f, b.height - kTitleH + 2.0f}, 16.0f,
                             NUIColor(0.060f, 0.058f, 0.084f, 0.70f));
    renderer.fillRoundedRect({b.x + 1.0f, b.y + kTitleH - 3.0f, b.width - 2.0f, 86.0f}, 16.0f,
                             NUIColor(0.20f, 0.14f, 0.34f, 0.075f));
    renderer.strokeRoundedRect(b, kRadius, 1.1f, accent.withAlpha(0.36f));
    drawTitleBar(renderer);
    drawModeSelector(renderer, accent);
    drawSectionLabels(renderer);
    for (const auto& k : m_knobs) drawKnob(renderer, k, accent);
    drawMixSlider(renderer, accent);
}

int AestraVerbEditor::hitTestKnob(float x, float y) const {
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

int AestraVerbEditor::hitTestMode(float x, float y) const {
    for (size_t i = 0; i < m_modes.size(); ++i) {
        if (m_modes[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraVerbEditor::hitTestMix(float x, float y) const {
    return m_mixBounds.contains({x, y});
}

bool AestraVerbEditor::hitTestCloseButton(float x, float y) const {
    return NUIRect(getBounds().right() - 26, getBounds().y + 13, 16, 16).contains({x, y});
}

bool AestraVerbEditor::hitTestTitleBar(float x, float y) const {
    return NUIRect(getBounds().x, getBounds().y, getBounds().width - 32, kTitleH).contains({x, y});
}

void AestraVerbEditor::updateParameter(uint32_t paramId, float v) {
    if (!m_instance) return;
    m_instance->setParameter(paramId, std::clamp(v, 0.0f, 1.0f));
    for (auto& k : m_knobs) {
        if (k.paramId == paramId) {
            k.value = getParamValue(paramId);
            break;
        }
    }
    setDirty(true);
}

void AestraVerbEditor::updateKnobValue(int idx, float v) {
    if (idx < 0 || idx >= static_cast<int>(m_knobs.size()) || !m_instance) return;
    updateParameter(m_knobs[idx].paramId, v);
}

bool AestraVerbEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    auto b = getBounds();
    const bool isDraggingKnob = std::any_of(m_knobs.begin(), m_knobs.end(),
                                            [](const Knob& knob) { return knob.dragging; });
    const bool contains = b.contains(event.position);
    if (event.pressed && event.button == NUIMouseButton::Left && !contains && !m_isDraggingWindow && !isDraggingKnob && !m_draggingMix) {
        if (m_onClose) m_onClose();
        return false;
    }
    if (!contains && !m_isDraggingWindow && !isDraggingKnob && !m_draggingMix) return false;

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hitTestCloseButton(event.position.x, event.position.y)) {
            if (m_onClose) m_onClose();
            return true;
        }
        if (hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true;
            m_dragStartPos = event.position;
            m_windowStartPos = {b.x, b.y};
            return true;
        }
        const int modeIdx = hitTestMode(event.position.x, event.position.y);
        if (modeIdx >= 0) {
            updateParameter(kMode, static_cast<float>(m_modes[static_cast<size_t>(modeIdx)].mode) / 2.0f);
            return true;
        }
        if (hitTestMix(event.position.x, event.position.y)) {
            m_draggingMix = true;
            updateParameter(kMix, (event.position.x - m_mixTrack.x) / std::max(1.0f, m_mixTrack.width));
            return true;
        }
        const int kIdx = hitTestKnob(event.position.x, event.position.y);
        if (kIdx >= 0) {
            m_knobs[static_cast<size_t>(kIdx)].dragging = true;
            m_knobs[static_cast<size_t>(kIdx)].dragStartY = event.position.y;
            m_knobs[static_cast<size_t>(kIdx)].dragStartValue = m_knobs[static_cast<size_t>(kIdx)].value;
            return true;
        }
    }

    if (m_isDraggingWindow) {
        if (event.released && event.button == NUIMouseButton::Left) {
            m_isDraggingWindow = false;
            return true;
        }
        setBounds(m_windowStartPos.x + event.position.x - m_dragStartPos.x,
                  m_windowStartPos.y + event.position.y - m_dragStartPos.y, b.width, b.height);
        layoutControls();
        return true;
    }

    if (m_draggingMix) {
        if (event.released && event.button == NUIMouseButton::Left) {
            m_draggingMix = false;
            setDirty(true);
            return true;
        }
        updateParameter(kMix, (event.position.x - m_mixTrack.x) / std::max(1.0f, m_mixTrack.width));
        return true;
    }

    for (size_t i = 0; i < m_knobs.size(); ++i) {
        if (m_knobs[i].dragging) {
            updateKnobValue(static_cast<int>(i),
                std::clamp(m_knobs[i].dragStartValue + (m_knobs[i].dragStartY - event.position.y) / 150.0f, 0.0f, 1.0f));
            if (event.released && event.button == NUIMouseButton::Left) {
                m_knobs[i].dragging = false;
            }
            return true;
        }
    }

    if (!event.pressed && !event.released) {
        const int h = contains ? hitTestKnob(event.position.x, event.position.y) : -1;
        const int mh = contains ? hitTestMode(event.position.x, event.position.y) : -1;
        if (h != m_hoveredKnob || mh != m_hoveredMode) {
            m_hoveredKnob = h;
            m_hoveredMode = mh;
            for (size_t i = 0; i < m_knobs.size(); ++i) m_knobs[i].hovered = (static_cast<int>(i) == h);
            for (size_t i = 0; i < m_modes.size(); ++i) m_modes[i].hovered = (static_cast<int>(i) == mh);
            setDirty(true);
        }
    }
    return contains;
}

} // namespace AestraUI
