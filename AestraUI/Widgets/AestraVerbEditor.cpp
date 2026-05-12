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

NUIColor verbSurfaceBg() { return NUIColor(0.038f, 0.049f, 0.060f, 0.985f); }
NUIColor verbInsetBg() { return NUIColor(0.018f, 0.024f, 0.031f, 0.965f); }
NUIColor verbGold() { return NUIColor(0.94f, 0.66f, 0.34f, 1.0f); }
NUIColor verbAccent() { return NUIColor(0.62f, 0.38f, 0.94f, 1.0f); }
float presetColumnWidth(float editorWidth) { return std::clamp(editorWidth * 0.22f, 138.0f, 168.0f); }
float editorContentX(const NUIRect& b) { return b.x + 18.0f + presetColumnWidth(b.width) + 18.0f; }

void drawVerbArc(NUIRenderer& renderer,
                 NUIPoint center,
                 float radius,
                 float startAngle,
                 float endAngle,
                 float thickness,
                 NUIColor color) {
    if (endAngle < startAngle) std::swap(startAngle, endAngle);
    if (endAngle - startAngle <= 0.001f) return;

    std::array<NUIPoint, 49> points{};
    const float pointsDivisor = points.size() > 1 ? static_cast<float>(points.size() - 1) : 1.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const float t = static_cast<float>(i) / pointsDivisor;
        const float angle = startAngle + (endAngle - startAngle) * t;
        points[i] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    renderer.drawPolyline(points.data(), static_cast<int>(points.size()), thickness, color);
}

float protectedLeftDockEdge(const NUIComponent* root, const NUIComponent* self, const NUIRect& parentBounds) {
    if (!root || parentBounds.width <= 0.0f || parentBounds.height <= 0.0f) return parentBounds.x;

    float edge = parentBounds.x;
    const auto scan = [&](const auto& recurse, const NUIComponent* node) -> void {
        if (!node || node == self || !node->isVisible()) return;
        const auto b = node->getBounds();
        const bool leftDocked = b.x <= parentBounds.x + 8.0f;
        const bool railSized = b.width >= 96.0f && b.width <= std::min(620.0f, parentBounds.width * 0.46f);
        const bool verticallySignificant = b.height >= parentBounds.height * 0.40f && b.bottom() >= parentBounds.y + 120.0f;
        if (leftDocked && railSized && verticallySignificant) {
            edge = std::max(edge, b.right());
        }
        for (const auto& child : node->getChildren()) {
            recurse(recurse, child.get());
        }
    };

    scan(scan, root);
    return edge;
}
}

AestraVerbEditor::AestraVerbEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraVerbEditor");
    setPanelTitle("Aestra Verb");
    setBadgeText("Reverb");
    setSize(kWinW, kWinH);
    m_modes = {{
        {"Room", 0, {}, false},
        {"Hall", 1, {}, false},
        {"Plate", 2, {}, false}
    }};
    m_presets = {{
        {"Vitruvian Space", 0, 0.35f, 0.28f, 0.54f, 0.58f, 0.24f, 0.09f, 0.52f, 0.30f,
         "AestraAssets/plugins/AestraVerb/presets/vitruvian-space.png", 0, false, {}, false},
        {"Grand Hall", 1, 0.76f, 0.58f, 0.51f, 0.86f, 0.42f, 0.18f, 0.74f, 0.36f,
         "AestraAssets/plugins/AestraVerb/presets/grand-hall.png", 0, false, {}, false},
        {"Plate Forge", 2, 0.54f, 0.48f, 0.46f, 0.82f, 0.34f, 0.20f, 0.78f, 0.32f,
         "AestraAssets/plugins/AestraVerb/presets/plate-forge.png", 0, false, {}, false},
        {"Celestial Room", 1, 0.92f, 0.76f, 0.42f, 0.94f, 0.28f, 0.30f, 0.88f, 0.40f,
         "AestraAssets/plugins/AestraVerb/presets/celestial-room.png", 0, false, {}, false}
    }};
    buildControls();
}

void AestraVerbEditor::buildControls() {
    m_knobs.clear();
    if (!m_instance) return;

    struct Meta { const char* label; uint32_t id; };
    const Meta metas[] = {
        {"Predelay", kPredelay},
        {"Size", kSize},
        {"Decay", kDecay},
        {"Damping", kDamping},
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
    if (m_layouting) return;
    m_layouting = true;

    auto b = getBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) {
        setBounds(b.x, b.y, kWinW, kWinH);
        b = getBounds();
    }
    if (!isDraggingWindow()) {
        enforceBoundsInParent(!m_userPositioned);
    }
    b = getBounds();

    const float presetX = b.x + kPad;
    const float presetW = presetColumnWidth(b.width);
    const float presetY = b.y + 88.0f;
    const float presetH = 70.0f;
    const float presetGap = 8.0f;
    for (size_t i = 0; i < m_presets.size(); ++i) {
        m_presets[i].bounds = NUIRect(presetX, presetY + static_cast<float>(i) * (presetH + presetGap), presetW, presetH);
    }

    const float contentX = editorContentX(b);
    const float contentW = b.width - (contentX - b.x) - kPad;
    const float modeY = b.y + 68.0f;
    const float modeH = 34.0f;
    const float modeW = contentW / 3.0f;
    for (size_t i = 0; i < m_modes.size(); ++i) {
        m_modes[i].bounds = NUIRect(contentX + modeW * static_cast<float>(i) - (i > 0 ? 1.0f : 0.0f),
                                    modeY,
                                    modeW + (i > 0 ? 1.0f : 0.0f),
                                    modeH);
    }

    const float mainY = b.y + 114.0f;
    const float stackW = std::clamp(contentW * 0.25f, 108.0f, 130.0f);
    const float rightW = std::clamp(contentW * 0.28f, 126.0f, 146.0f);
    const float smallKnob = 46.0f;
    const float centerW = std::max(160.0f, contentW - stackW - rightW - 34.0f);
    const float centerX = contentX + stackW + 17.0f;
    const float rightX = centerX + centerW + 17.0f;

    for (auto& k : m_knobs) {
        if (k.paramId == kDecay) {
            const float macroSize = std::min(174.0f, centerW - 10.0f);
            k.bounds = NUIRect(centerX, mainY + 8.0f, centerW, 198.0f);
            k.knobRect = NUIRect(centerX + (centerW - macroSize) * 0.5f, mainY + 18.0f, macroSize, macroSize);
            continue;
        }

        float x = contentX;
        float y = mainY;
        float w = stackW;
        switch (k.paramId) {
        case kPredelay: y = mainY + 88.0f; break;
        case kSize: y = mainY + 158.0f; break;
        case kDamping: x = rightX; y = mainY + 0.0f; w = rightW; break;
        case kDiffusion: x = rightX; y = mainY + 58.0f; w = rightW; break;
        case kModRate: x = rightX; y = mainY + 116.0f; w = rightW; break;
        case kModDepth: x = rightX; y = mainY + 174.0f; w = rightW; break;
        case kWidth: x = rightX; y = mainY + 232.0f; w = rightW; break;
        default: break;
        }
        if (x == rightX) {
            switch (k.paramId) {
            case kDamping: y = mainY + 0.0f; break;
            case kDiffusion: y = mainY + 52.0f; break;
            case kModRate: y = mainY + 104.0f; break;
            case kModDepth: y = mainY + 156.0f; break;
            case kWidth: y = mainY + 208.0f; break;
            default: break;
            }
        }
        k.bounds = NUIRect(x, y, w, 50.0f);
        k.knobRect = NUIRect(x + 6.0f, y + 2.0f, smallKnob, smallKnob);
    }

    m_mixBounds = NUIRect(centerX + 10.0f, mainY + 214.0f, centerW - 20.0f, 30.0f);
    m_mixTrack = NUIRect(m_mixBounds.x + 34.0f, m_mixBounds.y + 14.0f, m_mixBounds.width - 70.0f, 4.0f);
    m_layouting = false;
}

void AestraVerbEditor::onResize(int width, int height) {
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraVerbEditor::enforceBoundsInParent(bool recenterWhenPossible) {
    auto* parent = getParent();
    if (!parent) return;

    const NUIRect parentBounds = parent->getBounds();
    if (parentBounds.width <= 1.0f || parentBounds.height <= 1.0f) return;

    constexpr float kSafeMargin = 14.0f;
    constexpr float kChromeReserve = 56.0f;
    constexpr float kMinSafeWidth = 640.0f;
    constexpr float kMinSafeHeight = 500.0f;

    const float topReserve = parentBounds.y <= 1.0f && parentBounds.height > kMinSafeHeight + kChromeReserve + kSafeMargin * 2.0f
        ? kChromeReserve
        : 0.0f;

    NUIRect usable(parentBounds.x + kSafeMargin,
                   parentBounds.y + topReserve + kSafeMargin,
                   std::max(1.0f, parentBounds.width - kSafeMargin * 2.0f),
                   std::max(1.0f, parentBounds.height - topReserve - kSafeMargin * 2.0f));

    const NUIComponent* root = parent;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    const float dockEdge = protectedLeftDockEdge(root, this, parentBounds);
    if (dockEdge > usable.x) {
        const float shift = std::min(usable.width - 1.0f, dockEdge + kSafeMargin - usable.x);
        usable.x += shift;
        usable.width = std::max(1.0f, usable.width - shift);
    }

    auto current = getBounds();
    float targetW = std::min(kWinW, usable.width);
    float targetH = std::min(kWinH, usable.height);
    if (usable.width >= kMinSafeWidth) targetW = std::max(kMinSafeWidth, targetW);
    if (usable.height >= kMinSafeHeight) targetH = std::max(kMinSafeHeight, targetH);

    const bool canFullyCenter = usable.width >= targetW && usable.height >= targetH;
    float targetX = current.x;
    float targetY = current.y;
    if (recenterWhenPossible && canFullyCenter) {
        targetX = usable.x + (usable.width - targetW) * 0.5f;
        targetY = usable.y + (usable.height - targetH) * 0.5f;
    } else {
        targetX = std::clamp(current.x, usable.x, std::max(usable.x, usable.right() - targetW));
        targetY = std::clamp(current.y, usable.y, std::max(usable.y, usable.bottom() - targetH));
    }

    if (std::abs(current.x - targetX) > 0.5f || std::abs(current.y - targetY) > 0.5f ||
        std::abs(current.width - targetW) > 0.5f || std::abs(current.height - targetH) > 0.5f) {
        setBounds(std::round(targetX), std::round(targetY), std::round(targetW), std::round(targetH));
    }
}

void AestraVerbEditor::drawPresetStrip(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    renderer.fillRoundedRect({m_presets.front().bounds.x - 5.0f, m_presets.front().bounds.y - 34.0f,
                              m_presets.front().bounds.width + 10.0f, 350.0f},
                             8.0f, verbSurfaceBg().withAlpha(0.94f));
    renderer.strokeRoundedRect({m_presets.front().bounds.x - 5.0f, m_presets.front().bounds.y - 34.0f,
                                m_presets.front().bounds.width + 10.0f, 350.0f},
                               8.0f, 1.0f, NUIColor(1, 1, 1, 0.085f));
    renderer.drawText("PRESETS", {m_presets.front().bounds.x + 10.0f, m_presets.front().bounds.y - 20.0f}, 10.0f,
                      accent.withAlpha(0.76f));

    int activePreset = 0;
    float bestDistance = 1000.0f;
    for (size_t i = 0; i < m_presets.size(); ++i) {
        auto& p = m_presets[i];
        const float distance = std::abs(getParamValue(kMode) - static_cast<float>(p.mode) / 2.0f) * 1.8f +
            std::abs(getParamValue(kSize) - p.size) +
            std::abs(getParamValue(kDecay) - p.decay) +
            std::abs(getParamValue(kWidth) - p.width) * 0.6f;
        if (distance < bestDistance) {
            bestDistance = distance;
            activePreset = static_cast<int>(i);
        }
    }

    for (size_t i = 0; i < m_presets.size(); ++i) {
        auto& p = m_presets[i];
        const bool active = static_cast<int>(i) == activePreset && bestDistance < 0.55f;
        const bool focused = static_cast<int>(i) == m_focusedPreset;
        const bool pressed = static_cast<int>(i) == m_pressedPreset;
        const NUIColor presetFill = active ? NUIColor(0.050f, 0.043f, 0.043f, 0.98f)
            : (pressed ? NUIColor(0.034f, 0.041f, 0.052f, 0.99f)
                       : (p.hovered ? NUIColor(0.043f, 0.053f, 0.065f, 0.98f)
                                    : verbInsetBg().withAlpha(0.92f)));
        renderer.fillRoundedRect(p.bounds, 5.0f, presetFill);
        renderer.strokeRoundedRect(p.bounds, 5.0f, 1.0f,
                                   active ? verbGold().withAlpha(0.58f)
                                          : (p.hovered ? accent.withAlpha(0.38f) : NUIColor(1, 1, 1, 0.09f)));
        if (focused) {
            renderer.strokeRoundedRect({p.bounds.x + 2.0f, p.bounds.y + 2.0f, p.bounds.width - 4.0f, p.bounds.height - 4.0f},
                                       4.0f, 1.0f, accent.withAlpha(0.28f));
        }

        const NUIRect art(p.bounds.x + 8.0f, p.bounds.y + 8.0f, 54.0f, p.bounds.height - 16.0f);
        if (!p.artworkLoadAttempted) {
            p.artworkTexture = renderer.loadTexture(p.artworkPath);
            p.artworkLoadAttempted = true;
        }
        if (p.artworkTexture != 0) {
            renderer.drawTexture(p.artworkTexture, art, NUIRect(0.0f, 0.0f, 1254.0f, 1254.0f));
            renderer.fillRoundedRect(art, 3.0f, NUIColor(0.0f, 0.0f, 0.0f, active ? 0.05f : 0.16f));
        } else {
            const NUIColor top = i == 0 ? NUIColor(0.26f, 0.16f, 0.08f, 1.0f)
                             : i == 1 ? NUIColor(0.20f, 0.17f, 0.12f, 1.0f)
                             : i == 2 ? NUIColor(0.20f, 0.20f, 0.22f, 1.0f)
                                      : NUIColor(0.18f, 0.07f, 0.26f, 1.0f);
            const NUIColor bottom = i == 0 ? NUIColor(0.05f, 0.035f, 0.025f, 1.0f)
                                : i == 1 ? NUIColor(0.035f, 0.035f, 0.040f, 1.0f)
                                : i == 2 ? NUIColor(0.035f, 0.036f, 0.040f, 1.0f)
                                         : NUIColor(0.035f, 0.018f, 0.055f, 1.0f);
            renderer.fillRectGradient(art, top, bottom, true);
        }
        renderer.strokeRoundedRect(art, 3.0f, 1.0f, NUIColor(1, 1, 1, 0.08f));
        const NUIRect nameRect(p.bounds.x + 72.0f, p.bounds.y, p.bounds.width - 80.0f, p.bounds.height);
        renderer.drawText(p.label, {nameRect.x, std::round(renderer.calculateTextY(nameRect, p.label.size() > 12 ? 10.2f : 11.3f))},
                          p.label.size() > 12 ? 10.2f : 11.3f,
                          active ? verbGold().withAlpha(0.92f) : theme.getColor("textPrimary").withAlpha(0.82f));
    }
}

void AestraVerbEditor::drawModeSelector(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const int activeMode = static_cast<int>(std::round(getParamValue(kMode) * 2.0f));
    if (!m_modes.empty()) {
        const auto outer = NUIRect(m_modes.front().bounds.x, m_modes.front().bounds.y,
                                  m_modes.back().bounds.right() - m_modes.front().bounds.x,
                                  m_modes.front().bounds.height);
        renderer.fillRoundedRect(outer, 17.0f, NUIColor(0.022f, 0.028f, 0.036f, 0.985f));
        renderer.strokeRoundedRect(outer, 17.0f, 1.0f, NUIColor(1, 1, 1, 0.14f));
        renderer.strokeRoundedRect({outer.x + 1.0f, outer.y + 1.0f, outer.width - 2.0f, outer.height - 2.0f},
                                   16.0f, 1.0f, NUIColor(1, 1, 1, 0.018f));

        const float segmentW = outer.width / static_cast<float>(m_modes.size());
        const float pad = 2.0f;
        for (const auto& mode : m_modes) {
            if (mode.mode == activeMode) continue;
            const bool pressed = mode.mode == m_pressedMode;
            if (mode.hovered || pressed) {
                const NUIRect hoverRect(outer.x + pad + static_cast<float>(mode.mode) * segmentW,
                                        outer.y + pad,
                                        segmentW - pad * 2.0f,
                                        outer.height - pad * 2.0f);
                renderer.fillRoundedRect(hoverRect, 15.0f, NUIColor(1, 1, 1, pressed ? 0.060f : 0.040f));
            }
        }

        const NUIRect indicator(outer.x + pad + m_modeIndicatorPosition * segmentW,
                                outer.y + pad,
                                segmentW - pad * 2.0f,
                                outer.height - pad * 2.0f);
        renderer.fillRoundedRect(indicator, 15.0f, accent.withAlpha(m_pressedMode == activeMode ? 0.58f : 0.52f));
        renderer.strokeRoundedRect(indicator, 15.0f, 1.0f, accent.withAlpha(m_pressedMode == activeMode ? 0.64f : 0.58f));
        renderer.fillRect({indicator.x + 7.0f, indicator.y + 1.0f, indicator.width - 14.0f, 1.0f},
                          NUIColor(1, 1, 1, 0.11f));
        renderer.fillCircle({indicator.center().x, indicator.bottom() - 4.0f}, 2.0f, NUIColor(1, 1, 1, 0.20f));
    }
    for (const auto& mode : m_modes) {
        const bool active = mode.mode == activeMode;
        const bool focused = mode.mode == m_focusedMode;
        const NUIRect segment(mode.bounds.x + 2.0f, mode.bounds.y + 2.0f, mode.bounds.width - 4.0f, mode.bounds.height - 4.0f);
        if (focused) {
            renderer.strokeRoundedRect(segment, 15.0f, 1.0f, accent.withAlpha(active ? 0.46f : 0.28f));
        }
        renderer.drawTextCentered(mode.label, mode.bounds, 11.5f,
                                  theme.getColor("textPrimary").withAlpha(active ? 0.96f : (mode.hovered ? 0.82f : 0.60f)));
    }
}

void AestraVerbEditor::drawKnob(NUIRenderer& renderer, const Knob& k, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float cx = k.knobRect.center().x;
    const float cy = k.knobRect.center().y;
    const float r = std::clamp(std::min(k.knobRect.width, k.knobRect.height) * 0.34f, 10.0f, 18.0f);
    const bool focused = m_focusedKnob >= 0 && m_focusedKnob < static_cast<int>(m_knobs.size()) &&
        m_knobs[static_cast<size_t>(m_focusedKnob)].paramId == k.paramId;
    const bool active = k.dragging;
    const bool hover = k.hovered;
    const float stateLift = active ? 1.0f : (hover ? 0.55f : 0.0f);
    const float focusAlpha = focused ? 0.28f : 0.0f;

    if (k.paramId == kDecay) {
        const float macroR = k.knobRect.width * 0.40f;
        const float startAngle = kPi * 0.75f;
        const float sweep = kPi * 1.50f;
        const float endAngle = startAngle + k.value * sweep;
        renderer.fillCircle({cx, cy}, macroR + 13.0f, NUIColor(0.006f, 0.008f, 0.011f, 0.68f));
        renderer.fillCircle({cx, cy}, macroR + 7.0f, verbInsetBg().withAlpha(0.98f));
        renderer.drawShadow(NUIRect{cx - macroR, cy - macroR, macroR * 2.0f, macroR * 2.0f}, 0.0f, 3.0f, 8.0f,
                            NUIColor(0, 0, 0, 0.46f));
        renderer.strokeCircle({cx, cy}, macroR + 17.0f, 1.0f, accent.withAlpha(0.10f + stateLift * 0.07f));
        if (focused) {
            renderer.strokeCircle({cx, cy}, macroR + 22.0f, 1.0f, accent.withAlpha(focusAlpha));
        }
        for (int tick = 0; tick < 48; ++tick) {
            const float a = kPi * 0.73f + static_cast<float>(tick) / 47.0f * kPi * 1.54f;
            const float inner = macroR + 18.0f;
            const float outer = inner + ((tick % 6 == 0) ? 4.0f : 2.0f);
            renderer.drawLine({cx + std::cos(a) * inner, cy + std::sin(a) * inner},
                              {cx + std::cos(a) * outer, cy + std::sin(a) * outer}, 1.0f,
                              (tick % 6 == 0 ? verbGold() : accent).withAlpha(0.24f + stateLift * 0.08f));
        }
        drawVerbArc(renderer, {cx, cy}, macroR + 2.0f, startAngle, startAngle + sweep, 5.0f,
                    NUIColor(1, 1, 1, 0.090f + stateLift * 0.025f));
        drawVerbArc(renderer, {cx, cy}, macroR + 2.0f, startAngle, endAngle, 5.0f,
                    accent.withAlpha(0.82f + stateLift * 0.10f));
        renderer.fillCircle({cx, cy}, macroR * 0.78f, hover ? verbSurfaceBg().withAlpha(1.0f) : NUIColor(0.036f, 0.043f, 0.052f, 1.0f));
        renderer.fillCircle({cx - macroR * 0.18f, cy - macroR * 0.22f}, macroR * 0.23f, NUIColor(1, 1, 1, 0.030f + stateLift * 0.018f));
        renderer.strokeCircle({cx, cy}, macroR * 0.78f, 1.0f, NUIColor(1, 1, 1, 0.095f + stateLift * 0.045f));
        const float pa = startAngle + k.value * sweep;
        renderer.drawLine({cx + std::cos(pa) * (macroR - 14.0f), cy + std::sin(pa) * (macroR - 14.0f)},
                          {cx + std::cos(pa) * (macroR - 3.0f), cy + std::sin(pa) * (macroR - 3.0f)}, 4.0f,
                          verbGold().withAlpha(active ? 1.0f : (hover ? 0.98f : 0.92f)));
        const float seconds = 0.3f + getParamValue(kDecay) * 9.7f;
        std::ostringstream value;
        value << std::fixed << std::setprecision(2) << seconds;
        renderer.drawTextCentered("DECAY", {k.bounds.x, k.bounds.y + 70.0f, k.bounds.width, 18.0f}, 12.0f,
                                  accent.withAlpha(0.82f + stateLift * 0.12f));
        renderer.drawTextCentered(value.str(), {k.bounds.x, k.bounds.y + 94.0f, k.bounds.width, 56.0f}, 42.0f,
                                  theme.getColor("textPrimary").withAlpha(active ? 1.0f : (hover ? 0.97f : 0.94f)));
        renderer.drawTextCentered("s", {k.bounds.x, k.bounds.y + 143.0f, k.bounds.width, 20.0f}, 16.0f,
                                  accent.withAlpha(0.80f + stateLift * 0.12f));
        return;
    }

    renderer.drawShadow(NUIRect{cx - r * 0.78f, cy - r * 0.78f, r * 1.56f, r * 1.56f}, 0.0f, 2.0f, 4.0f,
                        NUIColor(0, 0, 0, 0.48f));
    renderer.fillCircle({cx, cy}, r * 0.76f, hover ? verbSurfaceBg().withAlpha(1.0f) : NUIColor(0.034f, 0.040f, 0.049f, 1.0f));
    renderer.strokeCircle({cx, cy}, r * 0.76f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.09f + stateLift * 0.045f));
    renderer.fillCircle({cx - r * 0.19f, cy - r * 0.22f}, r * 0.25f, NUIColor(1, 1, 1, 0.026f + stateLift * 0.016f));
    if (focused) {
        renderer.strokeCircle({cx, cy}, r + 12.0f, 1.0f, accent.withAlpha(0.24f));
    }

    const float startAngle = kPi * 0.75f;
    const float sweep = kPi * 1.5f;
    const float endAngle = startAngle + k.value * sweep;
    drawVerbArc(renderer, {cx, cy}, r, startAngle, startAngle + sweep, 3.0f, NUIColor(1, 1, 1, 0.12f + stateLift * 0.025f));
    drawVerbArc(renderer, {cx, cy}, r, startAngle, endAngle, 3.0f, accent.withAlpha(0.86f + stateLift * 0.11f));
    const float pa = startAngle + k.value * kPi * 1.5f;
    renderer.drawLine({cx, cy}, {cx + std::cos(pa) * (r * 0.56f), cy + std::sin(pa) * (r * 0.56f)}, 2.0f,
                      NUIColor(1.0f, 1.0f, 1.0f, active ? 0.98f : (hover ? 0.92f : 0.84f)));
    const float textX = k.knobRect.right() + 13.0f;
    renderer.drawText(k.label, {textX, k.bounds.y + 12.0f}, 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.74f + stateLift * 0.12f));
    renderer.drawText(formatParameterValue(k.paramId), {textX, k.bounds.y + 31.0f}, 11.0f,
                      accent.withAlpha(0.82f + stateLift * 0.15f));
}

void AestraVerbEditor::drawMixSlider(NUIRenderer& renderer, NUIColor accent) {
    auto& theme = NUIThemeManager::getInstance();
    const float mix = getParamValue(kMix);
    const float stateLift = m_draggingMix ? 1.0f : (m_mixHovered ? 0.55f : 0.0f);
    if (m_mixFocused) {
        renderer.strokeRoundedRect({m_mixBounds.x - 6.0f, m_mixBounds.y + 2.0f, m_mixBounds.width + 12.0f, m_mixBounds.height - 4.0f},
                                   7.0f, 1.0f, accent.withAlpha(0.24f));
    }
    renderer.drawText("MIX", {m_mixBounds.x, m_mixBounds.y + 8.0f}, 9.5f,
                      theme.getColor("textPrimary").withAlpha(0.70f + stateLift * 0.12f));
    renderer.fillRoundedRect(m_mixTrack, 2.0f, NUIColor(1, 1, 1, 0.13f + stateLift * 0.035f));
    renderer.fillRoundedRect({m_mixTrack.x, m_mixTrack.y, m_mixTrack.width * mix, m_mixTrack.height}, 2.0f,
                             accent.withAlpha(0.82f + stateLift * 0.10f));
    const float thumbX = m_mixTrack.x + m_mixTrack.width * mix;
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 7.5f, accent.withAlpha(0.20f + stateLift * 0.10f));
    renderer.fillCircle({thumbX, m_mixTrack.center().y}, 6.0f,
                        theme.getColor("textPrimary").withAlpha(0.92f + stateLift * 0.06f));
    renderer.drawText(formatParameterValue(kMix), {m_mixBounds.right() - 28.0f, m_mixBounds.y + 8.0f}, 9.5f,
                      accent.withAlpha(0.80f + stateLift * 0.14f));
}

void AestraVerbEditor::drawAnalysisPanels(NUIRenderer& renderer, NUIColor accent) {
    auto b = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    const float contentX = editorContentX(b);
    const float contentW = b.width - (contentX - b.x) - kPad;
    const float y = b.bottom() - 126.0f;
    const float h = 108.0f;
    const float leftW = contentW * 0.52f;
    const NUIRect response(contentX, y, leftW - 10.0f, h);
    const NUIRect field(contentX + leftW, y, contentW - leftW, h);

    renderer.fillRoundedRect(response, 7.0f, verbSurfaceBg().withAlpha(0.94f));
    renderer.strokeRoundedRect(response, 7.0f, 1.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.drawText("REVERB RESPONSE", {response.x + 12.0f, response.y + 10.0f}, 9.5f, accent.withAlpha(0.78f));
    for (int i = 0; i < 4; ++i) {
        const float gy = response.y + 30.0f + static_cast<float>(i) * 14.0f;
        renderer.drawLine({response.x + 44.0f, gy}, {response.right() - 14.0f, gy}, 1.0f, NUIColor(1, 1, 1, 0.065f));
    }
    std::array<NUIPoint, 72> top{};
    std::array<NUIPoint, 72> bottom{};
    const float mode = getParamValue(kMode);
    const float decay = getParamValue(kDecay);
    const float damping = getParamValue(kDamping);
    const float diffusion = getParamValue(kDiffusion);
    const float predelay = getParamValue(kPredelay);
    const float modRateForResponse = getParamValue(kModRate);
    const float modDepthForResponse = getParamValue(kModDepth);
    const float topDivisor = top.size() > 1 ? static_cast<float>(top.size() - 1) : 1.0f;
    for (size_t i = 0; i < top.size(); ++i) {
        const float t = static_cast<float>(i) / topDivisor;
        const float x = response.x + 44.0f + t * (response.width - 58.0f);
        const float tailSlope = 34.0f + decay * 30.0f;
        const float darkTilt = damping * 12.0f * t;
        const float density = 1.0f - diffusion;
        const float earlyGap = std::exp(-std::max(0.0f, t - predelay * 0.22f) * (10.0f + diffusion * 8.0f));
        const float ripple = (std::sin(t * (42.0f + mode * 18.0f) + m_visualPhase * (0.7f + modRateForResponse)) * 4.4f +
                              std::sin(t * (94.0f - damping * 24.0f) - m_visualPhase * 1.3f) * 2.3f) *
            (0.18f + density * 0.82f + modDepthForResponse * 0.35f) * (1.0f - t * 0.65f);
        const float shimmer = std::sin(m_visualPhase * 2.0f + t * 18.0f) * modDepthForResponse * 4.0f * (1.0f - t);
        const float yy = response.y + 30.0f + t * tailSlope + darkTilt - earlyGap * 7.0f + ripple;
        top[i] = {x, yy + shimmer};
        bottom[i] = {x, response.bottom() - 14.0f};
    }
    renderer.fillWaveformGradient(top.data(), bottom.data(), static_cast<int>(top.size()),
                                  accent.withAlpha(0.56f), accent.withAlpha(0.14f));
    renderer.drawText("0", {response.x + 22.0f, response.y + 27.0f}, 8.0f, theme.getColor("textSecondary").withAlpha(0.58f));
    renderer.drawText("-60", {response.x + 12.0f, response.bottom() - 18.0f}, 8.0f, theme.getColor("textSecondary").withAlpha(0.58f));

    renderer.fillRoundedRect(field, 7.0f, verbSurfaceBg().withAlpha(0.94f));
    renderer.strokeRoundedRect(field, 7.0f, 1.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.drawText("STEREO FIELD", {field.x + 14.0f, field.y + 10.0f}, 9.5f, accent.withAlpha(0.78f));
    const NUIPoint c{field.x + field.width * 0.50f, field.y + 57.0f};
    const float width = getParamValue(kWidth);
    const float modRate = getParamValue(kModRate);
    const float modDepth = getParamValue(kModDepth);
    const float radius = 24.0f + width * 13.0f;
    renderer.strokeCircle(c, radius, 1.0f, NUIColor(1, 1, 1, 0.20f));
    renderer.strokeCircle(c, radius * 0.58f, 1.0f, NUIColor(1, 1, 1, 0.055f));
    renderer.drawLine({c.x - radius, c.y}, {c.x + radius, c.y}, 1.0f, NUIColor(1, 1, 1, 0.13f));
    renderer.drawLine({c.x, c.y - radius}, {c.x, c.y + radius}, 1.0f, NUIColor(1, 1, 1, 0.13f));
    for (int i = 0; i < 84; ++i) {
        const float a = static_cast<float>(i) * 2.399963f + m_visualPhase * (0.25f + modRate * 0.85f);
        const float spread = radius * (0.16f + 0.82f * std::fmod(static_cast<float>(i * 17), 37.0f) / 37.0f);
        const float ellipse = 0.48f + width * 0.50f + std::sin(m_visualPhase * 0.8f) * modDepth * 0.08f;
        const float pulse = 1.0f + std::sin(m_visualPhase * (1.2f + modRate) + static_cast<float>(i) * 0.31f) * modDepth * 0.22f;
        const float swirl = a + modRate * 0.55f + std::sin(static_cast<float>(i) * 0.37f + m_visualPhase) * modDepth * 2.8f;
        renderer.fillCircle({c.x + std::cos(swirl) * spread * pulse,
                             c.y + std::sin(swirl) * spread * ellipse * pulse}, 0.95f, accent.withAlpha(0.14f + modDepth * 0.18f));
    }
}

void AestraVerbEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    if (auto* parent = getParent()) {
        const auto parentBounds = parent->getBounds();
        const bool parentChanged = !m_haveParentSnapshot ||
            std::abs(parentBounds.x - m_lastParentBounds.x) > 0.5f ||
            std::abs(parentBounds.y - m_lastParentBounds.y) > 0.5f ||
            std::abs(parentBounds.width - m_lastParentBounds.width) > 0.5f ||
            std::abs(parentBounds.height - m_lastParentBounds.height) > 0.5f;
        if (parentChanged && !isDraggingWindow()) {
            m_lastParentBounds = parentBounds;
            m_haveParentSnapshot = true;
            enforceBoundsInParent(!m_userPositioned);
            layoutControls();
        } else if (!m_haveParentSnapshot) {
            m_lastParentBounds = parentBounds;
            m_haveParentSnapshot = true;
        }
    }
    const float modRate = getParamValue(kModRate);
    const float modDepth = getParamValue(kModDepth);
    m_visualPhase += static_cast<float>(deltaTime) * (0.55f + modRate * 2.2f + modDepth * 1.6f);
    if (m_visualPhase > kTwoPi * 8.0f) {
        m_visualPhase = std::fmod(m_visualPhase, kTwoPi * 8.0f);
    }
    const float targetModePosition = static_cast<float>(std::clamp(static_cast<int>(std::round(getParamValue(kMode) * 2.0f)), 0, 2));
    const float modeDiff = targetModePosition - m_modeIndicatorPosition;
    if (std::abs(modeDiff) > 0.001f) {
        const float step = modeDiff * 12.0f * static_cast<float>(deltaTime);
        // Clamp step magnitude to avoid overshooting the target
        m_modeIndicatorPosition += std::copysign(std::min(std::abs(step), std::abs(modeDiff)), modeDiff);
        if (std::abs(targetModePosition - m_modeIndicatorPosition) < 0.001f) {
            m_modeIndicatorPosition = targetModePosition;
        }
        setDirty(true);
    }
    m_visualDirtyAccum += static_cast<float>(deltaTime);
    if (m_visualDirtyAccum >= 1.0f / 30.0f) {
        m_visualDirtyAccum = 0.0f;
        setDirty(true);
    }
}

void AestraVerbEditor::drawSectionLabels(NUIRenderer& renderer) {
    auto b = getBounds();
    const float mainX = editorContentX(b);
    const float contentW = b.width - (mainX - b.x) - kPad;
    const float rightX = mainX + contentW - 144.0f;

    renderer.drawText("PREDELAY / SIZE", {mainX + 6.0f, b.y + 120.0f}, 8.5f,
                      verbAccent().withAlpha(0.50f));

    renderer.drawText("TONE", {rightX, b.y + 104.0f}, 8.5f,
                      verbAccent().withAlpha(0.50f));

    renderer.drawText("MODULATION", {rightX, b.y + 208.0f}, 8.5f,
                      verbAccent().withAlpha(0.50f));
}

void AestraVerbEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    auto b = getBounds();
    NUIColor accent = verbAccent();
    drawPresetStrip(renderer, accent);
    drawModeSelector(renderer, accent);
    const float mainX = editorContentX(b);
    const float contentW = b.width - (mainX - b.x) - kPad;
    renderer.drawLine({mainX - 4.0f, b.y + 104.0f}, {mainX + contentW + 4.0f, b.y + 104.0f}, 1.0f,
                      NUIColor(1, 1, 1, 0.050f));
    renderer.fillRoundedRect({mainX - 8.0f, b.y + 106.0f, contentW + 16.0f, 294.0f}, 8.0f,
                             verbSurfaceBg().withAlpha(0.92f));
    renderer.strokeRoundedRect({mainX - 8.0f, b.y + 106.0f, contentW + 16.0f, 294.0f}, 8.0f, 1.0f,
                               NUIColor(1, 1, 1, 0.080f));
    renderer.drawLine({mainX + 125.0f, b.y + 146.0f}, {mainX + 125.0f, b.y + 358.0f}, 1.0f,
                      NUIColor(1, 1, 1, 0.045f));
    renderer.drawLine({mainX + contentW - 151.0f, b.y + 146.0f}, {mainX + contentW - 151.0f, b.y + 358.0f}, 1.0f,
                      NUIColor(1, 1, 1, 0.045f));
    for (const auto& k : m_knobs) drawKnob(renderer, k, accent);
    drawSectionLabels(renderer);
    drawMixSlider(renderer, accent);
    drawAnalysisPanels(renderer, accent);
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

int AestraVerbEditor::hitTestPreset(float x, float y) const {
    for (size_t i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].bounds.contains({x, y})) return static_cast<int>(i);
    }
    return -1;
}

bool AestraVerbEditor::hitTestMix(float x, float y) const {
    return m_mixBounds.contains({x, y});
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

void AestraVerbEditor::applyPreset(const PresetButton& preset) {
    updateParameter(kMode, static_cast<float>(preset.mode) / 2.0f);
    updateParameter(kSize, preset.size);
    updateParameter(kDecay, preset.decay);
    updateParameter(kDamping, preset.damping);
    updateParameter(kDiffusion, preset.diffusion);
    updateParameter(kModRate, preset.modRate);
    updateParameter(kModDepth, preset.modDepth);
    updateParameter(kWidth, preset.width);
    updateParameter(kMix, preset.mix);
}

bool AestraVerbEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    // Let base class handle title bar / close / drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    auto b = getBounds();
    const bool isDraggingKnob = std::any_of(m_knobs.begin(), m_knobs.end(),
                                            [](const Knob& knob) { return knob.dragging; });
    const bool contains = b.contains(event.position);
    const auto updateHoverState = [&]() {
        const int h = contains ? hitTestKnob(event.position.x, event.position.y) : -1;
        const int mh = contains ? hitTestMode(event.position.x, event.position.y) : -1;
        const int ph = contains ? hitTestPreset(event.position.x, event.position.y) : -1;
        const bool mixHovered = contains && hitTestMix(event.position.x, event.position.y);
        if (h == m_hoveredKnob && mh == m_hoveredMode && ph == m_hoveredPreset &&
            mixHovered == m_mixHovered) {
            return;
        }
        m_hoveredKnob = h;
        m_hoveredMode = mh;
        m_hoveredPreset = ph;
        m_mixHovered = mixHovered;
        for (size_t i = 0; i < m_knobs.size(); ++i) m_knobs[i].hovered = (static_cast<int>(i) == h);
        for (size_t i = 0; i < m_modes.size(); ++i) m_modes[i].hovered = (static_cast<int>(i) == mh);
        for (size_t i = 0; i < m_presets.size(); ++i) m_presets[i].hovered = (static_cast<int>(i) == ph);
        setDirty(true);
    };
    if (!isDraggingWindow() && !isDraggingKnob && !m_draggingMix) {
        updateHoverState();
    }
    if (event.released) {
        if (m_pressedMode != -1 || m_pressedPreset != -1) {
            m_pressedMode = -1;
            m_pressedPreset = -1;
            setDirty(true);
        }
    }
    if (!contains && !isDraggingWindow() && !isDraggingKnob && !m_draggingMix) {
        if (m_hoveredKnob != -1 || m_hoveredMode != -1 || m_hoveredPreset != -1 || m_mixHovered) {
            m_hoveredKnob = -1;
            m_hoveredMode = -1;
            m_hoveredPreset = -1;
            m_mixHovered = false;
            for (auto& knob : m_knobs) knob.hovered = false;
            for (auto& mode : m_modes) mode.hovered = false;
            for (auto& preset : m_presets) preset.hovered = false;
            setDirty(true);
        }
        return false;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        const int modeIdx = hitTestMode(event.position.x, event.position.y);
        if (modeIdx >= 0) {
            const auto mode = m_modes[static_cast<size_t>(modeIdx)].mode;
            m_pressedMode = mode;
            m_focusedMode = mode;
            m_focusedKnob = -1;
            m_focusedPreset = -1;
            m_mixFocused = false;
            updateParameter(kMode, static_cast<float>(mode) / 2.0f);
            return true;
        }
        const int presetIdx = hitTestPreset(event.position.x, event.position.y);
        if (presetIdx >= 0) {
            m_pressedPreset = presetIdx;
            m_focusedPreset = presetIdx;
            m_focusedKnob = -1;
            m_focusedMode = -1;
            m_mixFocused = false;
            applyPreset(m_presets[static_cast<size_t>(presetIdx)]);
            return true;
        }
        if (hitTestMix(event.position.x, event.position.y)) {
            m_draggingMix = true;
            m_mixFocused = true;
            m_focusedKnob = -1;
            m_focusedMode = -1;
            m_focusedPreset = -1;
            updateParameter(kMix, (event.position.x - m_mixTrack.x) / std::max(1.0f, m_mixTrack.width));
            return true;
        }
        const int kIdx = hitTestKnob(event.position.x, event.position.y);
        if (kIdx >= 0) {
            m_focusedKnob = kIdx;
            m_focusedMode = -1;
            m_focusedPreset = -1;
            m_mixFocused = false;
            m_knobs[static_cast<size_t>(kIdx)].dragging = true;
            m_knobs[static_cast<size_t>(kIdx)].dragStartY = event.position.y;
            m_knobs[static_cast<size_t>(kIdx)].dragStartValue = m_knobs[static_cast<size_t>(kIdx)].value;
            return true;
        }
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
        updateHoverState();
    }
    return contains;
}

} // namespace AestraUI
