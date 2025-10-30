// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file PerformanceHUD.cpp
 * @brief Performance HUD implementation
 */

#include "PerformanceHUD.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadCore/include/NomadLog.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Nomad {

using namespace NomadUI;

PerformanceHUD::PerformanceHUD()
    : m_profiler(Profiler::getInstance())
{
    m_frameTimeGraph.resize(GRAPH_SAMPLES, 0.0f);
    
    // Position in top-right corner
    setBounds(NUIRect(0, 0, HUD_WIDTH, HUD_HEIGHT));
}

void PerformanceHUD::toggle() {
    setVisible(!isVisible());
    
    if (isVisible()) {
        Log::info("Performance HUD: SHOWN");
    } else {
        Log::info("Performance HUD: HIDDEN");
    }
}

void PerformanceHUD::update() {
    if (!isVisible()) return;
    
    // Update graph
    const auto& stats = m_profiler.getCurrentFrame();
    m_frameTimeGraph[m_graphIndex] = static_cast<float>(stats.totalTimeMs);
    m_graphIndex = (m_graphIndex + 1) % GRAPH_SAMPLES;
}

void PerformanceHUD::onUpdate(double deltaTime) {
    update();
}

void PerformanceHUD::onRender(NUIRenderer& renderer) {
    if (!isVisible()) return;
    
    renderBackground(renderer);
    renderStats(renderer);
    
    if (m_showGraph) {
        renderGraph(renderer);
    }
}

void PerformanceHUD::renderBackground(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    
    // Semi-transparent dark background
    NUIColor bgColor = theme.getColor("backgroundSecondary").withAlpha(0.85f);
    renderer.fillRoundedRect(getBounds(), 6.0f, bgColor);
    
    // Subtle border
    NUIColor borderColor = theme.getColor("border");
    renderer.strokeRoundedRect(getBounds(), 6.0f, 1.0f, borderColor);
}

void PerformanceHUD::renderStats(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    NUIColor textColor = theme.getColor("textPrimary");
    NUIColor accentColor = theme.getColor("accentCyan");
    
    const auto& stats = m_profiler.getAverageStats();
    double fps = m_profiler.getFPS();
    
    float x = getBounds().x + PADDING;
    float y = getBounds().y + PADDING;
    float lineHeight = 18.0f;
    float fontSize = 12.0f;
    
    // FPS and frame time
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << fps << " FPS";
        
        // Color code based on performance
        NUIColor fpsColor = textColor;
        if (fps >= 58.0) {
            fpsColor = theme.getColor("success"); // Green - excellent
        } else if (fps >= 45.0) {
            fpsColor = theme.getColor("warning"); // Yellow - acceptable
        } else {
            fpsColor = theme.getColor("error"); // Red - poor
        }
        
        renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, fpsColor);
        
        oss.str("");
        oss << std::fixed << std::setprecision(2) << stats.totalTimeMs << " ms";
        renderer.drawText(oss.str(), NUIPoint(x + 80, y), fontSize, textColor);
        
        y += lineHeight;
    }
    
    // CPU / GPU breakdown
    {
        std::ostringstream oss;
        oss << "CPU: " << std::fixed << std::setprecision(2) << stats.cpuTimeMs << " ms  "
            << "GPU: " << stats.gpuTimeMs << " ms";
        renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, textColor);
        y += lineHeight;
    }
    
    // Draw calls and widgets
    {
        std::ostringstream oss;
        oss << "Draws: " << stats.drawCalls << "  "
            << "Widgets: " << stats.widgetCount;
        renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, textColor);
        y += lineHeight;
    }
    
    // Audio load
    {
        std::ostringstream oss;
        oss << "Audio: " << std::fixed << std::setprecision(1) << stats.audioLoadPercent << "%";
        
        // Color code audio load
        NUIColor audioColor = textColor;
        if (stats.audioLoadPercent < 70.0) {
            audioColor = theme.getColor("success");
        } else if (stats.audioLoadPercent < 90.0) {
            audioColor = theme.getColor("warning");
        } else {
            audioColor = theme.getColor("error");
        }
        
        renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, audioColor);
        y += lineHeight;
    }
}

void PerformanceHUD::renderGraph(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    
    // Graph area
    float graphY = getBounds().y + getBounds().height - GRAPH_HEIGHT - PADDING;
    NUIRect graphRect(getBounds().x + PADDING, graphY, 
                      getBounds().width - PADDING * 2, GRAPH_HEIGHT);
    
    // Background
    NUIColor graphBg = theme.getColor("backgroundPrimary").withAlpha(0.5f);
    renderer.fillRect(graphRect, graphBg);
    
    // Reference lines (16.7ms for 60fps, 33.3ms for 30fps)
    NUIColor gridColor = theme.getColor("border").withAlpha(0.3f);
    
    float fps60Line = graphRect.y + graphRect.height - (16.7f / 40.0f) * graphRect.height;
    renderer.drawLine(NUIPoint(graphRect.x, fps60Line), 
                      NUIPoint(graphRect.x + graphRect.width, fps60Line), 
                      1.0f, theme.getColor("success").withAlpha(0.5f));
    
    float fps30Line = graphRect.y + graphRect.height - (33.3f / 40.0f) * graphRect.height;
    renderer.drawLine(NUIPoint(graphRect.x, fps30Line), 
                      NUIPoint(graphRect.x + graphRect.width, fps30Line), 
                      1.0f, theme.getColor("warning").withAlpha(0.5f));
    
    // Draw graph
    NUIColor graphColor = theme.getColor("accentCyan");
    float barWidth = graphRect.width / GRAPH_SAMPLES;
    
    for (size_t i = 0; i < GRAPH_SAMPLES; ++i) {
        size_t idx = (m_graphIndex + i) % GRAPH_SAMPLES;
        float frameTime = m_frameTimeGraph[idx];
        
        // Clamp to 40ms max for display
        float normalizedHeight = std::min(frameTime / 40.0f, 1.0f);
        float barHeight = normalizedHeight * graphRect.height;
        
        float barX = graphRect.x + i * barWidth;
        float barY = graphRect.y + graphRect.height - barHeight;
        
        // Color code based on frame time
        NUIColor barColor = graphColor;
        if (frameTime > 33.3f) {
            barColor = theme.getColor("error");
        } else if (frameTime > 16.7f) {
            barColor = theme.getColor("warning");
        } else {
            barColor = theme.getColor("success");
        }
        
        renderer.fillRect(NUIRect(barX, barY, barWidth - 1.0f, barHeight), 
                         barColor.withAlpha(0.7f));
    }
    
    // Border
    renderer.strokeRect(graphRect, 1.0f, theme.getColor("border"));
}

} // namespace Nomad
