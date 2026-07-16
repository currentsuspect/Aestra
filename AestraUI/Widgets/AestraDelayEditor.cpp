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
NUIColor accent() {
    return NUIColor(0.55f, 0.40f, 0.92f, 1.0f);
}
NUIColor cyanAccent() {
    return NUIColor(0.0f, 0.90f, 0.80f, 1.0f);
}
NUIColor cardBg() {
    return NUIColor(0.084f, 0.084f, 0.084f, 0.95f);
}
NUIRect offsetRect(const NUIRect& r, float dx, float dy) {
    return NUIRect(r.x + dx, r.y + dy, r.width, r.height);
}
} // namespace

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
    if (!m_instance)
        return;

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    struct Meta {
        const char* label;
        uint32_t id;
        bool readOnly;
        float defaultValue;
    };
    const Meta meta[] = {
        {"Time", Delay::kTime, false, 0.25f},
        {"Feedback", Delay::kFeedback, false, 0.3f},
        {"Damping", Delay::kDamping, false, 0.2f},
        {"Stereo", Delay::kStereoShift, false, 0.5f},
        {"Mod Rate", Delay::kModRate, false, 0.1f},
        {"Mod Depth", Delay::kModDepth, false, 0.0f},
        {"Low Cut", Delay::kFeedbackHighpass, false, 0.0f},
        {"Output", Delay::kOutputTrim, false, 0.5f},
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
    (void)width;
    (void)height;
    layoutControls();
    AestraPanelWindow::onResize(width, height);
}

void AestraDelayEditor::onUpdate(double deltaTime) {
    AestraPanelWindow::onUpdate(deltaTime);
    m_controlSyncTimer += deltaTime;
    if (!m_instance || m_controlSyncTimer < 1.0 / 30.0)
        return;
    m_controlSyncTimer = 0.0;
    for (auto& knob : m_knobs) {
        if (knob.slider)
            knob.slider->setValue(std::clamp(m_instance->getParameter(knob.paramId), 0.0f, 1.0f));
    }
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const int division = Delay::noteDivisionIndexFromParam(m_instance->getParameter(Delay::kNoteDivision));
    divisionIndexToBaseModifier(division, m_syncBaseIndex, m_syncModifierIndex);
    setDirty(true);
}

void AestraDelayEditor::layoutControls() {
    const auto b = getBounds();
    const float contentX = kPad;
    const float contentW = b.width - kPad * 2.0f;

    const float pillY = AestraPanelWindow::TITLE_BAR_H + 42.0f;
    const float pillH = 34.0f;
    const float groupGap = 18.0f;
    const float groupW = (contentW - groupGap) * 0.5f;
    constexpr float timeLabelW = 76.0f;
    constexpr float routeLabelW = 78.0f;
    const float timeToggleX = contentX + timeLabelW;
    const float timeToggleW = groupW - timeLabelW;
    const float routeGroupX = contentX + groupW + groupGap;
    const float routeToggleX = routeGroupX + routeLabelW;
    const float routeToggleW = groupW - routeLabelW;
    m_freeRect = {timeToggleX, pillY, timeToggleW * 0.5f, pillH};
    m_syncRect = {timeToggleX + timeToggleW * 0.5f, pillY, timeToggleW * 0.5f, pillH};
    m_stereoRect = {routeToggleX, pillY, routeToggleW * 0.5f, pillH};
    m_pingPongRect = {routeToggleX + routeToggleW * 0.5f, pillY, routeToggleW * 0.5f, pillH};

    // setSize() dispatches onResize() during construction, before buildControls()
    // has populated the fixed Delay control set.
    if (m_knobs.size() < 8)
        return;

    const bool sync = m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) > 0.5f;
    const float mainY = pillY + pillH + 14.0f;
    const float mainBottom = b.height - 54.0f;
    const float mainH = mainBottom - mainY;
    const float mainGap = 12.0f;
    const float timingW = contentW * 0.61f;
    m_timingSectionRect = {contentX, mainY, timingW, mainH};
    m_characterSectionRect = {contentX + timingW + mainGap, mainY, contentW - timingW - mainGap, mainH};
    m_echoDisplayRect = {m_timingSectionRect.x + 12.0f, m_timingSectionRect.y + 31.0f,
                         m_timingSectionRect.width - 24.0f, 112.0f};

    const float knobGap = 8.0f;
    const float timingKnobY = m_echoDisplayRect.bottom() + 10.0f;
    const float timingKnobW = (m_echoDisplayRect.width - knobGap * 3.0f) / 4.0f;
    const float timingKnobH = m_timingSectionRect.bottom() - timingKnobY - 10.0f;
    const int timingIndices[] = {0, 1, 3, 7};
    for (int col = 0; col < 4; ++col) {
        auto& knob = m_knobs[static_cast<size_t>(timingIndices[col])];
        knob.bounds = {m_echoDisplayRect.x + static_cast<float>(col) * (timingKnobW + knobGap), timingKnobY,
                       timingKnobW, timingKnobH};
    }

    const float characterX = m_characterSectionRect.x + 12.0f;
    const float characterY = m_characterSectionRect.y + 31.0f;
    const float characterW = m_characterSectionRect.width - 24.0f;
    const float characterH = m_characterSectionRect.height - 41.0f;
    const float characterCellW = (characterW - knobGap) * 0.5f;
    const float characterCellH = (characterH - knobGap) * 0.5f;
    const int characterIndices[] = {2, 6, 5, 4};
    for (int index = 0; index < 4; ++index) {
        const int row = index / 2;
        const int col = index % 2;
        auto& knob = m_knobs[static_cast<size_t>(characterIndices[index])];
        knob.bounds = {characterX + static_cast<float>(col) * (characterCellW + knobGap),
                       characterY + static_cast<float>(row) * (characterCellH + knobGap), characterCellW,
                       characterCellH};
    }

    for (auto& knob : m_knobs) {
        const NUIRect knobAbs = {knob.bounds.x + b.x + (knob.bounds.width - kKnobSize) * 0.5f,
                                 knob.bounds.y + b.y + 27.0f, kKnobSize, kKnobSize};
        if (knob.slider)
            knob.slider->setBounds(knobAbs);
    }

    if (sync && !m_baseButtons.empty() && !m_modifierButtons.empty()) {
        const float btnY = m_echoDisplayRect.bottom() - 30.0f;
        const float btnH = 22.0f;
        const float btnGap = 4.0f;
        const float splitGap = 12.0f;
        const float availableW = m_echoDisplayRect.width - 24.0f - splitGap;
        const float baseGroupW = availableW * 0.49f;
        const float modifierGroupW = availableW - baseGroupW;
        const float baseX = m_echoDisplayRect.x + 12.0f;
        const float modifierX = baseX + baseGroupW + splitGap;
        const float baseBtnW = (baseGroupW - btnGap * 3.0f) / 4.0f;
        for (int i = 0; i < 4; ++i)
            m_baseButtons[i].bounds = {baseX + static_cast<float>(i) * (baseBtnW + btnGap), btnY, baseBtnW, btnH};
        const float modifierBtnW = (modifierGroupW - btnGap * 2.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            m_modifierButtons[i].bounds = {modifierX + static_cast<float>(i) * (modifierBtnW + btnGap), btnY,
                                           modifierBtnW, btnH};
        m_syncReadoutRect = {m_echoDisplayRect.right() - 154.0f, m_echoDisplayRect.y + 9.0f, 142.0f, 18.0f};
    }

    // --- bypass pill (absolute, same style as Comp/EQ) ---
    constexpr float kBypassW = 88.0f;
    constexpr float kBypassH = 26.0f;
    constexpr float kBypassRightPad = 44.0f;
    m_bypassRect = NUIRect(b.right() - kBypassRightPad - kBypassW, b.y + AestraPanelWindow::TITLE_BAR_H + 6.0f,
                           kBypassW, kBypassH);

    m_mixSliderRect = {contentX, b.height - 42.0f, contentW, 32.0f};

    if (m_instance) {
        using Delay = Aestra::Audio::Plugins::AestraDelay;
        const bool isSync = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
        for (auto& k : m_knobs) {
            if (k.paramId == Delay::kTime) {
                k.readOnly = isSync;
                if (k.slider)
                    k.slider->setVisible(!isSync);
            }
        }
    }
}

void AestraDelayEditor::drawPillSwitches(NUIRenderer& renderer, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const bool sync = m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    const bool ping = m_instance && m_instance->getParameter(Delay::kStereoMode) > 0.5f;

    auto drawGroup = [&](const NUIRect& leftLocal, const NUIRect& rightLocal, const char* groupLabel, const char* l,
                         const char* r, bool rightActive, const NUIColor& activeColor) {
        NUIRect left = offsetRect(leftLocal, wx, wy);
        NUIRect right = offsetRect(rightLocal, wx, wy);
        const NUIRect outer(left.x, left.y, left.width + right.width, left.height);
        const float labelW = groupLabel[0] == 'T' ? 76.0f : 78.0f;
        const NUIRect groupLabelRect(outer.x - labelW, outer.y, labelW - 8.0f, outer.height);
        renderer.drawText(groupLabel, {groupLabelRect.x + 2.0f, groupLabelRect.y + 12.0f}, 9.0f,
                          theme.getColor("textSecondary").withAlpha(0.70f));
        renderer.fillRoundedRect(outer, 9.0f, NUIColor(0.018f, 0.018f, 0.022f, 0.98f));
        renderer.strokeRoundedRect(outer, 9.0f, 1.0f, NUIColor(1.0f, 1.0f, 1.0f, 0.13f));
        renderer.drawLine({right.x, outer.y + 7.0f}, {right.x, outer.bottom() - 7.0f}, 1.0f,
                          NUIColor(1.0f, 1.0f, 1.0f, 0.10f));
        auto seg = [&](const NUIRect& rect, const char* label, bool active) {
            const NUIRect inset(rect.x + 3.0f, rect.y + 3.0f, rect.width - 6.0f, rect.height - 6.0f);
            if (active) {
                renderer.fillRoundedRect(inset, 7.0f, activeColor.withAlpha(0.15f));
                renderer.strokeRoundedRect(inset, 7.0f, 1.0f, activeColor.withAlpha(0.72f));
                renderer.fillCircle({inset.x + 11.0f, inset.center().y}, 2.5f, activeColor);
            }
            renderer.drawTextCentered(label, inset, 10.0f,
                                      active ? activeColor.withAlpha(0.98f)
                                             : theme.getColor("textSecondary").withAlpha(0.72f));
        };
        seg(left, l, !rightActive);
        seg(right, r, rightActive);
    };

    drawGroup(m_freeRect, m_syncRect, "TIME MODE", "Free", "Sync", sync, cyanAccent());
    drawGroup(m_stereoRect, m_pingPongRect, "ROUTING", "Stereo", "Ping Pong", ping, accent());
}

void AestraDelayEditor::drawSectionFrame(NUIRenderer& renderer, const NUIRect& localRect, const std::string& title,
                                         const NUIColor& color, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIRect rect = offsetRect(localRect, wx, wy);
    renderer.fillRoundedRect(rect, 14.0f, NUIColor(0.020f, 0.020f, 0.024f, 0.98f));
    renderer.strokeRoundedRect(rect, 14.0f, 1.5f, color.withAlpha(0.58f));
    renderer.fillRoundedRect({rect.x + 12.0f, rect.y + 12.0f, 4.0f, 16.0f}, 2.0f, color);
    renderer.drawText(title, {rect.x + 24.0f, rect.y + 14.0f}, 10.5f, theme.getColor("textPrimary").withAlpha(0.92f));
}

void AestraDelayEditor::drawEchoDisplay(NUIRenderer& renderer, float wx, float wy) {
    if (!m_instance)
        return;
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const NUIRect rect = offsetRect(m_echoDisplayRect, wx, wy);
    const bool sync = m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    const bool pingPong = m_instance->getParameter(Delay::kStereoMode) > 0.5f;
    const float feedback = std::clamp(m_instance->getParameter(Delay::kFeedback), 0.0f, 1.0f);
    const float stereo = m_instance->getParameter(Delay::kStereoShift) * 2.0f - 1.0f;
    const float modulation = m_instance->getParameter(Delay::kModDepth);

    renderer.fillRoundedRect(rect, 10.0f, NUIColor(0.008f, 0.008f, 0.012f, 0.98f));
    renderer.strokeRoundedRect(rect, 10.0f, 1.0f, cyanAccent().withAlpha(0.26f));
    renderer.drawText("ECHO PATH", {rect.x + 12.0f, rect.y + 9.0f}, 9.5f,
                      theme.getColor("textSecondary").withAlpha(0.82f));

    std::string readout;
    if (sync) {
        readout = m_instance->getParameterDisplay(Delay::kNoteDivision) + "  /  " + syncReadoutText();
    } else {
        readout = formattedValue(Delay::kTime) + "  /  FREE";
    }
    const float readoutW = renderer.measureText(readout, 10.0f).width;
    renderer.drawText(readout, {rect.right() - 12.0f - readoutW, rect.y + 9.0f}, 10.0f, cyanAccent().withAlpha(0.94f));

    const float startX = rect.x + 27.0f;
    const float endX = rect.right() - 20.0f;
    const float topY = rect.y + 43.0f;
    const float bottomY = rect.y + 66.0f;
    renderer.drawText("L", {rect.x + 11.0f, topY - 5.0f}, 8.5f, cyanAccent().withAlpha(0.72f));
    renderer.drawText("R", {rect.x + 11.0f, bottomY - 5.0f}, 8.5f, accent().withAlpha(0.78f));
    renderer.drawLine({startX, topY}, {endX, topY}, 1.0f, cyanAccent().withAlpha(0.12f));
    renderer.drawLine({startX, bottomY}, {endX, bottomY}, 1.0f, accent().withAlpha(0.12f));
    renderer.fillCircle({startX, topY}, 3.4f, cyanAccent());
    renderer.fillCircle({startX, bottomY}, 3.4f, accent());

    NUIPoint previousL{startX, topY};
    NUIPoint previousR{startX, bottomY};
    constexpr int repeats = 5;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        const float progress = static_cast<float>(repeat + 1) / static_cast<float>(repeats);
        const float x = startX + (endX - startX) * progress;
        const float stereoOffset = stereo * 5.0f * progress;
        const float motion = std::sin(progress * kPi * 3.0f) * modulation * 4.0f;
        const float decay = std::pow(std::max(feedback, 0.08f), static_cast<float>(repeat) * 0.55f);
        const float alpha = 0.22f + decay * 0.72f;
        NUIPoint pointL{x - stereoOffset, pingPong && (repeat & 1) ? bottomY : topY + motion};
        NUIPoint pointR{x + stereoOffset, pingPong && !(repeat & 1) ? topY : bottomY - motion};
        renderer.drawLine(previousL, pointL, 1.5f, cyanAccent().withAlpha(alpha * 0.72f));
        renderer.drawLine(previousR, pointR, 1.5f, accent().withAlpha(alpha * 0.72f));
        renderer.fillCircle(pointL, 2.2f + decay * 2.0f, cyanAccent().withAlpha(alpha));
        renderer.fillCircle(pointR, 2.2f + decay * 2.0f, accent().withAlpha(alpha));
        previousL = pointL;
        previousR = pointR;
    }
}

void AestraDelayEditor::drawSyncPanel(NUIRenderer& renderer, float wx, float wy) {
    if (!m_instance || m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kSyncMode) <= 0.5f)
        return;
    if (m_baseButtons.empty() || m_modifierButtons.empty())
        return;

    auto& theme = NUIThemeManager::getInstance();

    const NUIRect timeCard = offsetRect(m_knobs[0].bounds, wx, wy);
    renderer.fillRoundedRect(timeCard, 10.0f, cardBg());
    renderer.strokeRoundedRect(timeCard, 10.0f, 1.0f, cyanAccent().withAlpha(0.30f));
    renderer.drawText("DIVISION", {timeCard.x + 10.0f, timeCard.y + 9.0f}, 9.0f,
                      theme.getColor("textSecondary").withAlpha(0.82f));
    const std::string division = m_instance->getParameterDisplay(Aestra::Audio::Plugins::AestraDelay::kNoteDivision);
    renderer.drawTextCentered(division, {timeCard.x, timeCard.y + 30.0f, timeCard.width, 36.0f}, 20.0f, cyanAccent());
    renderer.drawTextCentered(syncReadoutText(), {timeCard.x, timeCard.bottom() - 27.0f, timeCard.width, 18.0f}, 10.0f,
                              theme.getColor("textPrimary").withAlpha(0.86f));

    auto drawTierButton = [&](const TierButton& btn, bool active, bool hovered) {
        NUIRect r = offsetRect(btn.bounds, wx, wy);
        if (active) {
            renderer.fillRoundedRect(r, 6.0f, accent());
            renderer.drawTextCentered(btn.label, r, 9.5f, theme.getColor("textPrimary"));
        } else {
            NUIColor fill = hovered ? NUIColor(0.099f, 0.099f, 0.099f, 0.92f) : NUIColor(0.068f, 0.068f, 0.068f, 0.90f);
            renderer.fillRoundedRect(r, 6.0f, fill);
            renderer.strokeRoundedRect(r, 6.0f, 1.0f, accent().withAlpha(0.32f));
            renderer.drawTextCentered(btn.label, r, 9.5f, theme.getColor("textSecondary").withAlpha(0.88f));
        }
    };

    for (int i = 0; i < 4; ++i) {
        drawTierButton(m_baseButtons[i], i == m_syncBaseIndex, i == m_hoveredBaseButton);
    }
    for (int i = 0; i < 3; ++i) {
        drawTierButton(m_modifierButtons[i], i == m_syncModifierIndex, i == m_hoveredModifierButton);
    }
}

std::string AestraDelayEditor::syncReadoutText() const {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (!m_instance)
        return {};
    if (auto delay = std::dynamic_pointer_cast<Delay>(m_instance)) {
        int ms = static_cast<int>(std::round(delay->getEffectiveDelayMs()));
        return std::to_string(ms) + "ms";
    }
    return {};
}

std::string AestraDelayEditor::formattedValue(uint32_t paramId) const {
    if (!m_instance)
        return {};
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
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const float value = k.slider ? static_cast<float>(k.slider->getValue()) : 0.0f;
    NUIColor knobAccent = accent();
    if (k.paramId == Delay::kTime || k.paramId == Delay::kStereoShift || k.paramId == Delay::kDamping ||
        k.paramId == Delay::kFeedbackHighpass)
        knobAccent = cyanAccent();
    else if (k.paramId == Delay::kModDepth || k.paramId == Delay::kModRate)
        knobAccent = NUIColor(0.94f, 0.34f, 0.68f, 1.0f);
    if (k.readOnly)
        knobAccent = cyanAccent().withAlpha(0.72f);
    NUIRect bounds = offsetRect(k.bounds, wx, wy);
    // Slider bounds are already absolute (set in layoutControls).
    NUIRect knobRect = k.slider ? k.slider->getBounds() : NUIRect();
    renderer.fillRoundedRect(bounds, 10.0f, cardBg());
    renderer.strokeRoundedRect(bounds, 10.0f, 1.0f, NUIColor(1, 1, 1, 0.10f));

    const float cx = knobRect.center().x;
    const float cy = knobRect.center().y;
    const float r = kKnobSize * 0.40f;
    renderer.fillCircle({cx, cy}, r + 7.0f, knobAccent.withAlpha(0.07f));
    renderer.fillCircle({cx, cy}, r, NUIColor(0.045f, 0.045f, 0.045f, 0.96f));
    renderer.strokeCircle({cx, cy}, r, 1.0f, knobAccent.withAlpha(0.32f));

    const float sa = kPi * 0.75f;
    const float ea = sa + value * kPi * 1.5f;
    for (int i = 0; i < 34; ++i) {
        const float a1 = sa + (ea - sa) * static_cast<float>(i) / 34.0f;
        const float a2 = sa + (ea - sa) * static_cast<float>(i + 1) / 34.0f;
        renderer.drawLine({cx + std::cos(a1) * (r - 3.0f), cy + std::sin(a1) * (r - 3.0f)},
                          {cx + std::cos(a2) * (r - 3.0f), cy + std::sin(a2) * (r - 3.0f)}, 3.0f,
                          knobAccent.withAlpha(k.readOnly ? 0.50f : 0.92f));
    }

    const float pa = sa + value * kPi * 1.5f;
    renderer.fillCircle({cx + std::cos(pa) * (r - 7.0f), cy + std::sin(pa) * (r - 7.0f)}, 3.2f, knobAccent);
    renderer.drawText(k.label, {bounds.x + 10.0f, bounds.y + 6.5f}, 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.90f));
    const std::string valueStr = formattedValue(k.paramId);
    renderer.drawText(valueStr, {bounds.x + 10.0f, bounds.bottom() - 18.0f}, 10.0f, knobAccent.withAlpha(0.96f));
}

void AestraDelayEditor::drawMixSlider(NUIRenderer& renderer, float wx, float wy) {
    auto& theme = NUIThemeManager::getInstance();
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const float mix = m_instance ? m_instance->getParameter(Delay::kMix) : 0.0f;
    NUIRect mixRect = offsetRect(m_mixSliderRect, wx, wy);
    const NUIRect track(mixRect.x + 58.0f, mixRect.y + 12.0f, mixRect.width - 104.0f, 8.0f);
    renderer.fillRoundedRect(mixRect, 10.0f, NUIColor(0.068f, 0.068f, 0.068f, 0.92f));
    renderer.strokeRoundedRect(mixRect, 10.0f, 1.0f, accent().withAlpha(0.35f));
    renderer.drawText("Mix", {mixRect.x + 14.0f, mixRect.y + 10.0f}, 10.5f,
                      theme.getColor("textPrimary").withAlpha(0.95f));
    renderer.fillRoundedRect(track, 3.0f, NUIColor(1, 1, 1, 0.10f));
    renderer.fillRoundedRect({track.x, track.y, track.width * mix, track.height}, 3.0f, accent().withAlpha(0.92f));
    renderer.fillCircle({track.x + track.width * mix, track.center().y}, 9.0f, theme.getColor("textPrimary"));
    const std::string pctStr = std::to_string(static_cast<int>(std::round(mix * 100.0f))) + "%";
    const float pctW = renderer.measureText(pctStr, 10.0f).width;
    renderer.drawText(pctStr, {mixRect.right() - 14.0f - pctW, mixRect.y + 10.0f}, 10.0f, accent().withAlpha(0.96f));
}

void AestraDelayEditor::drawBypassPill(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    const bool bypassed = m_instance && m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kBypass) > 0.5f;
    constexpr float kFont = 10.0f;
    if (bypassed) {
        renderer.fillRoundedRect(m_bypassRect, 7.0f,
                                 NUIColor(0.92f, 0.28f, 0.22f).withAlpha(m_bypassHovered ? 0.94f : 0.78f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, NUIColor(0.92f, 0.28f, 0.22f).withAlpha(0.50f));
        renderer.drawTextCentered("BYPASSED", m_bypassRect, kFont, theme.getColor("textPrimary"));
    } else {
        renderer.fillRoundedRect(m_bypassRect, 7.0f,
                                 theme.getColor("success").withAlpha(m_bypassHovered ? 0.30f : 0.18f));
        renderer.strokeRoundedRect(m_bypassRect, 7.0f, 1.0f, theme.getColor("success").withAlpha(0.40f));
        renderer.drawTextCentered("ACTIVE", m_bypassRect, kFont, theme.getColor("success"));
    }
}

void AestraDelayEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    // Recompute layout each frame so cached rects (and absolute slider bounds)
    // follow window movement.
    layoutControls();
    const auto b = getBounds();
    const float wx = b.x;
    const float wy = b.y;
    auto& theme = NUIThemeManager::getInstance();
    const float identityY = wy + AestraPanelWindow::TITLE_BAR_H + 8.0f;
    renderer.fillCircle({wx + kPad + 10.0f, identityY + 9.0f}, 9.0f, accent().withAlpha(0.18f));
    renderer.strokeCircle({wx + kPad + 10.0f, identityY + 9.0f}, 9.0f, 1.5f, accent().withAlpha(0.80f));
    renderer.fillCircle({wx + kPad + 12.5f, identityY + 6.5f}, 2.5f, cyanAccent());
    renderer.drawText("DELAY", {wx + kPad + 28.0f, identityY + 1.0f}, 13.0f,
                      theme.getColor("textPrimary").withAlpha(0.96f));
    renderer.drawText("STEREO ECHO ENGINE", {wx + kPad + 28.0f, identityY + 16.0f}, 8.5f,
                      theme.getColor("textSecondary").withAlpha(0.72f));
    drawBypassPill(renderer);
    drawPillSwitches(renderer, wx, wy);
    drawSectionFrame(renderer, m_timingSectionRect, "TIME + FEEDBACK", cyanAccent(), wx, wy);
    drawSectionFrame(renderer, m_characterSectionRect, "TONE + MOTION", accent(), wx, wy);
    drawEchoDisplay(renderer, wx, wy);
    drawSyncPanel(renderer, wx, wy);

    using Delay = Aestra::Audio::Plugins::AestraDelay;
    const bool sync = m_instance && m_instance->getParameter(Delay::kSyncMode) > 0.5f;
    for (size_t i = 0; i < m_knobs.size(); ++i) {
        // Hide Time knob (index 0) in sync mode
        if (sync && i == 0)
            continue;
        drawKnob(renderer, m_knobs[i], wx, wy);
    }
    drawMixSlider(renderer, wx, wy);
}

int AestraDelayEditor::hitTestBaseButton(float localX, float localY) const {
    for (size_t i = 0; i < m_baseButtons.size(); ++i) {
        if (m_baseButtons[i].bounds.contains({localX, localY}))
            return static_cast<int>(i);
    }
    return -1;
}

int AestraDelayEditor::hitTestModifierButton(float localX, float localY) const {
    for (size_t i = 0; i < m_modifierButtons.size(); ++i) {
        if (m_modifierButtons[i].bounds.contains({localX, localY}))
            return static_cast<int>(i);
    }
    return -1;
}

int AestraDelayEditor::computeDivisionIndex(int baseIdx, int modifierIdx) const {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    static const int kTable[4][3] = {
        {Delay::kDiv1_4, Delay::kDiv1_4D, Delay::kDiv1_4T},
        {Delay::kDiv1_8, Delay::kDiv1_8D, Delay::kDiv1_8T},
        {Delay::kDiv1_16, Delay::kDiv1_16D, Delay::kDiv1_16T},
        {Delay::kDiv1_32, Delay::kDiv1_32, Delay::kDiv1_32},
    };
    return kTable[baseIdx][modifierIdx];
}

void AestraDelayEditor::divisionIndexToBaseModifier(int divisionIdx, int& baseIdx, int& modifierIdx) const {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    switch (divisionIdx) {
    case Delay::kDiv1_4:
        baseIdx = 0;
        modifierIdx = 0;
        break;
    case Delay::kDiv1_8:
        baseIdx = 1;
        modifierIdx = 0;
        break;
    case Delay::kDiv1_16:
        baseIdx = 2;
        modifierIdx = 0;
        break;
    case Delay::kDiv1_32:
        baseIdx = 3;
        modifierIdx = 0;
        break;
    case Delay::kDiv1_4D:
        baseIdx = 0;
        modifierIdx = 1;
        break;
    case Delay::kDiv1_8D:
        baseIdx = 1;
        modifierIdx = 1;
        break;
    case Delay::kDiv1_16D:
        baseIdx = 2;
        modifierIdx = 1;
        break;
    case Delay::kDiv1_4T:
        baseIdx = 0;
        modifierIdx = 2;
        break;
    case Delay::kDiv1_8T:
        baseIdx = 1;
        modifierIdx = 2;
        break;
    case Delay::kDiv1_16T:
        baseIdx = 2;
        modifierIdx = 2;
        break;
    default:
        baseIdx = 1;
        modifierIdx = 0;
        break;
    }
}

void AestraDelayEditor::applySyncSelection() {
    using Delay = Aestra::Audio::Plugins::AestraDelay;
    if (!m_instance)
        return;
    const int divIdx = computeDivisionIndex(m_syncBaseIndex, m_syncModifierIndex);
    m_instance->setParameter(Delay::kNoteDivision, Delay::noteDivisionParamFromIndex(divIdx));
    setDirty(true);
}

bool AestraDelayEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible())
        return false;

    // Let base class handle title bar / close / drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    const auto b = getBounds();
    const float mx = event.position.x - b.x;
    const float my = event.position.y - b.y;
    const bool contains = b.contains(event.position);

    // Bypass click (uses absolute bounds, already offset in layoutControls)
    if (event.pressed && event.button == NUIMouseButton::Left && m_bypassRect.contains(event.position)) {
        if (m_instance) {
            const bool bypassed = m_instance->getParameter(Aestra::Audio::Plugins::AestraDelay::kBypass) > 0.5f;
            m_instance->setParameter(Aestra::Audio::Plugins::AestraDelay::kBypass, bypassed ? 0.0f : 1.0f);
            setDirty(true);
        }
        return true;
    }

    if (!contains && !isDraggingWindow() && !m_draggingMix)
        return false;

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
                    if (base == 3)
                        m_syncModifierIndex = 0;
                    applySyncSelection();
                    return true;
                }
                const int mod = hitTestModifierButton(mx, my);
                if (mod >= 0 && mod != m_syncModifierIndex) {
                    // Prevent dotted/triplet on 1/32 base
                    if (m_syncBaseIndex == 3 && mod != 0)
                        return true;
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
            for (auto& btn : m_baseButtons)
                btn.hovered = false;
            for (auto& btn : m_modifierButtons)
                btn.hovered = false;
            setDirty(true);
        }
    }

    // Hover for bypass pill
    if (!event.pressed && !event.released) {
        const bool hover = m_bypassRect.contains(event.position);
        if (hover != m_bypassHovered) {
            m_bypassHovered = hover;
            setDirty(true);
        }
    }

    // Let NUISlider children handle their own mouse events
    return NUIComponent::onMouseEvent(event) || consumeInsideBounds(event);
}

} // namespace AestraUI
