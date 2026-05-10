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

class AestraCompEditor : public NUIComponent {
public:
    explicit AestraCompEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);

    void onRender(NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    using NUIComponent::onResize;
    void onResize() { layoutControls(); }
    void onResize(int width, int height) override;
    void setOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

private:
    struct Control {
        std::string label;
        uint32_t paramId = 0;
        float value = 0.0f;
        NUIRect bounds;
        NUIRect knobRect;
        bool dragging = false;
        bool hovered = false;
        float dragStartY = 0.0f;
        float dragStartValue = 0.0f;
    };

    void buildControls();
    void layoutControls();
    void syncControlsFromPlugin();
    void drawTitleBar(NUIRenderer& renderer);
    void drawBypassButton(NUIRenderer& renderer);
    void drawControl(NUIRenderer& renderer, const Control& control);
    void drawGainReductionMeter(NUIRenderer& renderer);
    void drawLevelMeter(NUIRenderer& renderer, const NUIRect& bounds, const std::string& label, float smoothedLevel);
    void drawCloseButton(NUIRenderer& renderer);
    int hitTestControl(float x, float y) const;
    bool hitTestCloseButton(float x, float y) const;
    bool hitTestTitleBar(float x, float y) const;
    void updateControlValue(int idx, float normalizedValue);
    void setBypassed(bool bypassed);
    bool isBypassed() const;
    std::string valueText(uint32_t paramId) const;

    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<Control> m_controls;
    std::function<void()> m_onClose;

    NUIRect m_grMeterRect;
    NUIRect m_inputMeterRect;
    NUIRect m_outputMeterRect;
    NUIRect m_bypassRect;

    int m_hoveredControl = -1;
    bool m_bypassHovered = false;
    bool m_closeHovered = false;
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;

    float m_grDisplayDb = 0.0f;
    float m_inputDisplay = 0.0f;
    float m_outputDisplay = 0.0f;
    double m_meterTimer = 0.0;

    static constexpr float kWinW = 680.0f;
    static constexpr float kWinH = 508.0f;
    static constexpr float kTitleH = 58.0f;
    static constexpr float kPad = 20.0f;
    static constexpr float kRadius = 12.0f;
    static constexpr float kKnobSizePrimary = 64.0f;
    static constexpr float kKnobSizeSecondary = 48.0f;
};

} // namespace AestraUI
