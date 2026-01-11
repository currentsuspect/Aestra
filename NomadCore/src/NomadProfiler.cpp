// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file NomadProfiler.cpp
 * @brief Performance profiler implementation
 */

#include "NomadProfiler.h"
#include "NomadLog.h"
#include <sstream>
#include <iomanip>
#include <thread>

namespace Nomad {

//==============================================================================
// ScopedTimer Implementation
//==============================================================================

ScopedTimer::ScopedTimer(const char* name)
    : m_name(name)
    , m_start(std::chrono::steady_clock::now())
{
    Profiler::getInstance().beginZone(name);
}

ScopedTimer::~ScopedTimer() {
    Profiler::getInstance().endZone(m_name);
}

//==============================================================================
// Profiler Implementation
//==============================================================================

Profiler& Profiler::getInstance() {
    static Profiler instance;
    return instance;
}

Profiler::Profiler()
    : m_frameStart(std::chrono::steady_clock::now())
    , m_cpuStart(m_frameStart)
    , m_gpuStart(m_frameStart)
    , m_fpsTimer(m_frameStart)
{
    m_history.reserve(HISTORY_SIZE);
    m_zoneStack.reserve(16); // Typical max nesting depth
}

void Profiler::beginZone(const char* name) {
    if (!m_enabled) return;
    
    ZoneEntry entry;
    entry.name = name;
    entry.startUs = getMicroseconds();
    entry.threadId = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    
    m_zoneStack.push_back(entry);
}

void Profiler::endZone(const char* name) {
    if (!m_enabled) return;
    
    uint64_t endUs = getMicroseconds();
    
    // Find matching zone in stack (pop until found)
    for (auto it = m_zoneStack.rbegin(); it != m_zoneStack.rend(); ++it) {
        if (it->name == name) {
            it->endUs = endUs;
            double durationUs = static_cast<double>(endUs - it->startUs);
            
            // Accumulate to current frame stats by zone name
            if (std::string(name) == "UI_Update") {
                m_currentFrame.uiUpdateUs += durationUs;
            } else if (std::string(name) == "Render_Prep") {
                m_currentFrame.renderPrepUs += durationUs;
            } else if (std::string(name) == "GPU_Submit") {
                m_currentFrame.gpuSubmitUs += durationUs;
            } else if (std::string(name) == "Input_Poll") {
                m_currentFrame.inputPollUs += durationUs;
            }
            
            // Remove from stack
            // Record the finished zone into the current frame's zone list so it can be exported
            ZoneEntry finished = *it;
            finished.endUs = endUs;
            if (m_currentFrame.zones.size() < 10000) // safety cap
                m_currentFrame.zones.push_back(finished);

            m_zoneStack.erase(std::next(it).base());
            break;
        }
    }
}

void Profiler::beginFrame() {
    if (!m_enabled) return;
    
    m_frameStart = std::chrono::steady_clock::now();
    m_cpuStart = m_frameStart;
    
    // Reset current frame
    m_currentFrame = FrameStats();
    // capture absolute frame-start timestamp for later JSON export
    m_currentFrame.frameStartUs = getMicroseconds();
}

void Profiler::endFrame() {
    if (!m_enabled) return;
    
    auto frameEnd = std::chrono::steady_clock::now();
    
    // Calculate total frame time
    auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - m_frameStart);
    m_currentFrame.totalTimeMs = frameDuration.count() / 1000.0;
    
    // Calculate CPU time (zone timings)
    m_currentFrame.cpuTimeMs = (m_currentFrame.uiUpdateUs + m_currentFrame.renderPrepUs + 
                                 m_currentFrame.inputPollUs) / 1000.0;
    
    // GPU time is approximated by submit time (actual GPU time needs GL queries)
    m_currentFrame.gpuTimeMs = m_currentFrame.gpuSubmitUs / 1000.0;
    
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
}

void Profiler::recordDrawCall() {
    if (!m_enabled) return;
    m_currentFrame.drawCalls++;
}

void Profiler::recordTriangles(uint32_t count) {
    if (!m_enabled) return;
    m_currentFrame.triangles += count;
}

void Profiler::setWidgetCount(uint32_t count) {
    if (!m_enabled) return;
    m_currentFrame.widgetCount = count;
}

void Profiler::setAudioLoad(double percent) {
    if (!m_enabled) return;
    m_currentFrame.audioLoadPercent = percent;
}

void Profiler::updateAverages() {
    if (m_history.empty()) return;
    
    // Calculate averages over last 60 frames (1 second at 60fps)
    size_t sampleCount = std::min(size_t(60), m_history.size());
    FrameStats avg{};
    
    for (size_t i = 0; i < sampleCount; ++i) {
        size_t idx = (m_historyIndex + HISTORY_SIZE - i - 1) % m_history.size();
        if (idx >= m_history.size()) continue;
        
        const auto& frame = m_history[idx];
        avg.cpuTimeMs += frame.cpuTimeMs;
        avg.gpuTimeMs += frame.gpuTimeMs;
        avg.totalTimeMs += frame.totalTimeMs;
        avg.audioLoadPercent += frame.audioLoadPercent;
        avg.drawCalls += frame.drawCalls;
        avg.widgetCount += frame.widgetCount;
        avg.triangles += frame.triangles;
    }
    
    double scale = 1.0 / sampleCount;
    avg.cpuTimeMs *= scale;
    avg.gpuTimeMs *= scale;
    avg.totalTimeMs *= scale;
    avg.audioLoadPercent *= scale;
    avg.drawCalls = static_cast<uint32_t>(avg.drawCalls * scale);
    avg.widgetCount = static_cast<uint32_t>(avg.widgetCount * scale);
    avg.triangles = static_cast<uint32_t>(avg.triangles * scale);
    
    m_averageStats = avg;
}

uint64_t Profiler::getMicroseconds() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
    return duration.count();
}

void Profiler::exportToJSON(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        Log::error("Failed to export profiler data to: " + filepath);
        return;
    }
    
    // Chrome Trace Event Format
    file << "{\n";
    file << "  \"traceEvents\": [\n";
    
    // Export last 300 frames
    bool first = true;
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& frame = m_history[i];
        
        if (!first) file << ",\n";
        first = false;
        
        // Frame event
        file << "    {\"name\": \"Frame\", \"cat\": \"frame\", \"ph\": \"X\", "
             << "\"ts\": " << (i * 16666) << ", "  // Assume 60fps
             << "\"dur\": " << static_cast<uint64_t>(frame.totalTimeMs * 1000) << ", "
             << "\"pid\": 1, \"tid\": 1, "
             << "\"args\": {\"cpu\": " << frame.cpuTimeMs << ", \"gpu\": " << frame.gpuTimeMs << "}}";
    }
    
    // Export zone events (per-frame)
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto& frame = m_history[i];
        if (frame.zones.empty()) continue;
        // base synthetic frame timestamp
        uint64_t frameBaseTs = i * 16666;
        for (const auto& z : frame.zones) {
            file << ",\n";
            uint64_t offset = 0;
            if (z.startUs >= frame.frameStartUs) offset = z.startUs - frame.frameStartUs;
            uint64_t dur = 0;
            if (z.endUs >= z.startUs) dur = z.endUs - z.startUs;

            file << "    {\"name\": \"" << (z.name ? z.name : "zone") << "\", "
                 << "\"cat\": \"zone\", \"ph\": \"X\", "
                 << "\"ts\": " << (frameBaseTs + offset) << ", "
                 << "\"dur\": " << dur << ", "
                 << "\"pid\": 1, \"tid\": " << z.threadId << "}";
        }
    }
    
    file << "\n  ],\n";
    file << "  \"displayTimeUnit\": \"ms\"\n";
    file << "}\n";
    
    file.close();
    Log::info("Profiler data exported to: " + filepath);
}

} // namespace Nomad
