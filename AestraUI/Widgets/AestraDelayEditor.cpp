// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraDelayEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Plugin/AestraDelay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace AestraUI {

namespace {
constexpr float kPi = 3.14159265358979323846f;
NUIColor accent() { return NUIColor(0.48f, 0.34f, 0.78f, 1.0f); }
NUIColor cyanAccent() { return NUIColor(0.0f, 0.90f, 0.80f, 1.0f); }
NUIColor cardBg() { return NUIColor(0.085f, 0.080f, 0.115f, 0.95f); }
NUIRect offsetRect(const NUIRect& r, float dx, float dy) {
    return NUIRect(r.x + dx, r.y + dy, r.width, r.height);
}
}

AestraDelayEditor::AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)) {
    setId("AestraDelayEditor");
    setPanelTitle("Aestra Delay");
    setSize(kWinW, kWinH);
    setEnforceParentBounds(true);
    buildControls();
}

void AestraDelayEditor::setPlatformBridge(NUIPlatformBridge* bridge) {
    AestraPanelWindow::setPlatformBridge(bridge);
    for (auto& knob : m_knobs) {
        if (knob.slider) {
            knob.slider->setPlatformBridge(bridge);
        }
    }
}

void AestraDelayEditor::buildControls() {
    m_knobs.clear();
    m_baseButtons.clear();
    m_modifierButtons.clear();
    if (!m_instance) return;

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    struct Meta { const char* label; uint32_t id; bool readOnly; float defaultValue; };
    const Meta meta[] = {
        {"Time", Delay::kTime, false, 0.25f},
        {"Feedback", Delay::kFeedback, false, 0.3f},
        {"Damping", Delay::kDamping, false, 0.2f},
        {"Stereo", Delay::kStereoShift, false, 0.5f},
        {"Mod Rate", Delay::kModRate, false, 0.1f},
        {"Mod Depth", Delay::kModDepth, false, 0.0f},
    };

    for (const auto& item : meta) {
        KnobControl k;
        k.label = item.label;
        k.paramId = item.id;
        k.readOnly = item.readOnly;

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

        k.slider = slider;
        addChild(slider);
        m_knobs.push_back(k);
    }

    // Sync tier buttons
    const char* baseLabels[] = {"1/4", "1/8", "1/16", "1/32"};
    for (const char* lbl : baseLabels) {
        TierButton b;
        b.label = lbl;
        m_baseButtons.push_back(b);
    }
    const char* modLabels[] = {"Straight", "Dotted", "Triplet"};
    for (const char* lbl : modLabels) {
        TierButton b;
        b.label = lbl;
        m_modifierButtons.push_back(b);
    }

    // Derive base/modifier from stored division parameter
    const float divParam = m_instance->getParameter(Delay::kNoteDivision);
    const int divIdx = Delay::noteDivisionIndexFromParam(divParam);
    divisionIndexToBaseModifier(divIdx, m_syncBaseIndex, m_syncModifierIndex);

    layoutControls();
}

void AestraDelayEditor::onResize(int width, int height) {
    layoutControls();
}

void AestraDelayEditor::layoutControls() {
    const auto b = getBounds();
    const float contentX = kPad;
    const float contentW = b.width - kPad * 2.0f;

    const float pillY = AestraPanelWindow::TITLE_BAR_H + 14.0f;
    const float pillH = 34.0f;
    const float groupGap = 14.0f;
    const float groupW = (contentW - groupGap) * 0.5f;
    m_freeRect = {contentX, pillY, groupW * 0.5f, pillH};
    m_syncRect = {contentX + groupW * 0.5f, pillY, groupW * 0.5f, pillH};
    m_stereoRect = {contentX + groupW + groupGap, pillY, groupW * 0.5f, pillH};
    m_pingPongRect = {contentX + groupW + groupGap + groupW * 0.5f, pillY, groupW * 0.5f, pillH};

    const bool sync = m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) > 0.5f;
    const float gridY = pillY + pillH + 14.0f;

    const float knobY = gridY + 16.0f;
    const float cellGap = 14.0f;
    const float cellW = (contentW - cellGap * 2.0f) / 3.0f;
    const float cellH = 84.0f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        const int row = static_cast<int>(i / 3);
        const int col = static_cast<int>(i % 3);
        const float x = contentX + static_cast<float>(col) * (cellW + cellGap);
        const float y = knobY + static_cast<float>(row) * (cellH + 10.0f);
        m_knobs[i].bounds = {x, y, cellW, cellH};
        const NUIRect knobRect = {x + (cellW - kKnobSize) * 0.5f, y + 16.0f, kKnobSize, kKnobSize};
        if (m_knobs[i].slider) {
            m_knobs[i].slider->setBounds(knobRect);
        }
    }

    // Sync panel replaces the Time knob (index 0) in sync mode
    if (sync && !m_baseButtons.empty() && !m_modifierButtons.empty()) {
        const float syncPanelX = m_knobs[0].bounds.x;
        const float syncPanelW = m_knobs[0].bounds.width;
        const float containerH = m_knobs[0].bounds.height; // 84 px
        const float btnGap = 6.0f;
        const float btnH = 30.0f;
        const float rowGap = 6.0f;
        const float readoutGap = 4.0f;
        const float labelH = 10.0f;

        const float contentH = btnH + rowGap + btnH + readoutGap + labelH;
        const float topPadding = (containerH - contentH) * 0.5f;
        const float syncPanelY = m_knobs[0].bounds.y + topPadding;

        const float baseBtnW = (syncPanelW - btnGap * 3.0f) / 4.0f;
        for (int i = 0; i < 4; ++i) {
            m_baseButtons[i].bounds = {syncPanelX + static_cast<float>(i) * (baseBtnW + btnGap), syncPanelY, baseBtnW, btnH};
        }

        const float modBtnW = (syncPanelW - btnGap * 2.0f) / 3.0f;
        const float modRowY = syncPanelY + btnH + rowGap;
        for (int i = 0; i < 3; ++i) {
            m_modifierButtons[i].bounds = {syncPanelX + static_cast<float>(i) * (modBtnW + btnGap), modRowY, modBtnW, btnH};
        }

        const float readoutY = modRowY + btnH + readoutGap;
        m_syncReadoutRect = {syncPanelX, readoutY, syncPanelW, labelH};
    }

    m_mixSliderRect = {contentX, b.height - 48.0f, contentW, 28.0f};

    if (m_instance) {
        using Delay = Aestra::Audio::Plugins::AestraDelay;
        const bool isSync = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
        for (auto& k : m_knobs) {
            if (k.paramId == Delay::kTime) k.readOnly = isSync;
        }
    }
}

void AestraDelayEditor::drawPillSwitches(NUIRenderer& renderer, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const bool sync = m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    const bool ping = m_instance && m_instance->getParameter(Delay::kStereoMode) > 0.5f;

    auto drawGroup = [&](const NUIRect& leftLocal, const NUIRect& rightLocal, const char* l, const char* r, bool rightActive, const NUIColor& activeColor) {
        NUIRect left = offsetRect(leftLocal, wx, wy);
        NUIRect right = offsetRect(rightLocal, wx, wy);
        const NUIRect outer(left.x, left.y, left.width + right.width, left.height);
        renderer.fillRoundedRect(outer, 10.0f, NUIColor(0.032f, 0.030f, 0.044f, 0.96f));
        renderer.strokeRoundedRect(outer, 10.0f, 1.0f, accent().withAlpha(0.45f));
        auto seg = [&](const NUIRect& rect, const char* label, bool active) {
            renderer.fillRoundedRect(rect, 9.0f, active ? activeColor : NUIColor(0, 0, 0, 0));
            renderer.drawText(label, {rect.center().x - 24.0f, rect.y + 11.0f}, 10.0f,
                              active ? theme.getColor("textPrimary") : theme.getColor("textSecondary").withAlpha(0.76f));
        };
        seg(left, l, !rightActive);
        seg(right, r, rightActive);
    };

    drawGroup(m_freeRect, m_syncRect, "Free", "Sync", sync, accent());
    drawGroup(m_stereoRect, m_pingPongRect, "Stereo", "Ping-Pong", ping, accent().withAlpha(0.82f));

    // Divider between time mode and channel mode groups
    const float dividerX = wx + m_syncRect.x + m_syncRect.width + 7.0f;
    renderer.drawLine({dividerX, wy + m_syncRect.y + 6.0f},
                      {dividerX, wy + m_syncRect.y + m_syncRect.height - 6.0f},
                      1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.30f));
}

void AestraDelayEditor::drawSyncPanel(NUIRenderer& renderer, float wx, float wy) {
    if (!m_instance || m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) <= 0.5f) return;
    if (m_baseButtons.empty() || m_modifierButtons.empty()) return;

    auto& theme = NUIThemeManager::getInstance();

    auto drawTierButton = [&](const TierButton& btn, bool active, bool hovered) {
        NUIRect r = offsetRect(btn.bounds, wx, wy);
        if (active) {
            renderer.fillRoundedRect(r, 6.0f, accent());
            renderer.drawTextCentered(btn.label, r, 9.5f, theme.getColor("textPrimary"));
        } else {
            NUIColor fill = hovered ? NUIColor(0.10f, 0.095f, 0.14f, 0.92f)
                                     : NUIColor(0.07f, 0.065f, 0.09f, 0.90f);
            renderer.fillRoundedRect(r, 6.0f, fill);
            renderer.strokeRoundedRect(r, 6.0f, 1.0f, accent().withAlpha(0.32f));
            renderer.drawTextCentered(btn.label, r, 9.5f, theme.getColor("textSecondary").withAlpha(0.78f));
        }
    };

    for (int i = 0; i < 4; ++i) {
        drawTierButton(m_baseButtons[i], i == m_syncBaseIndex, i == m_hoveredBaseButton);
    }
    for (int i = 0; i < 3; ++i) {
        drawTierButton(m_modifierButtons[i], i == m_syncModifierIndex, i == m_hoveredModifierButton);
    }

    // Derived ms readout
    NUIRect readout = offsetRect(m_syncReadoutRect, wx, wy);
    const std::string text = syncReadoutText();
    if (!text.empty()) {
        renderer.drawTextCentered(text, readout, 8.0f, accent().withAlpha(0.9f));
    }
}

std::string AestraDelayEditor::syncReadoutText() const {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (!m_instance) return {};
    if (auto delay = std::dynamic_pointer_cast<Delay>(m_instance)) {
        int ms = static_cast<int>(std::round(delay->getEffectiveDelayMs()));
        return std::to_string(ms) + "ms";
    }
    return {};
}

std::string AestraDelayEditor::formattedValue(uint32_t paramId) const {
    if (!m_instance) return {};
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (paramId == Delay::kTime) {
        if (auto delay = std::dynamic_pointer_cast<Delay>(m_instance)) {
            return std::to_string(static_cast<int>(std::round(delay->getEffectiveDelayMs()))) + "ms";
        }
    }
    return m_instance->getParameterDisplay(paramId);
}

void AestraDelayEditor::drawKnob(NUIRenderer& renderer, const KnobControl& k, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    const float value = k.slider ? static_cast<float>(k.slider->getValue()) : 0.0f;
    const NUIColor knobAccent = k.readOnly ? cyanAccent() : accent();
    NUIRect bounds = offsetRect(k.bounds, wx, wy);
    NUIRect knobRect = k.slider ? offsetRect(k.slider->getBounds(), wx, wy) : NUIRect();
    renderer.fillRoundedRect(bounds, 10.0f, cardBg());
    renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, NUIColor(1, 1, 1, 0.06f));

    const float cx = knobRect.center().x;
    const float cy = knobRect.center().y;
    const float r = kKnobSize * 0.40f;
    renderer.fillCircle({cx, cy}, r + 7.0f, knobAccent.withAlpha(0.07f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.045f, 0.043f, 0.060f, 0.96f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, knobAccent.withAlpha(0.32f));

    const float sa = kPi * 0.75f;
    const float ea = sa + value * kPi * 1.5f;
    for (int i = 0; i < 34; ++i) {
        const float a1 = sa + (ea - sa) * static_cast<float>(i) / 34.0f;
        const float a2 = sa + (ea - sa) * static_cast<float>(i + 1) / 34.0f;
        renderer.drawLine({cx + std::cos(a1) * (r - 3.0f), cy + std::sin(a1) * (r - 3.0f)},
                          {cx + std::cos(a2) * (r - 3.0f), cy + std::sin(a2) * (r - 3.0f)},
                          3.0f, knobAccent.withAlpha(k.readOnly ? 0.40f : 0.82f));
    }

    const float pa = sa + value * kPi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa) * (r - 7.0f), cy + std::sin(pa) * (r - 7.0f)}, 3.2f, knobAccent);
    renderer.drawText(k.label, {bounds.x + 8.0f, bounds.y + 5.0f}, 9.0f, theme.getColor("textPrimary"));
    const std::string valueStr = formattedValue(k.paramId);
    const float valueW = renderer.measureText(valueStr, 8.0f).width;
    const float valueBaseline = knobRect.bottom() + 6.0f + 8.0f;
    renderer.drawText(valueStr, {bounds.center().x - valueW * 0.5f, valueBaseline}, 8.0f,
                      knobAccent.withAlpha(0.9f));
}

void AestraDelayEditor::drawMixSlider(NUIRenderer& renderer, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const float mix = m_instance ? m_instance->getParameter(Delay::kMix) : 0.0f;
    NUIRect mixRect = offsetRect(m_mixSliderRect, wx, wy);
    const NUIRect track(mixRect.x + 58.0f, mixRect.y + 11.0f, mixRect.width - 104.0f, 6.0f);
    renderer.fillRoundedRect(mixRect, 10.0f, NUIColor(0.07f, 0.065f, 0.09f, 0.92f));
    renderer.strokeRoundedRect(mixRect, 10.0f, 1.0f, accent().withAlpha(0.35f));
    renderer.drawText("Mix", {mixRect.x + 14.0f, mixRect.y + 9.0f}, 9.0f, theme.getColor("textPrimary"));
    renderer.fillRoundedRect(track, 3.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 3.0f, accent().withAlpha(0.92f));
    renderer.fillCircle({track.x + track.width * mix, track.center().y}, 8.0f, theme.getColor("textPrimary"));
    renderer.drawText(std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%",
                      {mixRect.right() - 42.0f, mixRect.y + 9.0f}, 8.0f, accent().withAlpha(0.92f));
}

void AestraDelayEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    const auto b = getBounds();
    const float wx = b.x;
    const float wy = b.y;
    drawPillSwitches(renderer, wx, wy);
    drawSyncPanel(renderer, wx, wy);

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const bool sync = m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        // Hide Time knob (index 0) in sync mode
        if (sync && i == 0) continue;
        drawKnob(renderer, m_knobs[i], wx, wy);
    }
    drawMixSlider(renderer, wx, wy);
}

int AestraDelayEditor::hitTestBaseButton(float localX, float localY) const {
    for (size_t i = 0; i < m_baseButtons.size(); ++i) {
        if (m_baseButtons[i].bounds.contains({localX, localY})) return static_cast<int>(i);
    }
    return -1;
}

int AestraDelayEditor::hitTestModifierButton(float localX, float localY) const {
    for (size_t i = 0; i < m_modifierButtons.size(); ++i) {
        if (m_modifierButtons[i].bounds.contains({localX, localY})) return static_cast<int>(i);
    }
    return -1;
}

int AestraDelayEditor::computeDivisionIndex(int baseIdx, int modifierIdx) const {
    switch (baseIdx) {
        case 0: // 1/4
            switch (modifierIdx) {
                case 0: return 2;   // Straight → 1/4
                case 1: return 7;   // Dotted → 1/4D
                case 2: return 10;  // Triplet → 1/4T
            }
            break;
        case 1: // 1/8 (default)
            switch (modifierIdx) {
                case 0: return 4;   // Straight → 1/8
                case 1: return 8;   // Dotted → 1/8D
                case 2: return 11;  // Triplet → 1/8T
            }
            break;
        case 2: // 1/16
            switch (modifierIdx) {
                case 0: return 3;   // Straight → 1/16
                case 1: return 9;   // Dotted → 1/16D
                case 2: return 12;  // Triplet → 1/16T
            }
            break;
        case 3: // 1/32 — no dotted/triplet variants in enum
            return 5; // Always 1/32
    }
    return 4; // Default 1/8
}

void AestraDelayEditor::divisionIndexToBaseModifier(int divisionIdx, int& baseIdx, int& modifierIdx) const {
    switch (divisionIdx) {
        case 2:  baseIdx = 0; modifierIdx = 0; return; // 1/4
        case 7:  baseIdx = 0; modifierIdx = 1; return; // 1/4D
        case 10: baseIdx = 0; modifierIdx = 2; return; // 1/4T
        case 4:  baseIdx = 1; modifierIdx = 0; return; // 1/8
        case 8:  baseIdx = 1; modifierIdx = 1; return; // 1/8D
        case 11: baseIdx = 1; modifierIdx = 2; return; // 1/8T
        case 3:  baseIdx = 2; modifierIdx = 0; return; // 1/16
        case 9:  baseIdx = 2; modifierIdx = 1; return; // 1/16D
        case 12: baseIdx = 2; modifierIdx = 2; return; // 1/16T
        case 5:  baseIdx = 3; modifierIdx = 0; return; // 1/32
        default: baseIdx = 1; modifierIdx = 0; return; // Default 1/8
    }
}

void AestraDelayEditor::applySyncSelection() {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (!m_instance) return;
    const int divIdx = computeDivisionIndex(m_syncBaseIndex, m_syncModifierIndex);
    m_instance->setParameter(Delay::kNoteDivision, Delay::noteDivisionParamFromIndex(divIdx));
    setDirty(true);
}

bool AestraDelayEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    // Let base class handle title bar / close / drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    const auto b = getBounds();
    const float mx = event.position.x - b.x;
    const float my = event.position.y - b.y;
    const bool contains = b.contains(event.position);

    if (!contains && !isDraggingWindow() && !m_draggingMix) return false;

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (m_instance) {
            if (m_freeRect.contains({mx, my})) {
                m_instance->setParameter(Delay::kSyncMode, 0.0f);
                layoutControls();
                setDirty(true);
                return true;
            }
            if (m_syncRect.contains({mx, my})) {
                m_instance->setParameter(Delay::kSyncMode, 1.0f);
                layoutControls();
                setDirty(true);
                return true;
            }
            if (m_stereoRect.contains({mx, my})) {
                m_instance->setParameter(Delay::kStereoMode, 0.0f);
                setDirty(true);
                return true;
            }
            if (m_pingPongRect.contains({mx, my})) {
                m_instance->setParameter(Delay::kStereoMode, 1.0f);
                setDirty(true);
                return true;
            }
            if (m_instance->getParameter(Delay::kSyncMode) > 0.5f) {
                const int base = hitTestBaseButton(mx, my);
                if (base >= 0 && base != m_syncBaseIndex) {
                    m_syncBaseIndex = base;
                    // 1/32 has no dotted/triplet variants — reset modifier to Straight
                    if (base == 3) m_syncModifierIndex = 0;
                    applySyncSelection();
                    return true;
                }
                const int mod = hitTestModifierButton(mx, my);
                if (mod >= 0 && mod != m_syncModifierIndex) {
                    // Prevent dotted/triplet on 1/32 base
                    if (m_syncBaseIndex == 3 && mod != 0) return true;
                    m_syncModifierIndex = mod;
                    applySyncSelection();
                    return true;
                }
            }
            if (m_mixSliderRect.contains({mx, my})) {
                m_draggingMix = true;
                const float sliderX = m_mixSliderRect.x + 58.0f;
                const float sliderW = m_mixSliderRect.width - 104.0f;
                m_instance->setParameter(Delay::kMix, std::clamp((mx - sliderX) / sliderW, 0.0f, 1.0f));
                setDirty(true);
                return true;
            }
        }
    }

    if (m_draggingMix && m_instance) {
        if (!event.pressed && event.button == NUIMouseButton::Left) {
            m_draggingMix = false;
            return true;
        }
        const float sliderX = m_mixSliderRect.x + 58.0f;
        const float sliderW = m_mixSliderRect.width - 104.0f;
        m_instance->setParameter(Delay::kMix, std::clamp((mx - sliderX) / sliderW, 0.0f, 1.0f));
        setDirty(true);
        return true;
    }

    if (!event.pressed && !event.released) {
        // Sync panel hover tracking
        if (m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f) {
            const int hb = hitTestBaseButton(mx, my);
            const int hm = hitTestModifierButton(mx, my);
            if (hb != m_hoveredBaseButton || hm != m_hoveredModifierButton) {
                m_hoveredBaseButton = hb;
                m_hoveredModifierButton = hm;
                for (size_t i = 0; i < m_baseButtons.size(); ++i) {
                    m_baseButtons[i].hovered = static_cast<int>(i) == hb;
                }
                for (size_t i = 0; i < m_modifierButtons.size(); ++i) {
                    m_modifierButtons[i].hovered = static_cast<int>(i) == hm;
                }
                setDirty(true);
            }
        } else if (m_hoveredBaseButton >= 0 || m_hoveredModifierButton >= 0) {
            m_hoveredBaseButton = -1;
            m_hoveredModifierButton = -1;
            for (auto& btn : m_baseButtons) btn.hovered = false;
            for (auto& btn : m_modifierButtons) btn.hovered = false;
            setDirty(true);
        }
    }

    // Let NUISlider children handle their own mouse events
    return NUIComponent::onMouseEvent(event);
}

} // namespace AestraUI
