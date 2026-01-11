// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadUnifiedProfiler.h
 * @brief Unified performance profiler for NOMAD - Consolidates multiple profiler systems
 * 
 * Features:
 * - Zone timing macros (NOMAD_ZONE) - replaces ScopedTimer
 * - Frame timing with render/swap/sleep breakdown
 * - Chrome Trace format export with threading support
 * - Audio engine telemetry integration
 * - Performance alerts and regression detection
 * - Memory and GPU profiling hooks
 * - HTML report generation
 * - F12-toggleable HUD overlay
 * - Zero overhead when disabled
 */

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <array>
#include <fstream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <queue>

namespace Nomad {

// Forward declarations
namespace Audio {
    class AudioEngine;
    struct AudioTelemetry;
}

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
 * @brief Performance alert types
 */
enum class PerformanceAlert {
    FrameTimeSpike,
    HighAudioLoad,
    MemoryPressure,
    GPUBottleneck,
    AudioXrun,
    PerformanceRegression
};

/**
 * @brief Performance alert data
 */
struct PerformanceAlertData {
    PerformanceAlert type;
    std::string message;
    double value;
    double threshold;
    std::chrono::steady_clock::time_point timestamp;
};

/**
 * @brief Memory profiling data
 */
struct MemoryStats {
    size_t currentBytes{0};
    size_t peakBytes{0};
    size_t allocationCount{0};
    size_t deallocationCount{0};
    double averageAllocationSize{0.0};
};

/**
 * @brief GPU profiling data (placeholder for future OpenGL timers)
 */
struct GPUStats {
    double drawCallTimeMs{0.0};
    double bufferUploadTimeMs{0.0};
    double shaderCompileTimeMs{0.0};
    size_t totalDrawCalls{0};
    size_t totalTriangles{0};
};

/**
 * @brief Thread-specific profiling
 */
struct ThreadStats {
    uint32_t threadId{0};
    std::string threadName;
    double cpuTimeMs{0.0};
    size_t zoneCount{0};
    std::vector<double> zoneTimes; // Recent zone times for this thread
};

/**
 * @brief Advanced frame timing statistics
 */
struct AdvancedFrameStats {
    // Basic timing
    double cpuTimeMs{0.0};          // CPU logic + render prep
    double gpuTimeMs{0.0};          // GPU draw time (from OpenGL timers)
    double totalTimeMs{0.0};        // Total frame time
    
    // Breakdown timing (from NUIFrameProfiler)
    double renderTimeMs{0.0};       // Time spent in render calls
    double swapTimeMs{0.0};         // Time spent in swapBuffers (VSync)
    double sleepTimeMs{0.0};        // Time spent sleeping
    
    // Audio metrics
    double audioLoadPercent{0.0};   // Audio thread load
    uint32_t audioXruns{0};         // Audio buffer underruns
    
    // Rendering metrics
    uint32_t drawCalls{0};          // OpenGL draw calls
    uint32_t widgetCount{0};        // Active widgets
    uint32_t triangles{0};          // Rendered triangles
    
    // Memory and performance
    MemoryStats memory;
    GPUStats gpu;
    
    // Threading
    std::unordered_map<uint32_t, ThreadStats> threadStats;
    
    // Absolute timestamps for JSON export
    uint64_t frameStartUs{0};
    
    // Performance alerts for this frame
    std::vector<PerformanceAlertData> alerts;
    
    // Per-zone timings (microseconds)
    double uiUpdateUs{0.0};
    double renderPrepUs{0.0};
    double gpuSubmitUs{0.0};
    double inputPollUs{0.0};
};

/**
 * @brief Zone timing entry with threading support
 */
struct UnifiedZoneEntry {
    const char* name{nullptr};
    uint64_t startUs{0};
    uint64_t endUs{0};
    uint32_t threadId{0};
    std::string threadName;
    uint64_t durationUs{0};
    
    // For nested zones
    uint32_t parentZoneId{0};
    uint32_t zoneId{0};
    static std::atomic<uint32_t> s_nextZoneId;
};

/**
 * @brief Performance regression detection
 */
struct PerformanceRegression {
    double currentAvg{0.0};
    double baselineAvg{0.0};
    double regressionPercent{0.0};
    std::string metricName;
    std::chrono::steady_clock::time_point detectedAt;
};

/**
 * @brief Unified profiler singleton
 */
class UnifiedProfiler {
public:
    static UnifiedProfiler& getInstance();
    
    // Zone timing (replaces old NomadProfiler)
    void beginZone(const char* name, uint32_t threadId = 0, const char* threadName = nullptr);
    void endZone(const char* name);
    
    // Frame markers
    void beginFrame();
    void endFrame();
    
    // UI-specific timing markers (from NUIFrameProfiler)
    void markRenderEnd();
    void markSwapEnd();
    
    // Stats recording
    void recordDrawCall();
    void recordTriangles(uint32_t count);
    void setWidgetCount(uint32_t count);
    void setAudioLoad(double percent);
    
    // Memory profiling
    void recordMemoryAllocation(size_t bytes);
    void recordMemoryDeallocation(size_t bytes);
    void recordMemoryPeak(size_t bytes);
    
    // GPU profiling hooks (for future OpenGL timer integration)
    void recordGPUDrawCall(double timeMs);
    void recordGPUBufferUpload(double timeMs);
    
    // Audio engine integration
    void setAudioEngine(Audio::AudioEngine* engine);
    void syncAudioTelemetry();
    
    // Query methods
    const AdvancedFrameStats& getCurrentFrame() const { return m_currentFrame; }
    const AdvancedFrameStats& getAverageStats() const { return m_averageStats; }
    double getFPS() const { return m_fps; }
    
    // History (extended capacity)
    const std::vector<AdvancedFrameStats>& getHistory() const { return m_history; }
    size_t getHistorySize() const { return m_history.size(); }
    
    // Performance alerts
    bool hasActiveAlerts() const { return !m_activeAlerts.empty(); }
    std::vector<PerformanceAlertData> getActiveAlerts() const;
    void clearAlerts();
    
    // Regression detection
    std::vector<PerformanceRegression> getRegressions() const { return m_regressions; }
    void setPerformanceBaseline(const std::string& metricName, double baselineValue);
    
    // Export and reporting
    void exportToJSON(const std::string& filepath);
    void exportToHTML(const std::string& filepath);
    void exportPerformanceReport(const std::string& filepath);
    
    // Enable/disable
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    
    // Configuration
    void setAlertThresholds(double frameTimeMs, double audioLoadPercent);
    void enableRegressionDetection(bool enabled) { m_regressionDetectionEnabled = enabled; }
    
    // Statistics printing (replaces NUIFrameProfiler console output)
    void printFrameStats() const;
    void printPerformanceSummary() const;
    
private:
    UnifiedProfiler();
    ~UnifiedProfiler() = default;
    UnifiedProfiler(const UnifiedProfiler&) = delete;
    UnifiedProfiler& operator=(const UnifiedProfiler&) = delete;
    
    void updateAverages();
    void updatePerformanceAlerts();
    void detectPerformanceRegressions();
    uint64_t getMicroseconds() const;
    double toMilliseconds(uint64_t startUs, uint64_t endUs) const;
    
    // Zone stack management
    struct ZoneStackEntry {
        UnifiedZoneEntry zone;
        std::chrono::steady_clock::time_point startTime;
    };
    void pushZone(const char* name, uint32_t threadId, const char* threadName);
    void popZone(const char* name);
    
    // Thread management
    ThreadStats& getOrCreateThreadStats(uint32_t threadId, const char* threadName);
    std::string getCurrentThreadName() const;
    uint32_t getCurrentThreadId() const;
    
    std::atomic<bool> m_enabled{true};
    
    // Current frame
    AdvancedFrameStats m_currentFrame;
    std::chrono::steady_clock::time_point m_frameStart;
    std::chrono::steady_clock::time_point m_renderEnd;
    std::chrono::steady_clock::time_point m_swapEnd;
    std::chrono::steady_clock::time_point m_lastFrameEnd;
    
    // Zone stack (for nested zones)
    std::vector<ZoneStackEntry> m_zoneStack;
    
    // Thread-specific data
    std::unordered_map<uint32_t, ThreadStats> m_threadStats;
    
    // History (extended from 300 to 600 frames)
    static constexpr size_t HISTORY_SIZE = 600;
    std::vector<AdvancedFrameStats> m_history;
    size_t m_historyIndex{0};
    
    // Running averages
    AdvancedFrameStats m_averageStats;
    double m_fps{60.0};
    
    // Frame counter
    uint64_t m_frameCount{0};
    std::chrono::steady_clock::time_point m_fpsTimer;
    uint32_t m_fpsFrameCount{0};
    
    // Audio integration
    Audio::AudioEngine* m_audioEngine{nullptr};
    
    // Performance monitoring
    std::vector<PerformanceAlertData> m_activeAlerts;
    std::vector<PerformanceRegression> m_regressions;
    bool m_regressionDetectionEnabled{true};
    
    // Performance thresholds
    struct AlertThresholds {
        double frameTimeMs{16.7};     // 60 FPS threshold
        double audioLoadPercent{80.0}; // Audio load threshold
        double memoryGrowthMB{100.0};  // Memory growth threshold
    } m_thresholds;
    
    // Performance baselines for regression detection
    std::unordered_map<std::string, double> m_baselines;
    
    // Export metadata
    struct ExportMetadata {
        std::string buildInfo;
        std::string systemInfo;
        std::chrono::steady_clock::time_point exportTime;
        uint64_t totalFrames{0};
    } m_exportMetadata;
};

} // namespace Nomad

// Macro for easy zone timing (enhanced from original)
#ifdef NOMAD_ENABLE_PROFILING
    #define NOMAD_ZONE(name) Nomad::ScopedTimer __nomad_zone_##__LINE__(name)
    #define NOMAD_ZONE_THREAD(name, threadName) \
        Nomad::UnifiedProfiler::getInstance().beginZone(name, 0, threadName); \
        Nomad::ScopedTimer __nomad_zone_thread_##__LINE__(name)
#else
    #define NOMAD_ZONE(name) ((void)0)
    #define NOMAD_ZONE_THREAD(name, threadName) ((void)0)
#endif

// Memory profiling macros
#ifdef NOMAD_ENABLE_MEMORY_PROFILING
    #define NOMAD_MEMORY_ALLOC(size) Nomad::UnifiedProfiler::getInstance().recordMemoryAllocation(size)
    #define NOMAD_MEMORY_FREE(size) Nomad::UnifiedProfiler::getInstance().recordMemoryDeallocation(size)
#else
    #define NOMAD_MEMORY_ALLOC(size) ((void)0)
    #define NOMAD_MEMORY_FREE(size) ((void)0)
#endif