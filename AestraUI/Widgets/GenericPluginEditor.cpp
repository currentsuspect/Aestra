// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "GenericPluginEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace AestraUI {

GenericPluginEditor::GenericPluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(instance) {

    setId("GenericPluginEditor");
    setEnforceParentBounds(true);
    buildParameterWidgets();

    std::string title = m_instance ? m_instance->getInfo().name : "Plugin Editor";
    setPanelTitle(title);

    // Fixed reasonable size
    setSize(WINDOW_WIDTH, WINDOW_HEIGHT);
}

GenericPluginEditor::~GenericPluginEditor() {
}

void GenericPluginEditor::buildParameterWidgets() {
    if (!m_instance) return;
    
    m_parameters.clear();
    
    auto allParams = m_instance->getParameters();
    
    for (const auto& info : allParams) {
        // Skip read-only or bypass parameters
        if (info.isReadOnly || info.isBypass) continue;
        
        ParameterWidget widget;
        widget.parameterId = info.id;
        widget.name = info.name;
        widget.shortName = info.shortName.empty() ? info.name : info.shortName;
        widget.normalizedValue = m_instance->getParameter(info.id);
        widget.minValue = info.minValue;
        widget.maxValue = info.maxValue;
        widget.stepCount = info.stepCount;
        
        m_parameters.push_back(widget);
    }

    layoutParameters();
}

void GenericPluginEditor::layoutParameters() {
    float y = AestraPanelWindow::TITLE_BAR_H + PADDING - m_scrollOffset;

    for (auto& p : m_parameters) {
        float x = PADDING;

        // Label
        p.labelBounds = NUIRect(x, y, LABEL_WIDTH, PARAMETER_HEIGHT);
        x += LABEL_WIDTH + PADDING;

        // Slider
        p.sliderBounds = NUIRect(x, y + PARAMETER_HEIGHT * 0.35f,
                                  SLIDER_WIDTH, PARAMETER_HEIGHT * 0.3f);
        x += SLIDER_WIDTH + PADDING;

        // Value display
        p.valueBounds = NUIRect(x, y, VALUE_WIDTH, PARAMETER_HEIGHT);

        y += PARAMETER_HEIGHT + 6.0f;
    }
}

void GenericPluginEditor::onResize() {
    layoutParameters();
}

void GenericPluginEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();

    if (m_parameters.empty()) {
        std::string msg = "No editable parameters";
        renderer.drawText(msg,
                         {bounds.x + bounds.width * 0.5f - 60,
                          bounds.y + AestraPanelWindow::TITLE_BAR_H
                              + (bounds.height - AestraPanelWindow::TITLE_BAR_H) * 0.5f},
                         11.0f, theme.getColor("textSecondary").withAlpha(0.5f));
    } else {
        float viewportTop = AestraPanelWindow::TITLE_BAR_H;
        float viewportBottom = viewportTop + contentRect.height;

        for (size_t i = 0; i < m_parameters.size(); ++i) {
            const auto& p = m_parameters[i];

            // Skip if parameter is outside viewport
            if (p.labelBounds.y + PARAMETER_HEIGHT < viewportTop ||
                p.labelBounds.y > viewportBottom) {
                continue;
            }

            bool hovered = (static_cast<int>(i) == m_hoveredParameter);
            drawParameter(renderer, p, hovered);
        }
    }
}

void GenericPluginEditor::drawParameter(NUIRenderer& renderer, const ParameterWidget& p, bool hovered) {
    auto& theme = NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    float offsetX = bounds.x;
    float offsetY = bounds.y;
    
    NUIRect rowRect(offsetX + p.labelBounds.x - 6.0f,
                    offsetY + p.labelBounds.y + 1.0f,
                    LABEL_WIDTH + SLIDER_WIDTH + VALUE_WIDTH + PADDING * 3.0f + 12.0f,
                    PARAMETER_HEIGHT - 2.0f);
    renderer.fillRoundedRect(rowRect, 9.0f,
        hovered || p.isDragging ? NUIColor(0.16f, 0.18f, 0.25f, 0.92f)
                                : NUIColor(0.10f, 0.11f, 0.15f, 0.74f));
    renderer.strokeRoundedRect(rowRect, 9.0f, 1.0f,
        hovered || p.isDragging ? theme.getColor("accentPrimary").withAlpha(0.30f)
                                : NUIColor(1.0f, 1.0f, 1.0f, 0.05f));

    float labelY = offsetY + p.sliderBounds.y + (p.sliderBounds.height * 0.5f) - 5.0f; 
    renderer.drawText(p.shortName, 
                     {offsetX + p.labelBounds.x, labelY},
                     11.0f, theme.getColor("textPrimary").withAlpha(0.90f));

    float trackH = 8.0f; 
    float trackY = offsetY + p.sliderBounds.y + (p.sliderBounds.height - trackH) * 0.5f;
    NUIRect track(offsetX + p.sliderBounds.x, trackY, 
                  p.sliderBounds.width, trackH);

    renderer.fillRoundedRect(track, 3.0f, NUIColor(0.03f, 0.04f, 0.06f, 0.78f));

    float fillWidth = track.width * p.normalizedValue;
    if (fillWidth > 0) {
        NUIRect fillRect(track.x, track.y, fillWidth, track.height);
        NUIColor accent = theme.getColor("accentPrimary");
        
        if (hovered || p.isDragging) {
             renderer.fillRoundedRect(fillRect, 3.0f, accent.withAlpha(0.95f));
             renderer.fillRoundedRect({fillRect.x, fillRect.y - 1, fillRect.width, fillRect.height + 2}, 
                                      4.0f, accent.withAlpha(0.22f));
        } else {
            renderer.fillRoundedRect(fillRect, 3.0f, accent.withAlpha(0.78f));
        }
    }

    float thumbSize = 12.0f;
    float thumbX = track.x + fillWidth - (thumbSize * 0.5f);
    float thumbY = track.center().y - (thumbSize * 0.5f);
    thumbX = std::clamp(thumbX, track.x - thumbSize * 0.5f, track.right() - thumbSize * 0.5f);
    NUIRect thumbRect(thumbX, thumbY, thumbSize, thumbSize);

    if (hovered || p.isDragging) {
         renderer.fillRoundedRect(thumbRect, thumbSize * 0.5f, NUIColor(1.0f, 1.0f, 1.0f, 1.0f));
         renderer.strokeRoundedRect(thumbRect, thumbSize * 0.5f, 1.5f, theme.getColor("accentPrimary"));
    } else {
         float idleSize = 8.0f;
         float offset = (thumbSize - idleSize) * 0.5f;
         NUIRect idleThumb = {thumbRect.x + offset, thumbRect.y + offset, idleSize, idleSize};
         renderer.fillRoundedRect(idleThumb, idleSize * 0.5f, NUIColor(0.9f, 0.9f, 0.9f, 0.9f));
    }

    std::string valueStr = formatParameterValue(p);
    renderer.drawText(valueStr, 
                     {offsetX + p.valueBounds.x, labelY},
                     11.0f, theme.getColor("textPrimary"));
}

bool GenericPluginEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;

    // Let base class handle title bar / close button / window drag first
    if (AestraPanelWindow::onMouseEvent(event)) {
        return true;
    }

    auto bounds = getBounds();
    const bool isDraggingParameter = std::any_of(
        m_parameters.begin(), m_parameters.end(),
        [](const ParameterWidget& p) { return p.isDragging; });

    if (!bounds.contains(event.position) && !isDraggingWindow() && !isDraggingParameter) {
        return false;
    }

    // Mouse wheel scrolling
    if (event.wheelDelta != 0) {
        m_scrollOffset -= event.wheelDelta * 15.0f;

        // Clamp scroll
        float contentHeight = m_parameters.size() * PARAMETER_HEIGHT + PADDING * 2;
        float viewHeight = bounds.height - AestraPanelWindow::TITLE_BAR_H;
        m_scrollOffset = std::clamp(m_scrollOffset, 0.0f, std::max(0.0f, contentHeight - viewHeight));

        layoutParameters();
        repaint();
        return true;
    }

    // Update hover (only check parameters in content area)
    int paramIndex = hitTestParameter(event.position.x, event.position.y);
    if (paramIndex != m_hoveredParameter) {
        m_hoveredParameter = paramIndex;
        repaint();
    }

    // Handle slider dragging
    if (event.button == NUIMouseButton::Left) {
        if (event.pressed && paramIndex >= 0) {
            // Start dragging
            m_parameters[paramIndex].isDragging = true;
            m_parameters[paramIndex].dragStartX = event.position.x;
            m_parameters[paramIndex].dragStartValue = m_parameters[paramIndex].normalizedValue;
            return true;
        } else if (!event.pressed) {
            // Stop all dragging
            for (auto& p : m_parameters) {
                p.isDragging = false;
            }
            return true;
        }
    }

    // Update dragging parameters
    for (size_t i = 0; i < m_parameters.size(); ++i) {
        if (m_parameters[i].isDragging) {
            float deltaX = event.position.x - m_parameters[i].dragStartX;
            float deltaValue = deltaX / SLIDER_WIDTH;
            float newValue = std::clamp(m_parameters[i].dragStartValue + deltaValue, 0.0f, 1.0f);

            updateParameterValue(static_cast<int>(i), newValue);
            repaint();
            return true;
        }
    }

    return true; // Always consume events within window
}

int GenericPluginEditor::hitTestParameter(float x, float y) {
    auto bounds = getBounds();

    // Convert global mouse position to local coordinates
    float localX = x - bounds.x;
    float localY = y - bounds.y;

    for (size_t i = 0; i < m_parameters.size(); ++i) {
        const auto& p = m_parameters[i];
        // Check if mouse is over slider area (both in local coords now)
        if (p.sliderBounds.contains(NUIPoint(localX, localY))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void GenericPluginEditor::updateParameterValue(int paramIndex, float normalizedValue) {
    if (paramIndex < 0 || paramIndex >= static_cast<int>(m_parameters.size())) return;
    
    auto& p = m_parameters[paramIndex];
    
    // Handle stepped parameters
    if (p.stepCount > 0) {
        float stepped = std::round(normalizedValue * p.stepCount) / p.stepCount;
        p.normalizedValue = stepped;
    } else {
        p.normalizedValue = normalizedValue;
    }
    
    // Update plugin parameter
    if (m_instance) {
        m_instance->setParameter(p.parameterId, p.normalizedValue);
    }
}

std::string GenericPluginEditor::formatParameterValue(const ParameterWidget& p) {
    // Try to get display string from plugin first
    if (m_instance) {
        std::string display = m_instance->getParameterDisplay(p.parameterId);
        if (!display.empty()) {
            return display;
        }
    }
    
    // Fallback: format the actual value
    float actualValue = p.minValue + p.normalizedValue * (p.maxValue - p.minValue);
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << actualValue;
    return oss.str();
}

} // namespace AestraUI
