// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraDelayEditor : public AestraPanelWindow {
public:
    explicit AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    using AestraPanelWindow::onResize;
    void onResize() { layoutControls(); }

private:
    struct Knob {
        std::string label;
        uint32_t paramId = 0;
        float value = 0.5f;
        bool readOnly = false;
        NUIRect bounds;
        NUIRect knobRect;
        bool dragging = false;
        bool hovered = false;
        float dragStartY = 0.0f;
        float dragStartValue = 0.0f;
    };

    struct TierButton {
        std::string label;
        NUIRect bounds;
        bool hovered = false;
    };

    void buildControls();
    void layoutControls();
    void drawPillSwitches(NUIRenderer& renderer, float wx, float wy);
    void drawSyncPanel(NUIRenderer& renderer, float wx, float wy);
    void drawKnob(NUIRenderer& renderer, const Knob& k, float wx, float wy);
    void drawMixSlider(NUIRenderer& renderer, float wx, float wy);
    void updateKnobValue(int idx, float v);
    std::string formattedValue(uint32_t paramId) const;
    int hitTestKnob(float localX, float localY) const;
    int hitTestBaseButton(float localX, float localY) const;
    int hitTestModifierButton(float localX, float localY) const;

    int computeDivisionIndex(int baseIdx, int modifierIdx) const;
    void divisionIndexToBaseModifier(int divisionIdx, int& baseIdx, int& modifierIdx) const;
    void applySyncSelection();
    std::string syncReadoutText() const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::vector<TierButton> m_baseButtons;
    std::vector<TierButton> m_modifierButtons;

    NUIRect m_freeRect;
    NUIRect m_syncRect;
    NUIRect m_stereoRect;
    NUIRect m_pingPongRect;
    NUIRect m_mixSliderRect;
    NUIRect m_syncReadoutRect;

    int m_hoveredKnob = -1;
    int m_hoveredBaseButton = -1;
    int m_hoveredModifierButton = -1;
    int m_syncBaseIndex = 1;      // default 1/8
    int m_syncModifierIndex = 0;  // default Straight
    bool m_draggingMix = false;

    static constexpr float kWinW = 620.0f;
    static constexpr float kWinH = 360.0f;
    static constexpr float kPad = 20.0f;
    static constexpr float kRadius = 15.0f;
    static constexpr float kKnobSize = 50.0f;
};

} // namespace AestraUI
