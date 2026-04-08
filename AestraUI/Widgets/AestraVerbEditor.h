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
    void buildControls();
    void layoutControls();
    void drawTitleBar(NUIRenderer& renderer);
    void drawKnob(NUIRenderer& renderer, const Knob& k, NUIColor accent);
    int hitTestKnob(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateKnobValue(int idx, float v);

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Knob> m_knobs;
    std::function<void()> m_onClose;
    int m_hoveredKnob = -1;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos, m_windowStartPos;
    static constexpr float kWinW = 470, kWinH = 220, kTitleH = 42, kPad = 18, kRadius = 12;
    static constexpr float kKnobSize = 56, kKnobGap = 20;
};

} // namespace AestraUI
