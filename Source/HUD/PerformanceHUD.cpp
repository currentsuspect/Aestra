// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file PerformanceHUD.cpp
 * @brief Performance HUD implementation
 */

#include "PerformanceHUD.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadAudio/include/AudioEngine.h"
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
    
    // Dynamically position in top-right corner like FPS monitor
    // Stack below FPS display (FPS display is 120px tall + 10px margin + 10px gap = 140px offset)
    if (getParent()) {
        auto parentBounds = getParent()->getBounds();
        float x = parentBounds.width - HUD_WIDTH - 10.0f; // HUD_WIDTH + 10px margin
        float y = 140.0f; // Below FPS display (120px height + 10px margin + 10px gap)
        setBounds(NUIRect(x, y, HUD_WIDTH, HUD_HEIGHT));
    }
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

    // Engine health (RT telemetry snapshots; UI thread only)
    if (m_audioEngine) {
        const auto& tel = m_audioEngine->telemetry();

        const uint64_t xruns = tel.xruns.load(std::memory_order_relaxed);
        const uint64_t underruns = tel.underruns.load(std::memory_order_relaxed);
        const uint64_t cbMaxNs = tel.maxCallbackNs.load(std::memory_order_relaxed);
        const uint64_t blocks = tel.blocksProcessed.load(std::memory_order_relaxed);
        const uint64_t srcBlocks = tel.srcActiveBlocks.load(std::memory_order_relaxed);
        const uint32_t lastFrames = tel.lastBufferFrames.load(std::memory_order_relaxed);
        const uint32_t lastSR = tel.lastSampleRate.load(std::memory_order_relaxed);

        const uint64_t qDrops = m_audioEngine->commandQueue().droppedCount();
        const uint32_t qMax = m_audioEngine->commandQueue().maxDepth();
        const uint32_t qCap = Nomad::Audio::AudioCommandQueue::capacity();

        const double cbMaxMs = static_cast<double>(cbMaxNs) / 1e6;
        const double budgetMs = (lastSR > 0) ? (static_cast<double>(lastFrames) * 1000.0 / static_cast<double>(lastSR)) : 0.0;
        const double srcPct = (blocks > 0) ? (100.0 * static_cast<double>(srcBlocks) / static_cast<double>(blocks)) : 0.0;

        // Status: red on xruns/drops; yellow if close to budget; green otherwise.
        const bool hasTiming = (cbMaxNs > 0 && budgetMs > 0.0);
        const bool closeToBudget = hasTiming && (cbMaxMs >= 0.8 * budgetMs);
        const bool warnQueue = (qCap > 0) && (static_cast<double>(qMax) / static_cast<double>(qCap) >= 0.8);

        const char* status = "🟢";
        NUIColor statusColor = theme.getColor("success");
        if (xruns > 0 || underruns > 0 || qDrops > 0) {
            status = "🔴";
            statusColor = theme.getColor("error");
        } else if (closeToBudget || warnQueue || !hasTiming) {
            status = "🟡";
            statusColor = theme.getColor("warning");
        }

        {
            std::ostringstream oss;
            oss << "Engine: " << status
                << "  XRuns: " << xruns
                << "  Qmax: " << qMax << "/" << qCap;
            renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, statusColor);
            y += lineHeight;
        }

        {
            std::ostringstream oss;
            oss << "CBmax: ";
            if (hasTiming) {
                oss << std::fixed << std::setprecision(3) << cbMaxMs << "ms"
                    << " / " << std::setprecision(3) << budgetMs << "ms";
            } else {
                oss << "n/a";
            }
            oss << "  SRC: " << std::fixed << std::setprecision(1) << srcPct << "%";
            renderer.drawText(oss.str(), NUIPoint(x, y), fontSize, textColor);
            y += lineHeight;
        }
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
