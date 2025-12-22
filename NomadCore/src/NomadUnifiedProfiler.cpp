// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadUnifiedProfiler.cpp
 * @brief Unified performance profiler implementation
 */

#include "NomadUnifiedProfiler.h"
#include "NomadLog.h"
#include <sstream>
#include <iomanip>
#include <thread>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>

namespace Nomad {

// Static member initialization
std::atomic<uint32_t> UnifiedZoneEntry::s_nextZoneId{1};

//==============================================================================
// ScopedTimer Implementation
//==============================================================================

ScopedTimer::ScopedTimer(const char* name)
    : m_name(name)
    , m_start(std::chrono::steady_clock::now())
{
    UnifiedProfiler::getInstance().beginZone(name);
}

ScopedTimer::~ScopedTimer() {
    UnifiedProfiler::getInstance().endZone(m_name);
}

//==============================================================================
// UnifiedProfiler Implementation
//==============================================================================

UnifiedProfiler& UnifiedProfiler::getInstance() {
    static UnifiedProfiler instance;
    return instance;
}

UnifiedProfiler::UnifiedProfiler()
    : m_frameStart(std::chrono::steady_clock::now())
    , m_renderEnd(m_frameStart)
    , m_swapEnd(m_frameStart)
    , m_lastFrameEnd(m_frameStart)
    , m_fpsTimer(m_frameStart)
{
    m_history.reserve(HISTORY_SIZE);
    m_zoneStack.reserve(16); // Typical max nesting depth
    m_threadStats.reserve(8); // Typical thread count
    
    // Initialize export metadata
    m_exportMetadata.buildInfo = "NOMAD-2025-Core";
    m_exportMetadata.exportTime = std::chrono::steady_clock::now();
    
    Log::info("Unified Profiler initialized");
}

void UnifiedProfiler::beginZone(const char* name, uint32_t threadId, const char* threadName) {
    if (!m_enabled) return;
    
    uint32_t actualThreadId = threadId;
    std::string actualThreadName = threadName ? threadName : "";
    
    if (threadId == 0) {
        actualThreadId = getCurrentThreadId();
        if (threadName == nullptr) {
            actualThreadName = getCurrentThreadName();
        }
    }
    
    pushZone(name, actualThreadId, actualThreadName.c_str());
}

void UnifiedProfiler::endZone(const char* name) {
    if (!m_enabled) return;
    
    popZone(name);
}

void UnifiedProfiler::pushZone(const char* name, uint32_t threadId, const char* threadName) {
    UnifiedZoneEntry entry;
    entry.name = name;
    entry.startUs = getMicroseconds();
    entry.threadId = threadId;
    entry.threadName = threadName ? threadName : "";
    entry.zoneId = s_nextZoneId.fetch_add(1);
    
    // Set parent zone ID if we have a current zone
    if (!m_zoneStack.empty()) {
        entry.parentZoneId = m_zoneStack.back().zone.zoneId;
    }
    
    ZoneStackEntry stackEntry;
    stackEntry.zone = entry;
    stackEntry.startTime = std::chrono::steady_clock::now();
    
    m_zoneStack.push_back(stackEntry);
    
    // Update thread stats
    ThreadStats& threadStats = getOrCreateThreadStats(threadId, threadName);
    threadStats.zoneCount++;
}

void UnifiedProfiler::popZone(const char* name) {
    if (m_zoneStack.empty()) return;
    
    uint64_t endUs = getMicroseconds();
    
    // Find matching zone in stack (pop until found)
    for (auto it = m_zoneStack.rbegin(); it != m_zoneStack.rend(); ++it) {
        if (it->zone.name == name) {
            it->zone.endUs = endUs;
            it->zone.durationUs = endUs - it->zone.startUs;
            
            // Accumulate to current frame stats by zone name
            double durationUs = static_cast<double>(it->zone.durationUs);
            if (std::string(name) == "UI_Update") {
                m_currentFrame.uiUpdateUs += durationUs;
            } else if (std::string(name) == "Render_Prep") {
                m_currentFrame.renderPrepUs += durationUs;
            } else if (std::string(name) == "GPU_Submit") {
                m_currentFrame.gpuSubmitUs += durationUs;
            } else if (std::string(name) == "Input_Poll") {
                m_currentFrame.inputPollUs += durationUs;
            }
            
            // Update thread-specific CPU time
            ThreadStats& threadStats = getOrCreateThreadStats(it->zone.threadId, it->zone.threadName.c_str());
            threadStats.cpuTimeMs += durationUs / 1000.0;
            
            // Record the finished zone into the current frame's zone list so it can be exported
            UnifiedZoneEntry finished = it->zone;
            if (m_currentFrame.zones.size() < 10000) { // safety cap
                m_currentFrame.zones.push_back(finished);
            }
            
            // Remove from stack
            m_zoneStack.erase(std::next(it).base());
            break;
        }
    }
}

void UnifiedProfiler::beginFrame() {
    if (!m_enabled) return;
    
    m_frameStart = std::chrono::steady_clock::now();
    m_renderEnd = m_frameStart;
    m_swapEnd = m_frameStart;
    
    // Reset current frame
    m_currentFrame = AdvancedFrameStats();
    m_currentFrame.frameStartUs = getMicroseconds();
    
    // Clear active alerts for this frame
    m_currentFrame.alerts.clear();
}

void UnifiedProfiler::markRenderEnd() {
    if (!m_enabled) return;
    m_renderEnd = std::chrono::steady_clock::now();
}

void UnifiedProfiler::markSwapEnd() {
    if (!m_enabled) return;
    m_swapEnd = std::chrono::steady_clock::now();
}

void UnifiedProfiler::endFrame() {
    if (!m_enabled) return;
    
    auto frameEnd = std::chrono::steady_clock::now();
    
    // Calculate total frame time
    auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - m_frameStart);
    m_currentFrame.totalTimeMs = frameDuration.count() / 1000.0;
    
    // Calculate breakdown times (from NUIFrameProfiler logic)
    m_currentFrame.renderTimeMs = std::chrono::duration<double, std::milli>(m_renderEnd - m_frameStart).count();
    m_currentFrame.swapTimeMs = std::chrono::duration<double, std::milli>(m_swapEnd - m_renderEnd).count();
    
    // Sleep time = total - render - swap
    m_currentFrame.sleepTimeMs = m_currentFrame.totalTimeMs - m_currentFrame.renderTimeMs - m_currentFrame.swapTimeMs;
    
    // Calculate CPU time (zone timings)
    m_currentFrame.cpuTimeMs = (m_currentFrame.uiUpdateUs + m_currentFrame.renderPrepUs + 
                                 m_currentFrame.inputPollUs) / 1000.0;
    
    // GPU time is approximated by submit time (actual GPU time needs GL queries)
    m_currentFrame.gpuTimeMs = m_currentFrame.gpuSubmitUs / 1000.0;
    
    // Update performance alerts
    updatePerformanceAlerts();
    
    // Detect performance regressions
    if (m_regressionDetectionEnabled) {
        detectPerformanceRegressions();
    }
    
    // Add to history
    if (m_history.size() < HISTORY_SIZE) {
        m_history.push_back(m_currentFrame);
    } else {
        m_history[m_historyIndex] = m_currentFrame;
        m_historyIndex = (m_historyIndex + 1) % HISTORY_SIZE;
    }
    
    // Update running averages
    updateAverages();
    
    // Update FPS counter (every second)
    m_fpsFrameCount++;
    auto fpsDuration = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - m_fpsTimer);
    if (fpsDuration.count() >= 1000) {
        m_fps = m_fpsFrameCount * 1000.0 / fpsDuration.count();
        m_fpsFrameCount = 0;
        m_fpsTimer = frameEnd;
    }
    
    m_frameCount++;
    m_lastFrameEnd = frameEnd;
}

void UnifiedProfiler::recordDrawCall() {
    if (!m_enabled) return;
    m_currentFrame.drawCalls++;
    m_currentFrame.gpu.totalDrawCalls++;
}

void UnifiedProfiler::recordTriangles(uint32_t count) {
    if (!m_enabled) return;
    m_currentFrame.triangles += count;
    m_currentFrame.gpu.totalTriangles += count;
}

void UnifiedProfiler::setWidgetCount(uint32_t count) {
    if (!m_enabled) return;
    m_currentFrame.widgetCount = count;
}

void UnifiedProfiler::setAudioLoad(double percent) {
    if (!m_enabled) return;
    m_currentFrame.audioLoadPercent = percent;
}

void UnifiedProfiler::recordMemoryAllocation(size_t bytes) {
    if (!m_enabled) return;
    
    m_currentFrame.memory.allocationCount++;
    m_currentFrame.memory.currentBytes += bytes;
    
    if (m_currentFrame.memory.currentBytes > m_currentFrame.memory.peakBytes) {
        m_currentFrame.memory.peakBytes = m_currentFrame.memory.currentBytes;
    }
    
    // Update average allocation size
    double totalAllocated = static_cast<double>(m_currentFrame.memory.currentBytes);
    double totalAllocs = static_cast<double>(m_currentFrame.memory.allocationCount);
    if (totalAllocs > 0) {
        m_currentFrame.memory.averageAllocationSize = totalAllocated / totalAllocs;
    }
}

void UnifiedProfiler::recordMemoryDeallocation(size_t bytes) {
    if (!m_enabled) return;
    
    m_currentFrame.memory.deallocationCount++;
    if (bytes <= m_currentFrame.memory.currentBytes) {
        m_currentFrame.memory.currentBytes -= bytes;
    }
}

void UnifiedProfiler::recordMemoryPeak(size_t bytes) {
    if (!m_enabled) return;
    if (bytes > m_currentFrame.memory.peakBytes) {
        m_currentFrame.memory.peakBytes = bytes;
    }
}

void UnifiedProfiler::recordGPUDrawCall(double timeMs) {
    if (!m_enabled) return;
    m_currentFrame.gpu.drawCallTimeMs += timeMs;
}

void UnifiedProfiler::recordGPUBufferUpload(double timeMs) {
    if (!m_enabled) return;
    m_currentFrame.gpu.bufferUploadTimeMs += timeMs;
}

void UnifiedProfiler::setAudioEngine(Audio::AudioEngine* engine) {
    m_audioEngine = engine;
}

void UnifiedProfiler::syncAudioTelemetry() {
    if (!m_enabled || !m_audioEngine) return;
    
    auto& telemetry = m_audioEngine->telemetry();
    
    // Sync audio metrics from AudioTelemetry
    m_currentFrame.audioXruns = telemetry.xruns.load(std::memory_order_relaxed);
    
    // Audio load is already set via setAudioLoad from TrackManager
    // This sync is for additional audio-specific metrics
}

ThreadStats& UnifiedProfiler::getOrCreateThreadStats(uint32_t threadId, const char* threadName) {
    auto it = m_threadStats.find(threadId);
    if (it != m_threadStats.end()) {
        return it->second;
    }
    
    ThreadStats stats;
    stats.threadId = threadId;
    stats.threadName = threadName ? threadName : "Unknown";
    m_threadStats[threadId] = stats;
    return m_threadStats[threadId];
}

std::string UnifiedProfiler::getCurrentThreadName() const {
    auto threadId = std::this_thread::get_id();
    
    // Map common thread IDs to names
    std::hash<std::thread::id> hasher;
    uint32_t id = static_cast<uint32_t>(hasher(threadId));
    
    if (id == 1) return "Main";
    if (id == 2) return "Audio";
    if (id == 3) return "Render";
    
    return "Worker_" + std::to_string(id);
}

uint32_t UnifiedProfiler::getCurrentThreadId() const {
    std::hash<std::thread::id> hasher;
    return static_cast<uint32_t>(hasher(std::this_thread::get_id()));
}

void UnifiedProfiler::updateAverages() {
    if (m_history.empty()) return;
    
    // Calculate averages over last 60 frames (1 second at 60fps)
    size_t sampleCount = std::min(size_t(60), m_history.size());
    AdvancedFrameStats avg{};
    
    for (size_t i = 0; i < sampleCount; ++i) {
        size_t idx = (m_historyIndex + HISTORY_SIZE - i - 1) % m_history.size();
        if (idx >= m_history.size()) continue;
        
        const auto& frame = m_history[idx];
        
        // Basic timing
        avg.cpuTimeMs += frame.cpuTimeMs;
        avg.gpuTimeMs += frame.gpuTimeMs;
        avg.totalTimeMs += frame.totalTimeMs;
        avg.renderTimeMs += frame.renderTimeMs;
        avg.swapTimeMs += frame.swapTimeMs;
        avg.sleepTimeMs += frame.sleepTimeMs;
        
        // Audio
        avg.audioLoadPercent += frame.audioLoadPercent;
        avg.audioXruns += frame.audioXruns;
        
        // Rendering
        avg.drawCalls += frame.drawCalls;
        avg.widgetCount += frame.widgetCount;
        avg.triangles += frame.triangles;
        
        // Memory
        avg.memory.peakBytes = std::max(avg.memory.peakBytes, frame.memory.peakBytes);
        avg.memory.allocationCount += frame.memory.allocationCount;
        avg.memory.deallocationCount += frame.memory.deallocationCount;
    }
    
    double scale = 1.0 / sampleCount;
    avg.cpuTimeMs *= scale;
    avg.gpuTimeMs *= scale;
    avg.totalTimeMs *= scale;
    avg.renderTimeMs *= scale;
    avg.swapTimeMs *= scale;
    avg.sleepTimeMs *= scale;
    avg.audioLoadPercent *= scale;
    avg.audioXruns = static_cast<uint32_t>(avg.audioXruns * scale);
    avg.drawCalls = static_cast<uint32_t>(avg.drawCalls * scale);
    avg.widgetCount = static_cast<uint32_t>(avg.widgetCount * scale);
    avg.triangles = static_cast<uint32_t>(avg.triangles * scale);
    
    // Average allocation size
    if (avg.memory.allocationCount > 0) {
        avg.memory.averageAllocationSize = static_cast<double>(avg.memory.currentBytes) / avg.memory.allocationCount;
    }
    
    m_averageStats = avg;
}

void UnifiedProfiler::updatePerformanceAlerts() {
    // Frame time spike detection
    if (m_currentFrame.totalTimeMs > m_thresholds.frameTimeMs) {
        PerformanceAlertData alert;
        alert.type = PerformanceAlert::FrameTimeSpike;
        alert.message = "Frame time spike: " + std::to_string(m_currentFrame.totalTimeMs) + "ms (threshold: " + std::to_string(m_thresholds.frameTimeMs) + "ms)";
        alert.value = m_currentFrame.totalTimeMs;
        alert.threshold = m_thresholds.frameTimeMs;
        alert.timestamp = std::chrono::steady_clock::now();
        m_currentFrame.alerts.push_back(alert);
        m_activeAlerts.push_back(alert);
    }
    
    // High audio load detection
    if (m_currentFrame.audioLoadPercent > m_thresholds.audioLoadPercent) {
        PerformanceAlertData alert;
        alert.type = PerformanceAlert::HighAudioLoad;
        alert.message = "High audio load: " + std::to_string(m_currentFrame.audioLoadPercent) + "% (threshold: " + std::to_string(m_thresholds.audioLoadPercent) + "%)";
        alert.value = m_currentFrame.audioLoadPercent;
        alert.threshold = m_thresholds.audioLoadPercent;
        alert.timestamp = std::chrono::steady_clock::now();
        m_currentFrame.alerts.push_back(alert);
        m_activeAlerts.push_back(alert);
    }
    
    // Audio xrun detection
    if (m_currentFrame.audioXruns > 0) {
        PerformanceAlertData alert;
        alert.type = PerformanceAlert::AudioXrun;
        alert.message = "Audio xrun detected: " + std::to_string(m_currentFrame.audioXruns) + " underruns";
        alert.value = static_cast<double>(m_currentFrame.audioXruns);
        alert.threshold = 0.0;
        alert.timestamp = std::chrono::steady_clock::now();
        m_currentFrame.alerts.push_back(alert);
        m_activeAlerts.push_back(alert);
    }
}

void UnifiedProfiler::detectPerformanceRegressions() {
    // Only check every 300 frames (5 seconds at 60fps) to avoid noise
    if (m_frameCount % 300 != 0) return;
    
    const auto& current = m_averageStats;
    
    // Check frame time regression
    auto baselineIt = m_baselines.find("frameTimeMs");
    if (baselineIt != m_baselines.end()) {
        double regression = ((current.totalTimeMs - baselineIt->second) / baselineIt->second) * 100.0;
        if (regression > 20.0) { // 20% regression threshold
            PerformanceRegression pr;
            pr.currentAvg = current.totalTimeMs;
            pr.baselineAvg = baselineIt->second;
            pr.regressionPercent = regression;
            pr.metricName = "Frame Time";
            pr.detectedAt = std::chrono::steady_clock::now();
            m_regressions.push_back(pr);
        }
    }
    
    // Check audio load regression
    baselineIt = m_baselines.find("audioLoadPercent");
    if (baselineIt != m_baselines.end()) {
        double regression = ((current.audioLoadPercent - baselineIt->second) / baselineIt->second) * 100.0;
        if (regression > 30.0) { // 30% regression threshold for audio
            PerformanceRegression pr;
            pr.currentAvg = current.audioLoadPercent;
            pr.baselineAvg = baselineIt->second;
            pr.regressionPercent = regression;
            pr.metricName = "Audio Load";
            pr.detectedAt = std::chrono::steady_clock::now();
            m_regressions.push_back(pr);
        }
    }
}

std::vector<PerformanceAlertData> UnifiedProfiler::getActiveAlerts() const {
    return m_activeAlerts;
}

void UnifiedProfiler::clearAlerts() {
    m_activeAlerts.clear();
    m_regressions.clear();
}

void UnifiedProfiler::setPerformanceBaseline(const std::string& metricName, double baselineValue) {
    m_baselines[metricName] = baselineValue;
}

void UnifiedProfiler::setAlertThresholds(double frameTimeMs, double audioLoadPercent) {
    m_thresholds.frameTimeMs = frameTimeMs;
    m_thresholds.audioLoadPercent = audioLoadPercent;
}

uint64_t UnifiedProfiler::getMicroseconds() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return duration.count();
}

double UnifiedProfiler::toMilliseconds(uint64_t startUs, uint64_t endUs) const {
    return static_cast<double>(endUs - startUs) / 1000.0;
}

void UnifiedProfiler::exportToJSON(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        Log::error("Failed to export profiler data to: " + filepath);
        return;
    }
    
    m_exportMetadata.exportTime = std::chrono::steady_clock::now();
    m_exportMetadata.totalFrames = m_frameCount;
    
    // Enhanced Chrome Trace Event Format
    file << "{\n";
    file << "  \"traceEvents\": [\n";
    
    // Export metadata as first event
    file << "    {\"name\": \"Metadata\", \"cat\": \"metadata\", \"ph\": \"i\", "
         << "\"ts\": 0, \"pid\": 1, \"tid\": 1, "
         << "\"args\": {\"buildInfo\": \"" << m_exportMetadata.buildInfo << "\", "
         << "\"totalFrames\": " << m_exportMetadata.totalFrames << ", "
         << "\"exportTime\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
             m_exportMetadata.exportTime.time_since_epoch()).count() << "\"}}\n";
    
    // Export last frames with enhanced data
    bool first = true;
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& frame = m_history[i];
        
        if (!first) file << ",\n";
        first = false;
        
        // Enhanced frame event with more metrics
        file << "    {\"name\": \"Frame\", \"cat\": \"frame\", \"ph\": \"X\", "
             << "\"ts\": " << (i * 16666) << ", "  // Assume 60fps
             << "\"dur\": " << static_cast<uint64_t>(frame.totalTimeMs * 1000) << ", "
             << "\"pid\": 1, \"tid\": 1, "
             << "\"args\": {\"cpu\": " << frame.cpuTimeMs << ", "
             << "\"gpu\": " << frame.gpuTimeMs << ", "
             << "\"render\": " << frame.renderTimeMs << ", "
             << "\"swap\": " << frame.swapTimeMs << ", "
             << "\"audioLoad\": " << frame.audioLoadPercent << ", "
             << "\"drawCalls\": " << frame.drawCalls << ", "
             << "\"widgets\": " << frame.widgetCount << ", "
             << "\"memoryMB\": " << (frame.memory.currentBytes / (1024.0 * 1024.0)) << "}}\n";
    }
    
    // Export enhanced zone events with threading
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& frame = m_history[i];
        if (frame.zones.empty()) continue;
        
        uint64_t frameBaseTs = i * 16666;
        for (const auto& z : frame.zones) {
            file << ",\n";
            uint64_t offset = 0;
            if (z.startUs >= frame.frameStartUs) offset = z.startUs - frame.frameStartUs;

            file << "    {\"name\": \"" << (z.name ? z.name : "zone") << "\", "
                 << "\"cat\": \"zone\", \"ph\": \"X\", "
                 << "\"ts\": " << (frameBaseTs + offset) << ", "
                 << "\"dur\": " << z.durationUs << ", "
                 << "\"pid\": 1, \"tid\": " << z.threadId << ", "
                 << "\"args\": {\"threadName\": \"" << z.threadName << "\", "
                 << "\"zoneId\": " << z.zoneId << ", "
                 << "\"parentZone\": " << z.parentZoneId << "}}\n";
        }
    }
    
    file << "\n  ],\n";
    file << "  \"displayTimeUnit\": \"ms\",\n";
    file << "  \"systemInfo\": \"" << m_exportMetadata.systemInfo << "\"\n";
    file << "}\n";
    
    file.close();
    Log::info("Enhanced profiler data exported to: " + filepath);
}

void UnifiedProfiler::exportToHTML(const std::string& filepath) {
    // This is a simplified HTML export - full interactive charts
    std::ofstream file(filepath);
    if (!file.is_open()) {
        Log::error("Failed to export HTML profiler report to: " + filepath);
        return;
    }
    
    file << "<!DOCTYPE html>\n";
    file << "<html>\n<head>\n";
    file << "<title>Nomad Profiler Report</title>\n";
    file << "<style>\n";
    file << "body { font-family: Arial, sans-serif; margin: 20px; }\n";
    file << ".metric { margin: 10px 0; padding: 10px; border: 1px solid #ccc; }\n";
    file << ".good { background-color: #d4edda; }\n";
    file << ".warning { background-color: #fff3cd; }\n";
    file << ".error { background-color: #f8d7da; }\n";
    file << "</style>\n</head>\n<body>\n";
    
    file << "<h1>Nomad Performance Report</h1>\n";
    file << "<p>Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
        m_exportMetadata.exportTime.time_since_epoch()).count() << "</p>\n";
    
    // Current performance summary
    file << "<h2>Current Performance</h2>\n";
    file << "<div class=\"metric " 
         << (m_currentFrame.totalTimeMs < 16.7 ? "good" : (m_currentFrame.totalTimeMs < 33.3 ? "warning" : "error")) 
         << "\">\n";
    file << "<strong>Frame Time:</strong> " << std::fixed << std::setprecision(2) 
         << m_currentFrame.totalTimeMs << " ms (FPS: " << m_fps << ")</div>\n";
    
    file << "<div class=\"metric " 
         << (m_currentFrame.audioLoadPercent < 70 ? "good" : (m_currentFrame.audioLoadPercent < 90 ? "warning" : "error")) 
         << "\">\n";
    file << "<strong>Audio Load:</strong> " << std::fixed << std::setprecision(1) 
         << m_currentFrame.audioLoadPercent << "%</div>\n";
    
    // Performance alerts
    if (!m_activeAlerts.empty()) {
        file << "<h2>Active Alerts</h2>\n";
        for (const auto& alert : m_activeAlerts) {
            file << "<div class=\"metric error\">\n";
            file << "<strong>" << alert.message << "</strong><br>\n";
            file << "Value: " << alert.value << ", Threshold: " << alert.threshold << "\n";
            file << "</div>\n";
        }
    }
    
    file << "</body>\n</html>\n";
    
    file.close();
    Log::info("HTML profiler report exported to: " + filepath);
}

void UnifiedProfiler::exportPerformanceReport(const std::string& filepath) {
    // Export both JSON and HTML for comprehensive reporting
    std::string jsonPath = filepath + ".json";
    std::string htmlPath = filepath + ".html";
    
    exportToJSON(jsonPath);
    exportToHTML(htmlPath);
    
    Log::info("Performance report exported to: " + filepath + " (JSON + HTML)");
}

void UnifiedProfiler::printFrameStats() const {
    if (!m_enabled || m_frameCount == 0) return;
    
    std::cout << "\n┌────────────────────────────────────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│                                    NOMAD UNIFIED PROFILER - Frame #" 
              << std::setw(9) << m_frameCount << "                                   │\n";
    std::cout << "├────────────────────────────────────────────────────────────────────────────────────────────────────┤\n";
    
    // Current frame breakdown
    std::cout << "│ CURRENT FRAME BREAKDOWN:                                                                             │\n";
    std::cout << "│   Render Time:  " << std::setw(8) << std::fixed << std::setprecision(2) 
              << m_currentFrame.renderTimeMs << " ms  ";
    if (m_currentFrame.renderTimeMs > 50.0) std::cout << "⚠️  CPU bottleneck!        │\n";
    else std::cout << "                              │\n";
    
    std::cout << "│   Swap Time:    " << std::setw(8) << m_currentFrame.swapTimeMs 
              << " ms  ";
    if (m_currentFrame.swapTimeMs > 20.0) std::cout << "⚠️  VSync stall!        │\n";
    else std::cout << "                      │\n";
    
    std::cout << "│   Sleep Time:   " << std::setw(8) << m_currentFrame.sleepTimeMs 
              << " ms                               │\n";
    std::cout << "│   Total Time:   " << std::setw(8) << m_currentFrame.totalTimeMs 
              << " ms                               │\n";
    std::cout << "│   FPS:          " << std::setw(8) << std::setprecision(1) 
              << m_fps << "                                    │\n";
    
    // Audio and memory metrics
    std::cout << "│   Audio Load:   " << std::setw(8) << std::setprecision(1) 
              << m_currentFrame.audioLoadPercent << "%  ";
    if (m_currentFrame.audioLoadPercent > 90.0) std::cout << "🔴 Audio overload!      │\n";
    else if (m_currentFrame.audioLoadPercent > 70.0) std::cout << "🟡 High audio load    │\n";
    else std::cout << "🟢 Healthy audio    │\n";
    
    std::cout << "│   Memory:       " << std::setw(8) << std::setprecision(1) 
              << (m_currentFrame.memory.currentBytes / (1024.0 * 1024.0)) 
              << " MB (Peak: " << (m_currentFrame.memory.peakBytes / (1024.0 * 1024.0)) << " MB)         │\n";
    
    // Performance alerts for this frame
    if (!m_currentFrame.alerts.empty()) {
        std::cout << "│   ALERTS:       ";
        for (const auto& alert : m_currentFrame.alerts) {
            std::cout << "⚠️  " << alert.message << "  ";
        }
        std::cout << "│\n";
    }
    
    std::cout << "├────────────────────────────────────────────────────────────────────────────────────────────────────┤\n";
    
    // Averages (smoothed)
    std::cout << "│ AVERAGES (smoothed):                                                                                │\n";
    std::cout << "│   Render:       " << std::setw(8) << std::setprecision(2) 
              << m_averageStats.renderTimeMs << " ms                               │\n";
    std::cout << "│   Swap:         " << std::setw(8) << m_averageStats.swapTimeMs 
              << " ms                               │\n";
    std::cout << "│   Total:        " << std::setw(8) << m_averageStats.totalTimeMs 
              << " ms                               │\n";
    std::cout << "│   FPS:          " << std::setw(8) << std::setprecision(1) 
              << m_fps << "                                    │\n";
    std::cout << "│   Audio:        " << std::setw(8) << std::setprecision(1) 
              << m_averageStats.audioLoadPercent << "%                               │\n";
    
    std::cout << "└────────────────────────────────────────────────────────────────────────────────────────────────────┘\n";
    
    // Additional diagnostics
    if (m_averageStats.swapTimeMs > 20.0) {
        std::cout << "⚠️  WARNING: Swap time is very high (" << m_averageStats.swapTimeMs 
                  << "ms) - likely VSync stall!\n";
        std::cout << "    Try disabling VSync to test if it's GPU-bound.\n\n";
    }
    
    if (m_averageStats.renderTimeMs > 50.0) {
        std::cout << "⚠️  WARNING: Render time is very high (" << m_averageStats.renderTimeMs 
                  << "ms) - CPU bottleneck!\n";
        std::cout << "    Consider optimizing draw calls or enabling batching.\n\n";
    }
    
    if (m_averageStats.audioLoadPercent > 90.0) {
        std::cout << "🔴 CRITICAL: Audio load is extremely high (" << m_averageStats.audioLoadPercent 
                  << "%) - audio xruns likely!\n";
        std::cout << "    Reduce track count, simplify effects, or increase buffer size.\n\n";
    }
}

void UnifiedProfiler::printPerformanceSummary() const {
    std::cout << "\n═══════════════════════════════════════════════════════════════════════════════\n";
    std::cout << "                              NOMAD PERFORMANCE SUMMARY                              \n";
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
    
    std::cout << "Total Frames: " << m_frameCount << "\n";
    std::cout << "Average FPS: " << std::fixed << std::setprecision(1) << m_fps << "\n";
    std::cout << "Average Frame Time: " << std::setprecision(2) << m_averageStats.totalTimeMs << " ms\n";
    std::cout << "Peak Memory Usage: " << (m_averageStats.memory.peakBytes / (1024.0 * 1024.0)) << " MB\n";
    std::cout << "Total Draw Calls: " << m_averageStats.drawCalls << "\n";
    std::cout << "Total Triangles: " << m_averageStats.triangles << "\n";
    std::cout << "Audio Xruns: " << m_averageStats.audioXruns << "\n";
    
    if (!m_regressions.empty()) {
        std::cout << "\nPerformance Regressions Detected:\n";
        for (const auto& reg : m_regressions) {
            std::cout << "  - " << reg.metricName << ": " << std::setprecision(1) 
                      << reg.regressionPercent << "% regression\n";
        }
    }
    
    std::cout << "═══════════════════════════════════════════════════════════════════════════════\n";
}

} // namespace Nomad