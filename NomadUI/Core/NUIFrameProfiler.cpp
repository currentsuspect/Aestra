#include "NUIFrameProfiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace NomadUI {

NUIFrameProfiler::NUIFrameProfiler()
{
    m_history.reserve(MAX_HISTORY);
    reset();
}

void NUIFrameProfiler::beginFrame()
{
    if (!m_enabled) return;
    
    m_frameStart = Clock::now();
}

void NUIFrameProfiler::markRenderEnd()
{
    if (!m_enabled) return;
    
    m_renderEnd = Clock::now();
}

void NUIFrameProfiler::markSwapEnd()
{
    if (!m_enabled) return;
    
    m_swapEnd = Clock::now();
}

void NUIFrameProfiler::endFrame()
{
    if (!m_enabled) return;
    
    auto frameEnd = Clock::now();
    
    // Calculate timings
    FrameSample sample;
    sample.renderTimeMs = toMilliseconds(m_frameStart, m_renderEnd);
    sample.swapTimeMs = toMilliseconds(m_renderEnd, m_swapEnd);
    
    // Sleep time = total - render - swap
    double totalMs = toMilliseconds(m_frameStart, frameEnd);
    sample.sleepTimeMs = totalMs - sample.renderTimeMs - sample.swapTimeMs;
    
    // Total frame time (from last frame end to this frame end)
    if (m_stats.frameCount > 0) {
        sample.totalTimeMs = toMilliseconds(m_lastFrameEnd, frameEnd);
    } else {
        sample.totalTimeMs = totalMs;
    }
    
    // Calculate FPS
    if (sample.totalTimeMs > 0.0) {
        sample.fps = 1000.0 / sample.totalTimeMs;
    } else {
        sample.fps = 0.0;
    }
    
    // Update stats
    updateStats(sample);
    
    // Store in history (circular buffer)
    if (m_history.size() < MAX_HISTORY) {
        m_history.push_back(sample);
    } else {
        m_history[m_historyIndex] = sample;
    }
    m_historyIndex = (m_historyIndex + 1) % MAX_HISTORY;
    
    m_lastFrameEnd = frameEnd;
    m_stats.frameCount++;
}

void NUIFrameProfiler::reset()
{
    m_stats = Stats{};
    m_stats.minFPS = 999999.0;
    m_stats.maxFPS = 0.0;
    m_stats.minFrameMs = 999999.0;
    m_stats.maxFrameMs = 0.0;
    
    m_history.clear();
    m_historyIndex = 0;
    
    m_lastFrameEnd = Clock::now();
}

void NUIFrameProfiler::updateStats(const FrameSample& sample)
{
    m_stats.current = sample;
    
    if (m_stats.frameCount == 0) {
        // First frame - initialize averages
        m_stats.avgRenderMs = sample.renderTimeMs;
        m_stats.avgSwapMs = sample.swapTimeMs;
        m_stats.avgSleepMs = sample.sleepTimeMs;
        m_stats.avgTotalMs = sample.totalTimeMs;
        m_stats.avgFPS = sample.fps;
    } else {
        // Exponential moving average
        m_stats.avgRenderMs = m_stats.avgRenderMs * (1.0 - SMOOTHING) + sample.renderTimeMs * SMOOTHING;
        m_stats.avgSwapMs = m_stats.avgSwapMs * (1.0 - SMOOTHING) + sample.swapTimeMs * SMOOTHING;
        m_stats.avgSleepMs = m_stats.avgSleepMs * (1.0 - SMOOTHING) + sample.sleepTimeMs * SMOOTHING;
        m_stats.avgTotalMs = m_stats.avgTotalMs * (1.0 - SMOOTHING) + sample.totalTimeMs * SMOOTHING;
        m_stats.avgFPS = m_stats.avgFPS * (1.0 - SMOOTHING) + sample.fps * SMOOTHING;
    }
    
    // Update min/max
    m_stats.minFPS = std::min(m_stats.minFPS, sample.fps);
    m_stats.maxFPS = std::max(m_stats.maxFPS, sample.fps);
    m_stats.minFrameMs = std::min(m_stats.minFrameMs, sample.totalTimeMs);
    m_stats.maxFrameMs = std::max(m_stats.maxFrameMs, sample.totalTimeMs);
}

double NUIFrameProfiler::toMilliseconds(const TimePoint& start, const TimePoint& end) const
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void NUIFrameProfiler::printStats() const
{
    if (!m_enabled || m_stats.frameCount == 0) return;
    
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║           NOMAD FRAME PROFILER - Frame #" 
              << std::setw(9) << m_stats.frameCount << "          ║\n";
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    
    // Current frame
    std::cout << "║ CURRENT FRAME:                                             ║\n";
    std::cout << "║   Render Time:  " << std::setw(8) << std::fixed << std::setprecision(2) 
              << m_stats.current.renderTimeMs << " ms                               ║\n";
    std::cout << "║   Swap Time:    " << std::setw(8) << m_stats.current.swapTimeMs 
              << " ms  ";
    if (m_stats.current.swapTimeMs > 20.0) std::cout << "⚠️ VSync stall!        ║\n";
    else std::cout << "                      ║\n";
    
    std::cout << "║   Sleep Time:   " << std::setw(8) << m_stats.current.sleepTimeMs 
              << " ms                               ║\n";
    std::cout << "║   Total Time:   " << std::setw(8) << m_stats.current.totalTimeMs 
              << " ms                               ║\n";
    std::cout << "║   FPS:          " << std::setw(8) << std::setprecision(1) 
              << m_stats.current.fps << "                                    ║\n";
    
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    
    // Averages
    std::cout << "║ AVERAGES (smoothed):                                       ║\n";
    std::cout << "║   Render:       " << std::setw(8) << std::setprecision(2) 
              << m_stats.avgRenderMs << " ms                               ║\n";
    std::cout << "║   Swap:         " << std::setw(8) << m_stats.avgSwapMs 
              << " ms                               ║\n";
    std::cout << "║   Sleep:        " << std::setw(8) << m_stats.avgSleepMs 
              << " ms                               ║\n";
    std::cout << "║   Total:        " << std::setw(8) << m_stats.avgTotalMs 
              << " ms                               ║\n";
    std::cout << "║   FPS:          " << std::setw(8) << std::setprecision(1) 
              << m_stats.avgFPS << "                                    ║\n";
    
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    
    // Min/Max
    std::cout << "║ RANGE (last " << std::min(m_history.size(), MAX_HISTORY) << " frames):                                     ║\n";
    std::cout << "║   FPS:          " << std::setw(8) << std::setprecision(1) 
              << m_stats.minFPS << " - " << std::setw(8) << m_stats.maxFPS << "                    ║\n";
    std::cout << "║   Frame Time:   " << std::setw(8) << std::setprecision(2) 
              << m_stats.minFrameMs << " - " << std::setw(8) << m_stats.maxFrameMs << " ms                ║\n";
    
    std::cout << "╠════════════════════════════════════════════════════════════╣\n";
    
    // Breakdown percentages
    double totalWork = m_stats.avgRenderMs + m_stats.avgSwapMs + m_stats.avgSleepMs;
    if (totalWork > 0.0) {
        double renderPct = (m_stats.avgRenderMs / totalWork) * 100.0;
        double swapPct = (m_stats.avgSwapMs / totalWork) * 100.0;
        double sleepPct = (m_stats.avgSleepMs / totalWork) * 100.0;
        
        std::cout << "║ BREAKDOWN:                                                 ║\n";
        std::cout << "║   Render:       " << std::setw(5) << std::setprecision(1) 
                  << renderPct << "%                                       ║\n";
        std::cout << "║   Swap:         " << std::setw(5) << swapPct 
                  << "%                                       ║\n";
        std::cout << "║   Sleep:        " << std::setw(5) << sleepPct 
                  << "%                                       ║\n";
    }
    
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    // Diagnostics
    if (m_stats.avgSwapMs > 20.0) {
        std::cout << "⚠️  WARNING: Swap time is very high (" << m_stats.avgSwapMs 
                  << "ms) - likely VSync stall!\n";
        std::cout << "    Try disabling VSync to test if it's GPU-bound.\n\n";
    }
    
    if (m_stats.avgRenderMs > 50.0) {
        std::cout << "⚠️  WARNING: Render time is very high (" << m_stats.avgRenderMs 
                  << "ms) - CPU bottleneck!\n";
        std::cout << "    Consider optimizing draw calls or enabling batching.\n\n";
    }
    
    if (m_stats.avgSleepMs < 0.0) {
        std::cout << "⚠️  WARNING: Negative sleep time detected - frame pacing issues!\n";
        std::cout << "    Your render+swap exceeds target frame time.\n\n";
    }
}

} // namespace NomadUI
