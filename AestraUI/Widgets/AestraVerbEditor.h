// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once
#include "NUIComponent.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {

class AestraVerbEditor : public NUIComponent {
public:
    explicit AestraVerbEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize() { layoutControls(); }
    void setOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

private:
    struct Knob {
        std::string label; uint32_t paramId = 0; float value = 0.5f;
        NUIRect bounds, knobRect;
        bool dragging = false, hovered = false;
        float dragStartY = 0, dragStartValue = 0;
    };
    struct ModeButton {
        std::string label;
        int mode = 0;
        NUIRect bounds;
        bool hovered = false;
    };
    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const Knob& k, NUIColor accent);
    void drawModeSelector(NUIRenderer& renderer, NUIColor accent);
    void drawMixSlider(NUIRenderer& renderer, NUIColor accent);
    void drawSectionLabels(NUIRenderer& renderer);
    int hitTestKnob(float x, float y) const;
    int hitTestMode(float x, float y) const;
    bool hitTestMix(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateKnobValue(int idx, float v);
    void updateParameter(uint32_t paramId, float v);
    float getParamValue(uint32_t paramId) const;
    std::string formatParameterValue(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::array<ModeButton, 3> m_modes;
    NUIRect m_mixBounds;
    NUIRect m_mixTrack;
    std::function<void()> m_onClose;
    int m_hoveredKnob = -1;
    int m_hoveredMode = -1;
    bool m_isDraggingWindow = false;
    bool m_draggingMix = false;
    NUIPoint m_dragStartPos, m_windowStartPos;
    static constexpr float kWinW = 560, kWinH = 420, kTitleH = 74, kPad = 26, kRadius = 18;
    static constexpr float kKnobSize = 46, kKnobGap = 12;
};

} // namespace AestraUI
