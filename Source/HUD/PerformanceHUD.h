// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file PerformanceHUD.h
 * @brief F12-toggleable performance overlay
 */

#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraCore/include/AestraProfiler.h"
#include <memory>

namespace Aestra {
namespace Audio {
class AudioEngine;
}

/**
 * @brief Performance HUD overlay
 * 
 * Displays:
 * - FPS and frame time
 * - CPU/GPU breakdown
 * - Draw calls and widget count
 * - Audio thread load
 * - Frame time graph
 */
class PerformanceHUD : public AestraUI::NUIComponent {
public:
    PerformanceHUD();
    ~PerformanceHUD() override = default;
    
    // Toggle visibility (F12 key)
    void toggle();
    
    // Optional: attach AudioEngine for RT health telemetry readout (UI thread only).
    void setAudioEngine(Aestra::Audio::AudioEngine* engine) { m_audioEngine = engine; }

    // Update stats
    void update();
    
    // NUIComponent overrides
    void onRender(AestraUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    
private:
    void renderBackground(AestraUI::NUIRenderer& renderer);
    void renderStats(AestraUI::NUIRenderer& renderer);
    void renderGraph(AestraUI::NUIRenderer& renderer);
    
    Profiler& m_profiler;
    Aestra::Audio::AudioEngine* m_audioEngine{nullptr};
    
    // Graph data (rolling buffer of frame times)
    static constexpr size_t GRAPH_SAMPLES = 120; // 2 seconds at 60fps
    std::vector<float> m_frameTimeGraph;
    size_t m_graphIndex{0};
    
    // HUD state
    bool m_showGraph{true};
    bool m_showDetailed{false};
    
    // Position and size
    static constexpr float HUD_WIDTH = 400.0f;
    static constexpr float HUD_HEIGHT = 190.0f;
    static constexpr float GRAPH_HEIGHT = 60.0f;
    static constexpr float PADDING = 8.0f;
};

} // namespace Aestra
