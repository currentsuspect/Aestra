// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadProfiler.h
 * @brief Lightweight performance profiler for NOMAD
 * 
 * Features:
 * - Zone timing macros (NOMAD_ZONE)
 * - Ring buffer for last 300 frames
 * - F12-toggleable HUD overlay
 * - JSON export for Chrome Trace Viewer
 * - Zero overhead when disabled
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <array>
#include <fstream>

namespace Nomad {

/**
 * @brief High-precision timer using steady_clock
 */
class ScopedTimer {
public:
    ScopedTimer(const char* name);
    ~ScopedTimer();
    
private:
    const char* m_name;
    std::chrono::steady_clock::time_point m_start;
};

/**
 * @brief Frame timing statistics
 */
// forward declare ZoneEntry so FrameStats can contain a vector of them
struct ZoneEntry;
struct FrameStats {
    double cpuTimeMs{0.0};      // CPU logic + render prep
    double gpuTimeMs{0.0};      // GPU draw time
    double totalTimeMs{0.0};    // Total frame time
    double audioLoadPercent{0.0}; // Audio thread load
    
    uint32_t drawCalls{0};      // OpenGL draw calls
    uint32_t widgetCount{0};    // Active widgets
    uint32_t triangles{0};      // Rendered triangles
    
    // Per-zone timings (microseconds)
    double uiUpdateUs{0.0};
    double renderPrepUs{0.0};
    double gpuSubmitUs{0.0};
    double inputPollUs{0.0};
    
    // Absolute frame start time (microseconds since epoch) - used for JSON export
    uint64_t frameStartUs{0};

    // Per-frame recorded zones (populated when zones end)
    std::vector<ZoneEntry> zones;
};

/**
 * @brief Zone timing entry
 */
struct ZoneEntry {
    const char* name{nullptr};
    uint64_t startUs{0};
    uint64_t endUs{0};
    uint32_t threadId{0};
};

/**
 * @brief Performance profiler singleton
 */
class Profiler {
public:
    static Profiler& getInstance();
    
    // Zone timing
    void beginZone(const char* name);
    void endZone(const char* name);
    
    // Frame markers
    void beginFrame();
    void endFrame();
    
    // Stats recording
    void recordDrawCall();
    void recordTriangles(uint32_t count);
    void setWidgetCount(uint32_t count);
    void setAudioLoad(double percent);
    
    // Query
    const FrameStats& getCurrentFrame() const { return m_currentFrame; }
    const FrameStats& getAverageStats() const { return m_averageStats; }
    double getFPS() const { return m_fps; }
    
    // History
    const std::vector<FrameStats>& getHistory() const { return m_history; }
    
    // Export
    void exportToJSON(const std::string& filepath);
    
    // Enable/disable
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
private:
    Profiler();
    ~Profiler() = default;
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    
    void updateAverages();
    uint64_t getMicroseconds() const;
    
    std::atomic<bool> m_enabled{true};
    
    // Current frame
    FrameStats m_currentFrame;
    std::chrono::steady_clock::time_point m_frameStart;
    std::chrono::steady_clock::time_point m_cpuStart;
    std::chrono::steady_clock::time_point m_gpuStart;
    
    // Zone stack (for nested zones)
    std::vector<ZoneEntry> m_zoneStack;
    
    // History (ring buffer of last 300 frames)
    static constexpr size_t HISTORY_SIZE = 300;
    std::vector<FrameStats> m_history;
    size_t m_historyIndex{0};
    
    // Running averages
    FrameStats m_averageStats;
    double m_fps{60.0};
    
    // Frame counter
    uint64_t m_frameCount{0};
    std::chrono::steady_clock::time_point m_fpsTimer;
    uint32_t m_fpsFrameCount{0};
};

} // namespace Nomad

// Macro for easy zone timing
#ifdef NOMAD_ENABLE_PROFILING
#define NOMAD_ZONE(name) Nomad::ScopedTimer __nomad_zone_##__LINE__(name)
#else
#define NOMAD_ZONE(name) ((void)0)
#endif
