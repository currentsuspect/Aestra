#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <array>

namespace NomadUI {

/**
 * Micro profiler for frame timing analysis.
 * 
 * Measures and tracks:
 * - Render time (CPU work)
 * - Swap time (GPU sync/VSync)
 * - Sleep time (frame pacing)
 * - Total frame time
 * - FPS (actual vs target)
 * 
 * Usage:
 *   profiler.beginFrame();
 *   // ... render work ...
 *   profiler.markRenderEnd();
 *   swapBuffers();
 *   profiler.markSwapEnd();
 *   // ... sleep ...
 *   profiler.endFrame();
 */
class NUIFrameProfiler {
public:
    struct FrameSample {
        double renderTimeMs;    // Time spent in render calls
        double swapTimeMs;      // Time spent in swapBuffers (VSync)
        double sleepTimeMs;     // Time spent sleeping
        double totalTimeMs;     // Total frame time
        double fps;             // Instantaneous FPS
    };

    struct Stats {
        // Current frame
        FrameSample current;
        
        // Smoothed averages (exponential moving average)
        double avgRenderMs;
        double avgSwapMs;
        double avgSleepMs;
        double avgTotalMs;
        double avgFPS;
        
        // Min/Max tracking (last 100 frames)
        double minFPS;
        double maxFPS;
        double minFrameMs;
        double maxFrameMs;
        
        // Frame counter
        uint64_t frameCount;
    };

    NUIFrameProfiler();

    // Frame timing markers
    void beginFrame();
    void markRenderEnd();
    void markSwapEnd();
    void endFrame();

    // Query stats
    const Stats& getStats() const { return m_stats; }
    
    // Get historical samples (last N frames)
    const std::vector<FrameSample>& getHistory() const { return m_history; }
    
    // Enable/disable profiling
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    // Reset stats
    void reset();
    
    // Print current stats to console
    void printStats() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::high_resolution_clock::time_point;

    bool m_enabled = true;
    
    // Timing markers
    TimePoint m_frameStart;
    TimePoint m_renderEnd;
    TimePoint m_swapEnd;
    TimePoint m_lastFrameEnd;
    
    // Stats tracking
    Stats m_stats;
    
    // Historical data (circular buffer, last 100 frames)
    std::vector<FrameSample> m_history;
    size_t m_historyIndex = 0;
    static constexpr size_t MAX_HISTORY = 100;
    
    // Smoothing factor for exponential moving average
    static constexpr double SMOOTHING = 0.1; // 10% new, 90% old
    
    void updateStats(const FrameSample& sample);
    double toMilliseconds(const TimePoint& start, const TimePoint& end) const;
};

} // namespace NomadUI
