// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"
#include "PluginHost.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace AestraUI {

/**
 * @brief Generic auto-generated parameter UI for plugins
 *
 * Creates a windowed parameter slider interface for any plugin.
 * Inherits unified panel chrome from AestraPanelWindow.
 */
class GenericPluginEditor : public AestraPanelWindow {
public:
    GenericPluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance);
    ~GenericPluginEditor() override;

    void drawContent(NUIRenderer& renderer, const NUIRect& contentRect) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    using AestraPanelWindow::onResize;
    void onResize();

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
    
    std::shared_ptr<Aestra::Audio::IPluginInstance> m_instance;
    std::vector<ParameterWidget> m_parameters;
    float m_scrollOffset = 0.0f;
    int m_hoveredParameter = -1;

    // Layout constants
    static constexpr float PARAMETER_HEIGHT = 36.0f;
    static constexpr float LABEL_WIDTH = 132.0f;
    static constexpr float SLIDER_WIDTH = 220.0f;
    static constexpr float VALUE_WIDTH = 64.0f;
    static constexpr float PADDING = 12.0f;
    static constexpr float WINDOW_WIDTH = 468.0f;
    static constexpr float WINDOW_HEIGHT = 420.0f;

    void buildParameterWidgets();
    void layoutParameters();
    void drawParameter(NUIRenderer& renderer, const ParameterWidget& p, bool hovered);
    int hitTestParameter(float x, float y);
    void updateParameterValue(int paramIndex, float normalizedValue);
    std::string formatParameterValue(const ParameterWidget& p);
};

} // namespace AestraUI
