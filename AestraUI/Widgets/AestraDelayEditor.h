// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraDelayEditor : public NUIComponent {
public:
    explicit AestraDelayEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize() { layoutControls(); }
    void setOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

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

    struct DivisionButton {
        int index = 0;
        std::string label;
        NUIRect bounds;
    };

    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawPillSwitches(NUIRenderer& renderer);
    void drawDivisionGrid(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const Knob& k);
    void drawMixSlider(NUIRenderer& renderer);
    void updateKnobValue(int idx, float v);
    std::string formattedValue(uint32_t paramId) const;
    int hitTestKnob(float x, float y) const;
    int hitTestDivision(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::vector<DivisionButton> m_divisionButtons;
    std::function<void()> m_onClose;

    NUIRect m_freeRect;
    NUIRect m_syncRect;
    NUIRect m_stereoRect;
    NUIRect m_pingPongRect;
    NUIRect m_mixSliderRect;

    int m_hoveredKnob = -1;
    bool m_isDraggingWindow = false;
    bool m_draggingMix = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;

    static constexpr float kWinW = 620.0f;
    static constexpr float kWinH = 360.0f;
    static constexpr float kTitleH = 54.0f;
    static constexpr float kPad = 20.0f;
    static constexpr float kRadius = 15.0f;
    static constexpr float kKnobSize = 58.0f;
};

} // namespace AestraUI
