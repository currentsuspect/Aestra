// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIComponent.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraCompEditor : public NUIComponent {
public:
    explicit AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize() { layoutControls(); }
    void setOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

private:
    struct Knob {
        std::string label;
        uint32_t paramId = 0;
        float value = 0.5f;
        NUIRect bounds;
        NUIRect knobRect;
        bool dragging = false;
        bool hovered = false;
        float dragStartY = 0.0f;
        float dragStartValue = 0.0f;
    };

    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawModeSwitcher(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const Knob& k);
    void drawGainReductionMeter(NUIRenderer& renderer, const NUIRect& bounds);
    void drawHorizontalMeter(NUIRenderer& renderer, const NUIRect& bounds, const std::string& label, float level);
    int hitTestKnob(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateKnobValue(int idx, float normalizedValue);
    std::string formattedValue(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::function<void()> m_onClose;
    int m_hoveredKnob = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;
    NUIRect m_peakModeRect;
    NUIRect m_rmsModeRect;
    NUIRect m_grMeterRect;

    static constexpr float kWinW = 560.0f;
    static constexpr float kWinH = 390.0f;
    static constexpr float kTitleH = 58.0f;
    static constexpr float kPad = 22.0f;
    static constexpr float kRadius = 16.0f;
    static constexpr float kKnobSize = 62.0f;
};

} // namespace AestraUI
