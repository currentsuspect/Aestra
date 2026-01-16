// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "GenericPluginEditor.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace AestraUI {

GenericPluginEditor::GenericPluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(instance) {
    
    setId("GenericPluginEditor");
    buildParameterWidgets();
    
    // Fixed reasonable size
    setSize(WINDOW_WIDTH, WINDOW_HEIGHT);
}

GenericPluginEditor::~GenericPluginEditor() {
}

void GenericPluginEditor::buildParameterWidgets() {
    if (!m_instance) return;
    
    m_parameters.clear();
    
    auto allParams = m_instance->getParameters();
    printf("[GenericPluginEditor] Plugin has %zu total parameters\n", allParams.size());
    
    for (const auto& info : allParams) {
        printf("[GenericPluginEditor] Param: %s (readOnly=%d, bypass=%d)\n", 
               info.name.c_str(), info.isReadOnly, info.isBypass);
        
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
    
    printf("[GenericPluginEditor] Created %zu editable parameter widgets\n", m_parameters.size());
    
    layoutParameters();
}

void GenericPluginEditor::layoutParameters() {
    float y = TITLE_BAR_HEIGHT + PADDING - m_scrollOffset;
    
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
        
        y += PARAMETER_HEIGHT;
    }
}

void GenericPluginEditor::onResize() {
    layoutParameters();
}

void GenericPluginEditor::onRender(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    // Window background
    renderer.fillRect(bounds, theme.getColor("surfaceSecondary"));
    
    // Draw title bar
    drawTitleBar(renderer);
    
    // Content area clipping (use local coordinates - start at 0,0)
    NUIRect contentArea(0, TITLE_BAR_HEIGHT, bounds.width, bounds.height - TITLE_BAR_HEIGHT);
    renderer.setClipRect(NUIRect(bounds.x, bounds.y + TITLE_BAR_HEIGHT, 
                                 bounds.width, bounds.height - TITLE_BAR_HEIGHT));
    
    // Check if we have parameters
    if (m_parameters.empty()) {
        // Display "No parameters" message
        std::string msg = "No editable parameters";
        renderer.drawText(msg, 
                         {bounds.x + bounds.width * 0.5f - 60, 
                          bounds.y + TITLE_BAR_HEIGHT + (bounds.height - TITLE_BAR_HEIGHT) * 0.5f}, 
                         11.0f, theme.getColor("textSecondary"));
    } else {
        // Only draw visible parameters (using local coordinate comparison)
        float viewportTop = contentArea.y;
        float viewportBottom = contentArea.y + contentArea.height;
        
        for (size_t i = 0; i < m_parameters.size(); ++i) {
            const auto& p = m_parameters[i];
            
            // Skip if parameter is outside viewport (both in local coords now)
            if (p.labelBounds.y + PARAMETER_HEIGHT < viewportTop || 
                p.labelBounds.y > viewportBottom) {
                continue;
            }
            
            bool hovered = (static_cast<int>(i) == m_hoveredParameter);
            drawParameter(renderer, p, hovered);
        }
    }
    
    renderer.clearClipRect();
    
    // Border
    renderer.strokeRect(bounds, 1.0f, theme.getColor("border"));
}

void GenericPluginEditor::drawTitleBar(NUIRenderer& renderer) {
    auto bounds = getBounds();
    auto& theme = NUIThemeManager::getInstance();
    
    NUIRect titleBar(bounds.x, bounds.y, bounds.width, TITLE_BAR_HEIGHT);
    
    // Title bar background
    renderer.fillRect(titleBar, theme.getColor("surfaceDefault"));
    
    // Plugin name
    std::string title = m_instance ? m_instance->getInfo().name : "Plugin Editor";
    renderer.drawText(title, {titleBar.x + PADDING, titleBar.y + 10}, 12.0f, theme.getColor("textPrimary"));
    
    // Close button
    float closeSize = 16.0f;
    float closeX = titleBar.right() - closeSize - 8.0f;
    float closeY = titleBar.y + (TITLE_BAR_HEIGHT - closeSize) * 0.5f;
    NUIRect closeBtn(closeX, closeY, closeSize, closeSize);
    
    renderer.fillRect(closeBtn, theme.getColor("accentDanger").withAlpha(0.8f));
    
    // Draw X
    float pad = 4.0f;
    renderer.drawLine({closeX + pad, closeY + pad}, 
                     {closeX + closeSize - pad, closeY + closeSize - pad},
                     1.5f, theme.getColor("textPrimary"));
    renderer.drawLine({closeX + closeSize - pad, closeY + pad}, 
                     {closeX + pad, closeY + closeSize - pad},
                     1.5f, theme.getColor("textPrimary"));
    
    // Separator
    renderer.drawLine({titleBar.x, titleBar.bottom()}, 
                     {titleBar.right(), titleBar.bottom()},
                     1.0f, theme.getColor("border"));
}

void GenericPluginEditor::drawParameter(NUIRenderer& renderer, const ParameterWidget& p, bool hovered) {
    auto& theme = NUIThemeManager::getInstance();
    auto bounds = getBounds();
    
    // Convert local coords to global for rendering
    float offsetX = bounds.x;
    float offsetY = bounds.y;
    
    // Draw label
    renderer.drawText(p.shortName, 
                     {offsetX + p.labelBounds.x, offsetY + p.labelBounds.y + 8}, 
                     10.0f, theme.getColor("textPrimary"));
    
    // Draw slider track background (darker for contrast)
    NUIRect track(offsetX + p.sliderBounds.x, offsetY + p.sliderBounds.y, 
                  p.sliderBounds.width, p.sliderBounds.height);
    renderer.fillRect(track, NUIColor(0.1f, 0.1f, 0.1f, 1.0f)); // Dark gray
    renderer.strokeRect(track, 1.0f, theme.getColor("border")); // Border for definition
    
    // Draw slider fill
    float fillWidth = track.width * p.normalizedValue;
    if (fillWidth > 0) {
        NUIRect fillRect(track.x, track.y, fillWidth, track.height);
        
        if (hovered || p.isDragging) {
            renderer.fillRect(fillRect, theme.getColor("accentPrimary"));
        } else {
            renderer.fillRect(fillRect, theme.getColor("accentPrimary").withAlpha(0.7f));
        }
    }
    
    // Draw slider thumb (more prominent)
    if (fillWidth > 2) {
        float thumbX = track.x + fillWidth - 3;
        NUIRect thumbRect(thumbX, track.y - 2, 6, track.height + 4);
        renderer.fillRect(thumbRect, theme.getColor("textPrimary"));
        renderer.strokeRect(thumbRect, 1.0f, NUIColor(0.0f, 0.0f, 0.0f, 0.5f)); // Subtle outline
    }
    
    // Draw value text
    std::string valueStr = formatParameterValue(p);
    renderer.drawText(valueStr, 
                     {offsetX + p.valueBounds.x, offsetY + p.valueBounds.y + 8}, 
                     9.0f, theme.getColor("textSecondary"));
}

bool GenericPluginEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    
    auto bounds = getBounds();
    
    // Close if clicked outside window
    if (event.pressed && event.button == NUIMouseButton::Left && !bounds.contains(event.position)) {
        if (m_onClose) m_onClose();
        return false;
    }
    
    if (!bounds.contains(event.position)) {
        return false;
    }
    
    // Check close button
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (hitTestCloseButton(event.position.x, event.position.y)) {
            if (m_onClose) m_onClose();
            return true;
        }
    }
    
    // Window dragging
    if (event.button == NUIMouseButton::Left) {
        if (event.pressed && hitTestTitleBar(event.position.x, event.position.y)) {
            m_isDraggingWindow = true;
            m_dragStartPos = event.position;
            m_windowStartPos = NUIPoint(bounds.x, bounds.y);
            return true;
        } else if (!event.pressed) {
            m_isDraggingWindow = false;
        }
    }
    
    if (m_isDraggingWindow) {
        float dx = event.position.x - m_dragStartPos.x;
        float dy = event.position.y - m_dragStartPos.y;
        setBounds(m_windowStartPos.x + dx, m_windowStartPos.y + dy, bounds.width, bounds.height);
        return true;
    }
    
    // Mouse wheel scrolling
    if (event.wheelDelta != 0) {
        m_scrollOffset -= event.wheelDelta * 15.0f;
        
        // Clamp scroll
        float contentHeight = m_parameters.size() * PARAMETER_HEIGHT + PADDING * 2;
        float viewHeight = bounds.height - TITLE_BAR_HEIGHT;
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

bool GenericPluginEditor::hitTestCloseButton(float x, float y) {
    auto bounds = getBounds();
    float closeSize = 16.0f;
    float closeX = bounds.right() - closeSize - 8.0f;
    float closeY = bounds.y + (TITLE_BAR_HEIGHT - closeSize) * 0.5f;
    NUIRect closeBtn(closeX, closeY, closeSize, closeSize);
    return closeBtn.contains(NUIPoint(x, y));
}

bool GenericPluginEditor::hitTestTitleBar(float x, float y) {
    auto bounds = getBounds();
    NUIRect titleBar(bounds.x, bounds.y, bounds.width - 32.0f, TITLE_BAR_HEIGHT); // Exclude close button area
    return titleBar.contains(NUIPoint(x, y));
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
