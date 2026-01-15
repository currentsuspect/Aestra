// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file PerformanceHUD.h
 * @brief F12-toggleable performance overlay
 */

#pragma once

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadCore/include/NomadProfiler.h"
#include <memory>

namespace Nomad {
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
class PerformanceHUD : public NomadUI::NUIComponent {
public:
    PerformanceHUD();
    ~PerformanceHUD() override = default;
    
    // Toggle visibility (F12 key)
    void toggle();
    
    // Optional: attach AudioEngine for RT health telemetry readout (UI thread only).
    void setAudioEngine(Nomad::Audio::AudioEngine* engine) { m_audioEngine = engine; }

    // Update stats
    void update();
    
    // NUIComponent overrides
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onUpdate(double deltaTime) override;
    
private:
    void renderBackground(NomadUI::NUIRenderer& renderer);
    void renderStats(NomadUI::NUIRenderer& renderer);
    void renderGraph(NomadUI::NUIRenderer& renderer);
    
    Profiler& m_profiler;
    Nomad::Audio::AudioEngine* m_audioEngine{nullptr};
    
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

} // namespace Nomad
