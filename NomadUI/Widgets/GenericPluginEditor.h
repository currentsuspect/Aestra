// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/NUIComponent.h"
#include "../Core/NUITypes.h"
#include <PluginHost.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace NomadUI {

/**
 * @brief Generic auto-generated parameter UI for plugins
 * 
 * Creates a windowed parameter slider interface for any plugin.
 * Includes title bar, close button, and dragging support.
 */
class GenericPluginEditor : public NUIComponent {
public:
    GenericPluginEditor(std::shared_ptr<Nomad::Audio::IPluginInstance> instance);
    ~GenericPluginEditor() override;
   
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onResize();
    
    // Callback when close button is clicked
    void setOnClose(std::function<void()> callback) { m_onClose = callback; }
    
private:
    struct ParameterWidget {
        uint32_t parameterId;
        std::string name;
        std::string shortName;
        float normalizedValue; // 0.0 to 1.0
        float minValue;
        float maxValue;
        uint32_t stepCount; // 0 = continuous
        NUIRect sliderBounds;
        NUIRect labelBounds;
        NUIRect valueBounds;
        bool isDragging = false;
        float dragStartX = 0.0f;
        float dragStartValue = 0.0f;
    };
    
    std::shared_ptr<Nomad::Audio::IPluginInstance> m_instance;
    std::vector<ParameterWidget> m_parameters;
    float m_scrollOffset = 0.0f;
    int m_hoveredParameter = -1;
    std::function<void()> m_onClose;
    
    // Window dragging
    bool m_isDraggingWindow = false;
    NUIPoint m_dragStartPos;
    NUIPoint m_windowStartPos;
    
    // Layout constants
    static constexpr float TITLE_BAR_HEIGHT = 32.0f;
    static constexpr float PARAMETER_HEIGHT = 28.0f;
    static constexpr float LABEL_WIDTH = 120.0f;
    static constexpr float SLIDER_WIDTH = 180.0f;
    static constexpr float VALUE_WIDTH = 50.0f;
    static constexpr float PADDING = 8.0f;
    static constexpr float WINDOW_WIDTH = 400.0f;
    static constexpr float WINDOW_HEIGHT = 400.0f;
    
    void buildParameterWidgets();
    void layoutParameters();
    void drawTitleBar(NUIRenderer& renderer);
    void drawParameter(NUIRenderer& renderer, const ParameterWidget& p, bool hovered);
    int hitTestParameter(float x, float y);
    bool hitTestCloseButton(float x, float y);
    bool hitTestTitleBar(float x, float y);
    void updateParameterValue(int paramIndex, float normalizedValue);
    std::string formatParameterValue(const ParameterWidget& p);
};

} // namespace NomadUI
