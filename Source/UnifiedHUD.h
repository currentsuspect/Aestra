// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIAdaptiveFPS.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadUnifiedProfiler.h"
#include "../NomadAudio/include/AudioEngine.h"
#include "../NomadAudio/include/AudioCommandQueue.h"
#include <memory>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <array>

/**
 * @brief Rocket Engine Profiler - Professional DAW Performance HUD
 * 
 * Ultimate profiling overlay featuring:
 * - Real-time FPS and frame timing with sparklines
 * - Audio callback budget visualization
 * - Zone hotspots (top 5 slowest code zones)
 * - Memory tracking
 * - Collapsible sections
 * - Dual-layer performance graph
 */
class UnifiedHUD : public NomadUI::NUIComponent {
public:
    static constexpr float HUD_WIDTH = 360.0f;
    static constexpr float HUD_HEIGHT = 400.0f;
    static constexpr float PADDING = 10.0f;
    static constexpr size_t GRAPH_SAMPLES = 90;
    static constexpr size_t SPARKLINE_SAMPLES = 20;
    static constexpr float GRAPH_HEIGHT = 55.0f;
    static constexpr float SECTION_SPACING = 6.0f;
    static constexpr float LINE_HEIGHT = 13.0f;

    UnifiedHUD(NomadUI::NUIAdaptiveFPS* adaptiveFPS)
        : m_adaptiveFPS(adaptiveFPS)
        , m_visible(false)
        , m_audioEngine(nullptr)
    {
        m_frameTimeGraph.resize(GRAPH_SAMPLES, 0.0f);
        m_audioCallbackGraph.resize(GRAPH_SAMPLES, 0.0f);
        m_fpsSparkline.fill(0.0f);
        m_audioLoadSparkline.fill(0.0f);
        setBounds(NomadUI::NUIRect(0, 0, HUD_WIDTH, HUD_HEIGHT));
    }

    void setAudioEngine(Nomad::Audio::AudioEngine* engine) {
        m_audioEngine = engine;
        Nomad::UnifiedProfiler::getInstance().setAudioEngine(engine);
    }

    void toggle() { m_visible = !m_visible; }
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    void onUpdate(double deltaTime) override {
        if (!m_visible) return;
        
        // Get real frame time from NUIAdaptiveFPS
        float actualFPS = 0.0f;
        float actualFrameTime = 0.0f;
        if (m_adaptiveFPS) {
            auto stats = m_adaptiveFPS->getStats();
            actualFPS = static_cast<float>(stats.actualFPS);
            actualFrameTime = static_cast<float>(stats.averageFrameTime * 1000.0);
        }
        
        // Update frame time graph
        m_frameTimeGraph[m_graphIndex] = actualFrameTime;
        
        // Update FPS sparkline
        m_fpsSparkline[m_sparklineIndex] = actualFPS;
        
        // Update audio callback graph and sparkline
        float audioLoad = 0.0f;
        if (m_audioEngine) {
            const auto& tel = m_audioEngine->telemetry();
            float callbackMs = static_cast<float>(tel.getLastCallbackNs()) / 1e6f;
            m_audioCallbackGraph[m_graphIndex] = callbackMs;
            
            // Calculate load percentage
            uint32_t bufFrames = tel.getLastBufferFrames();
            uint32_t sampleRate = tel.getLastSampleRate();
            if (sampleRate > 0 && bufFrames > 0) {
                float budgetMs = static_cast<float>(bufFrames) * 1000.0f / static_cast<float>(sampleRate);
                audioLoad = (callbackMs / budgetMs) * 100.0f;
            }
            
            // Track max callback for WCET
            float maxCallbackMs = static_cast<float>(tel.getMaxCallbackNs()) / 1e6f;
            if (maxCallbackMs > m_wcetMs) {
                m_wcetMs = maxCallbackMs;
            }
        }
        m_audioLoadSparkline[m_sparklineIndex] = std::min(audioLoad, 100.0f);
        
        // Update zone hotspots
        updateZoneHotspots();
        
        m_graphIndex = (m_graphIndex + 1) % GRAPH_SAMPLES;
        m_sparklineIndex = (m_sparklineIndex + 1) % SPARKLINE_SAMPLES;
        
        // Position in top-right corner
        if (getParent()) {
            auto parentBounds = getParent()->getBounds();
            float x = parentBounds.width - HUD_WIDTH - 10.0f;
            float y = 35.0f;
            setBounds(NomadUI::NUIRect(x, y, HUD_WIDTH, HUD_HEIGHT));
        }
    }

    void onRender(NomadUI::NUIRenderer& renderer) override {
        if (!m_visible) return;
        
        renderBackground(renderer);
        
        float y = getBounds().y + PADDING;
        
        renderTitle(renderer, y);
        renderFrameStats(renderer, y);
        
        if (m_audioEngine) {
            renderAudioSection(renderer, y);
        }
        
        renderZoneHotspots(renderer, y);
        renderRenderingStats(renderer, y);
        renderGraph(renderer);
    }

private:
    NomadUI::NUIAdaptiveFPS* m_adaptiveFPS;
    Nomad::Audio::AudioEngine* m_audioEngine;
    bool m_visible;
    
    // Graphs
    std::vector<float> m_frameTimeGraph;
    std::vector<float> m_audioCallbackGraph;
    size_t m_graphIndex{0};
    
    // Sparklines
    std::array<float, SPARKLINE_SAMPLES> m_fpsSparkline;
    std::array<float, SPARKLINE_SAMPLES> m_audioLoadSparkline;
    size_t m_sparklineIndex{0};
    
    // WCET tracking
    float m_wcetMs{0.0f};
    
    // Zone hotspots
    struct ZoneHotspot {
        std::string name;
        float timeMs{0.0f};
        float percentage{0.0f};
    };
    std::array<ZoneHotspot, 10> m_zoneHotspots;

    void updateZoneHotspots() {
        // Get zone timings from profiler - use LAST COMPLETED frame, not current (which is reset)
        const auto& stats = Nomad::UnifiedProfiler::getInstance().getLastCompletedFrame();
        
        // Aggregate zones by name from the zones vector
        std::unordered_map<std::string, float> zoneAggregate;
        
        for (const auto& zone : stats.zones) {
            if (zone.name) {
                zoneAggregate[zone.name] += static_cast<float>(zone.durationUs) / 1000.0f;
            }
        }
        
        // Convert to sorted list
        std::vector<ZoneHotspot> zones;
        for (const auto& [name, timeMs] : zoneAggregate) {
            if (timeMs > 0.001f) {  // Filter tiny zones
                zones.push_back({name, timeMs, 0.0f});
            }
        }
        
        // Sort by time descending
        std::sort(zones.begin(), zones.end(), [](const ZoneHotspot& a, const ZoneHotspot& b) {
            return a.timeMs > b.timeMs;
        });
        
        // Calculate total (of top zones only for percentage)
        float totalMs = 0.0f;
        for (size_t i = 0; i < std::min(zones.size(), m_zoneHotspots.size()); ++i) {
            totalMs += zones[i].timeMs;
        }
        
        // Fill hotspots array with top 5
        for (size_t i = 0; i < m_zoneHotspots.size(); ++i) {
            if (i < zones.size()) {
                m_zoneHotspots[i] = zones[i];
                m_zoneHotspots[i].percentage = (totalMs > 0.0f) ? (zones[i].timeMs / totalMs) * 100.0f : 0.0f;
            } else {
                m_zoneHotspots[i] = ZoneHotspot{};
            }
        }
    }

    void renderBackground(NomadUI::NUIRenderer& renderer) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        
        NomadUI::NUIColor bgColor(0.02f, 0.02f, 0.05f, 0.94f);
        renderer.fillRoundedRect(getBounds(), 8.0f, bgColor);
        
        NomadUI::NUIColor borderColor = theme.getColor("accentPrimary").withAlpha(0.6f);
        renderer.strokeRoundedRect(getBounds(), 8.0f, 1.5f, borderColor);
    }

    void renderTitle(NomadUI::NUIRenderer& renderer, float& y) {
        float x = getBounds().x + PADDING;
        
        // Rocket emoji effect with gradient title
        NomadUI::NUIColor titleColor(0.3f, 0.9f, 1.0f, 1.0f);
        renderer.drawText("🚀 PERFORMANCE MONITOR", NomadUI::NUIPoint(x, y), 11.0f, titleColor);
        y += 16.0f;
    }

    void renderSparkline(NomadUI::NUIRenderer& renderer, float x, float y, float width, float height,
                         const std::array<float, SPARKLINE_SAMPLES>& data, float maxVal, NomadUI::NUIColor color) {
        if (maxVal <= 0.0f) return;
        
        float barWidth = width / SPARKLINE_SAMPLES;
        
        for (size_t i = 0; i < SPARKLINE_SAMPLES; ++i) {
            size_t idx = (m_sparklineIndex + i + 1) % SPARKLINE_SAMPLES;
            float val = std::min(data[idx] / maxVal, 1.0f);
            float barHeight = val * height;
            
            float barX = x + i * barWidth;
            float barY = y + height - barHeight;
            
            renderer.fillRect(NomadUI::NUIRect(barX, barY, barWidth - 1.0f, barHeight), color.withAlpha(0.6f));
        }
    }

    void renderFrameStats(NomadUI::NUIRenderer& renderer, float& y) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        float x = getBounds().x + PADDING;
        float rightX = getBounds().x + HUD_WIDTH - PADDING;
        
        double fps = 0.0, frameTimeMs = 0.0, targetFps = 60.0;
        bool userActive = true, canSustain60 = true;
        
        if (m_adaptiveFPS) {
            auto stats = m_adaptiveFPS->getStats();
            fps = stats.actualFPS;
            frameTimeMs = stats.averageFrameTime * 1000.0;
            targetFps = stats.currentTargetFPS;
            userActive = stats.userActive;
            canSustain60 = stats.canSustain60;
        }
        
        // FPS with color
        NomadUI::NUIColor fpsColor = theme.getColor("success");
        if (fps < 30.0) fpsColor = theme.getColor("error");
        else if (fps < 55.0) fpsColor = theme.getColor("warning");
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << fps << " FPS";
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 14.0f, fpsColor);
        
        // Frame time
        oss.str("");
        oss << std::fixed << std::setprecision(2) << frameTimeMs << " ms";
        NomadUI::NUIColor ftColor = (frameTimeMs > 33.3) ? theme.getColor("error") : 
                                     (frameTimeMs > 16.7) ? theme.getColor("warning") : theme.getColor("textPrimary");
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x + 90, y), 11.0f, ftColor);
        
        // FPS Sparkline
        renderSparkline(renderer, rightX - 70, y, 60, 12, m_fpsSparkline, 70.0f, fpsColor);
        y += LINE_HEIGHT + 2;
        
        // Mode info
        NomadUI::NUIColor labelColor(0.55f, 0.55f, 0.6f, 1.0f);
        oss.str("");
        oss << "Target: " << std::fixed << std::setprecision(0) << targetFps 
            << "  Active: " << (userActive ? "YES" : "NO")
            << "  60OK: " << (canSustain60 ? "YES" : "NO");
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, labelColor);
        
        // Mode
        oss.str("");
        oss << "Mode: ";
        if (m_adaptiveFPS) {
            switch (m_adaptiveFPS->getMode()) {
                case NomadUI::NUIAdaptiveFPS::Mode::Auto: oss << "Auto"; break;
                case NomadUI::NUIAdaptiveFPS::Mode::Locked30: oss << "Locked30"; break;
                case NomadUI::NUIAdaptiveFPS::Mode::Locked60: oss << "Locked60"; break;
            }
        }
        renderer.drawText(oss.str(), NomadUI::NUIPoint(rightX - 70, y), 9.0f, labelColor);
        y += LINE_HEIGHT + SECTION_SPACING;
    }

    void renderAudioSection(NomadUI::NUIRenderer& renderer, float& y) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        float x = getBounds().x + PADDING;
        float rightX = getBounds().x + HUD_WIDTH - PADDING;
        
        const auto& tel = m_audioEngine->telemetry();
        const auto& cmdQueue = m_audioEngine->commandQueue();
        
        uint64_t xruns = tel.getXruns();
        uint64_t underruns = tel.getUnderruns();
        uint64_t lastCbNs = tel.getLastCallbackNs();
        uint64_t maxCbNs = tel.getMaxCallbackNs();
        uint32_t bufFrames = tel.getLastBufferFrames();
        uint32_t sampleRate = tel.getLastSampleRate();
        uint64_t srcBlocks = tel.getSrcActiveBlocks();
        uint64_t blocks = tel.getBlocksProcessed();
        
        uint32_t qMax = cmdQueue.maxDepth();
        uint64_t qDrops = cmdQueue.droppedCount();
        uint32_t qCap = Nomad::Audio::AudioCommandQueue::capacity();
        
        double budgetMs = (sampleRate > 0 && bufFrames > 0) 
            ? (static_cast<double>(bufFrames) * 1000.0 / static_cast<double>(sampleRate)) : 0.0;
        double lastCbMs = static_cast<double>(lastCbNs) / 1e6;
        double maxCbMs = static_cast<double>(maxCbNs) / 1e6;
        double loadPercent = (budgetMs > 0.0) ? (lastCbMs / budgetMs) * 100.0 : 0.0;
        double srcPercent = (blocks > 0) ? (100.0 * srcBlocks / blocks) : 0.0;
        
        // Section header
        NomadUI::NUIColor headerColor(0.3f, 0.75f, 1.0f, 1.0f);
        renderer.drawText("AUDIO ENGINE", NomadUI::NUIPoint(x, y), 10.0f, headerColor);
        
        // Audio load sparkline
        renderSparkline(renderer, rightX - 70, y - 2, 60, 10, m_audioLoadSparkline, 100.0f, 
                        loadPercent > 70 ? theme.getColor("warning") : theme.getColor("success"));
        y += LINE_HEIGHT;
        
        // Load bar
        float barWidth = HUD_WIDTH - PADDING * 2 - 55;
        float barHeight = 6.0f;
        float barX = x + 50;
        
        NomadUI::NUIColor barBg(0.08f, 0.08f, 0.1f, 1.0f);
        renderer.fillRect(NomadUI::NUIRect(barX, y, barWidth, barHeight), barBg);
        
        float fillWidth = std::min(static_cast<float>(loadPercent / 100.0) * barWidth, barWidth);
        NomadUI::NUIColor barFill = (loadPercent > 90) ? theme.getColor("error") :
                                     (loadPercent > 70) ? theme.getColor("warning") : theme.getColor("success");
        renderer.fillRect(NomadUI::NUIRect(barX, y, fillWidth, barHeight), barFill);
        
        std::ostringstream oss;
        oss << "Load: " << std::fixed << std::setprecision(0) << loadPercent << "%";
        NomadUI::NUIColor labelColor(0.6f, 0.6f, 0.65f, 1.0f);
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y - 1), 9.0f, labelColor);
        y += barHeight + 3;
        
        // Callback timing
        oss.str("");
        oss << "CB: " << std::fixed << std::setprecision(3) << lastCbMs << "ms / " 
            << std::setprecision(2) << budgetMs << "ms  WCET: " << std::setprecision(3) << maxCbMs << "ms";
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, labelColor);
        y += LINE_HEIGHT;
        
        // XRuns
        NomadUI::NUIColor xrunColor = (xruns > 0) ? theme.getColor("error") : theme.getColor("success");
        oss.str("");
        oss << "XRuns: " << xruns << "  Underruns: " << underruns 
            << "  Qmax: " << qMax << "/" << qCap << "  SRC: " << std::fixed << std::setprecision(0) << srcPercent << "%";
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, xrunColor);
        y += LINE_HEIGHT;
        
        // Buffer config
        oss.str("");
        oss << "Buffer: " << bufFrames << " @ " << sampleRate << " Hz";
        if (qDrops > 0) oss << "  Drops: " << qDrops;
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, labelColor);
        y += LINE_HEIGHT + SECTION_SPACING;
    }

    void renderZoneHotspots(NomadUI::NUIRenderer& renderer, float& y) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        float x = getBounds().x + PADDING;
        
        NomadUI::NUIColor headerColor(1.0f, 0.6f, 0.2f, 1.0f);
        renderer.drawText("🔥 ZONE HOTSPOTS", NomadUI::NUIPoint(x, y), 10.0f, headerColor);
        y += LINE_HEIGHT;
        
        NomadUI::NUIColor labelColor(0.6f, 0.6f, 0.65f, 1.0f);
        float barMaxWidth = 80.0f;
        
        for (const auto& zone : m_zoneHotspots) {
            if (zone.name.empty() || zone.timeMs < 0.001f) continue;
            
            // Zone name
            std::ostringstream oss;
            oss << zone.name;
            renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, labelColor);
            
            // Time
            oss.str("");
            oss << std::fixed << std::setprecision(2) << zone.timeMs << "ms";
            renderer.drawText(oss.str(), NomadUI::NUIPoint(x + 85, y), 9.0f, theme.getColor("textPrimary"));
            
            // Bar
            float barWidth = (zone.percentage / 100.0f) * barMaxWidth;
            float barX = x + 140;
            float barY = y + 2;
            float barHeight = 6.0f;
            
            NomadUI::NUIColor barBg(0.1f, 0.1f, 0.12f, 1.0f);
            renderer.fillRect(NomadUI::NUIRect(barX, barY, barMaxWidth, barHeight), barBg);
            
            NomadUI::NUIColor barFill = (zone.percentage > 50) ? theme.getColor("error") :
                                         (zone.percentage > 30) ? theme.getColor("warning") : headerColor;
            renderer.fillRect(NomadUI::NUIRect(barX, barY, barWidth, barHeight), barFill);
            
            // Percentage
            oss.str("");
            oss << std::fixed << std::setprecision(0) << zone.percentage << "%";
            renderer.drawText(oss.str(), NomadUI::NUIPoint(barX + barMaxWidth + 5, y), 9.0f, labelColor);
            
            y += LINE_HEIGHT;
        }
        
        y += SECTION_SPACING;
    }

    void renderRenderingStats(NomadUI::NUIRenderer& renderer, float& y) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        float x = getBounds().x + PADDING;
        
        const auto& stats = Nomad::UnifiedProfiler::getInstance().getAverageStats();
        
        NomadUI::NUIColor headerColor(0.4f, 0.9f, 0.5f, 1.0f);
        renderer.drawText("RENDERING", NomadUI::NUIPoint(x, y), 10.0f, headerColor);
        y += LINE_HEIGHT;
        
        NomadUI::NUIColor labelColor(0.6f, 0.6f, 0.65f, 1.0f);
        std::ostringstream oss;
        oss << "Draws: " << stats.drawCalls << "  Widgets: " << stats.widgetCount << "  Tris: " << stats.triangles;
        renderer.drawText(oss.str(), NomadUI::NUIPoint(x, y), 9.0f, labelColor);
        y += LINE_HEIGHT + SECTION_SPACING;
    }

    void renderGraph(NomadUI::NUIRenderer& renderer) {
        auto& theme = NomadUI::NUIThemeManager::getInstance();
        
        float graphY = getBounds().y + getBounds().height - GRAPH_HEIGHT - PADDING;
        NomadUI::NUIRect graphRect(getBounds().x + PADDING, graphY, HUD_WIDTH - PADDING * 2, GRAPH_HEIGHT);
        
        // Background
        NomadUI::NUIColor graphBg(0.01f, 0.01f, 0.03f, 0.85f);
        renderer.fillRect(graphRect, graphBg);
        
        float maxMs = 40.0f;
        
        // Reference lines
        float y60 = graphRect.y + graphRect.height - (16.7f / maxMs) * graphRect.height;
        renderer.drawLine(NomadUI::NUIPoint(graphRect.x, y60), 
                          NomadUI::NUIPoint(graphRect.x + graphRect.width, y60),
                          1.0f, theme.getColor("success").withAlpha(0.25f));
        
        float y30 = graphRect.y + graphRect.height - (33.3f / maxMs) * graphRect.height;
        renderer.drawLine(NomadUI::NUIPoint(graphRect.x, y30), 
                          NomadUI::NUIPoint(graphRect.x + graphRect.width, y30),
                          1.0f, theme.getColor("warning").withAlpha(0.25f));
        
        float barWidth = graphRect.width / GRAPH_SAMPLES;
        
        for (size_t i = 0; i < GRAPH_SAMPLES; ++i) {
            size_t idx = (m_graphIndex + i) % GRAPH_SAMPLES;
            float frameTime = m_frameTimeGraph[idx];
            float audioTime = m_audioCallbackGraph[idx];
            
            float barX = graphRect.x + i * barWidth;
            
            // Frame time bar
            float ftHeight = std::min(frameTime / maxMs, 1.0f) * graphRect.height;
            float ftY = graphRect.y + graphRect.height - ftHeight;
            
            NomadUI::NUIColor ftColor = (frameTime > 33.3f) ? theme.getColor("error").withAlpha(0.7f) :
                                         (frameTime > 16.7f) ? theme.getColor("warning").withAlpha(0.6f) :
                                         theme.getColor("accentCyan").withAlpha(0.5f);
            renderer.fillRect(NomadUI::NUIRect(barX, ftY, barWidth - 1.0f, ftHeight), ftColor);
            
            // Audio callback overlay
            if (audioTime > 0.01f) {
                float atHeight = std::min(audioTime / maxMs, 1.0f) * graphRect.height;
                float atY = graphRect.y + graphRect.height - atHeight;
                renderer.fillRect(NomadUI::NUIRect(barX, atY, barWidth - 1.0f, 2.0f), 
                                  NomadUI::NUIColor(1.0f, 0.3f, 0.5f, 0.9f));
            }
        }
        
        // Border
        renderer.strokeRect(graphRect, 1.0f, theme.getColor("border").withAlpha(0.35f));
        
        // Legend
        float legendY = graphRect.y - 10.0f;
        renderer.drawText("Frame", NomadUI::NUIPoint(graphRect.x, legendY), 8.0f, 
                          theme.getColor("accentCyan").withAlpha(0.7f));
        renderer.drawText("Audio", NomadUI::NUIPoint(graphRect.x + 45, legendY), 8.0f, 
                          NomadUI::NUIColor(1.0f, 0.3f, 0.5f, 0.8f));
        renderer.drawText("16.7ms", NomadUI::NUIPoint(graphRect.x + graphRect.width - 35, y60 - 3), 7.0f, 
                          theme.getColor("success").withAlpha(0.5f));
    }
};
