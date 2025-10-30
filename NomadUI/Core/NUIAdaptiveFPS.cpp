// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "NUIAdaptiveFPS.h"
#include <algorithm>
#include <numeric>
#include <thread>
#include <iostream>

namespace NomadUI {

// ============================================================================
// Constructor / Destructor
// ============================================================================

NUIAdaptiveFPS::NUIAdaptiveFPS()
    : m_mode(Mode::Auto)
    , m_currentTargetFPS(30.0)
    , m_userActive(false)
    , m_idleTimer(0.0)
    , m_animationActive(false)
    , m_audioVisualizationActive(false)
    , m_averageFrameTime(1.0 / 30.0)
    , m_actualFPS(30.0)
    , m_framesSince60FPSChange(0)
    , m_wasActive(false)
    , m_logFrameCounter(0)
{
    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_lastActivityTime = m_lastFrameTime;
}

NUIAdaptiveFPS::NUIAdaptiveFPS(const Config& config)
    : NUIAdaptiveFPS()
{
    m_config = config;
}

NUIAdaptiveFPS::~NUIAdaptiveFPS() {
}

// ============================================================================
// Configuration
// ============================================================================

void NUIAdaptiveFPS::setMode(Mode mode) {
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;

    // Reset to appropriate FPS based on mode
    switch (m_mode) {
        case Mode::Locked30:
            m_currentTargetFPS = m_config.fps30;
            break;
        case Mode::Locked60:
            m_currentTargetFPS = m_config.fps60;
            break;
        case Mode::Auto:
            // Will be updated in updateTargetFPS
            break;
    }

    if (m_config.enableLogging) {
        std::cout << "[AdaptiveFPS] Mode changed to: ";
        switch (m_mode) {
            case Mode::Auto: std::cout << "Auto"; break;
            case Mode::Locked30: std::cout << "Locked 30 FPS"; break;
            case Mode::Locked60: std::cout << "Locked 60 FPS"; break;
        }
        std::cout << std::endl;
    }
}

void NUIAdaptiveFPS::setConfig(const Config& config) {
    m_config = config;
}

// ============================================================================
// Activity Tracking
// ============================================================================

void NUIAdaptiveFPS::signalActivity(ActivityType type) {
    m_userActive = true;
    m_idleTimer = 0.0;
    m_lastActivityTime = std::chrono::high_resolution_clock::now();

    if (m_config.enableLogging && !m_wasActive) {
        std::cout << "[AdaptiveFPS] Activity detected: ";
        switch (type) {
            case ActivityType::MouseMove: std::cout << "MouseMove"; break;
            case ActivityType::MouseClick: std::cout << "MouseClick"; break;
            case ActivityType::MouseDrag: std::cout << "MouseDrag"; break;
            case ActivityType::Scroll: std::cout << "Scroll"; break;
            case ActivityType::KeyPress: std::cout << "KeyPress"; break;
            case ActivityType::WindowResize: std::cout << "WindowResize"; break;
            case ActivityType::Animation: std::cout << "Animation"; break;
            case ActivityType::AudioVisualization: std::cout << "AudioVisualization"; break;
        }
        std::cout << std::endl;
    }
}

void NUIAdaptiveFPS::setAnimationActive(bool active) {
    m_animationActive = active;
    if (active) {
        signalActivity(ActivityType::Animation);
    }
}

void NUIAdaptiveFPS::setAudioVisualizationActive(bool active) {
    m_audioVisualizationActive = active;
    if (active) {
        signalActivity(ActivityType::AudioVisualization);
    }
}

// ============================================================================
// Frame Timing
// ============================================================================

std::chrono::high_resolution_clock::time_point NUIAdaptiveFPS::beginFrame() {
    auto now = std::chrono::high_resolution_clock::now();
    
    // On first frame or if too much time has passed (e.g., breakpoint),
    // reset m_lastFrameTime to avoid huge delta spikes
    double timeSinceLastFrame = std::chrono::duration<double>(now - m_lastFrameTime).count();
    if (timeSinceLastFrame > 0.5) {  // More than 500ms = something weird happened
        m_lastFrameTime = now;
    }
    
    return now;
}

double NUIAdaptiveFPS::endFrame(
    const std::chrono::high_resolution_clock::time_point& frameStart,
    double deltaTime)
{
    auto frameEnd = std::chrono::high_resolution_clock::now();
    
    // Calculate actual total frame time (from end of last frame to end of this frame)
    // This includes the work time + sleep time from the previous frame
    double actualDeltaTime = std::chrono::duration<double>(frameEnd - m_lastFrameTime).count();
    
    // Only clamp on extreme outliers (debugger pauses, system sleep)
    // For normal operation, even slow frames should be measured accurately
    if (actualDeltaTime > 1.0) {  // More than 1 second = something abnormal
        actualDeltaTime = 1.0 / 30.0; // Default to 30 FPS equivalent
    }
    if (actualDeltaTime < 0.001) {  // Less than 1ms = impossible
        actualDeltaTime = 0.001;
    }
    
    // Update target FPS based on activity and performance using actual delta
    updateTargetFPS(actualDeltaTime);

    // Measure work time only (time spent rendering, excluding sleep)
    double frameTime = std::chrono::duration<double>(frameEnd - frameStart).count();

    // Update performance metrics with work time
    updatePerformanceMetrics(frameTime);
    
    // Calculate actual FPS from total frame time (includes sleep)
    // Using seconds, so 1.0 / 0.0166 = 60 FPS
    double instantFPS = 1.0 / actualDeltaTime;
    m_actualFPS = m_actualFPS * 0.9 + instantFPS * 0.1; // Exponential smoothing

    // Calculate sleep time to maintain target FPS
    double targetFrameTime = 1.0 / m_currentTargetFPS;
    double sleepTime = targetFrameTime - frameTime;
    
    // DEBUG: Print timing info (frames 10-20 for steady state)
    static int debugFrameCount = 0;
    if (debugFrameCount >= 10 && debugFrameCount < 20) {
        std::cout << "[STEADY Frame " << debugFrameCount << "] "
                  << "actualDeltaTime: " << (actualDeltaTime * 1000.0) << "ms | "
                  << "frameTime (work): " << (frameTime * 1000.0) << "ms | "
                  << "instantFPS: " << instantFPS << std::endl;
    }
    debugFrameCount++;

    // Log state periodically
    if (m_config.enableLogging) {
        logState();
    }

    m_lastFrameTime = frameEnd;

    return std::max(0.0, sleepTime);
}

void NUIAdaptiveFPS::sleep(double sleepDuration) {
    if (sleepDuration > 0.0) {
        std::this_thread::sleep_for(std::chrono::duration<double>(sleepDuration));
    }
}

// ============================================================================
// State Queries
// ============================================================================

double NUIAdaptiveFPS::getAverageFrameTime() const {
    return m_averageFrameTime;
}

bool NUIAdaptiveFPS::canSustain60FPS() const {
    // Need at least some samples to evaluate
    if (m_frameTimeHistory.size() < 5) {
        return true; // Assume yes initially
    }

    // Check if average frame time is below threshold
    return m_averageFrameTime < m_config.performanceThreshold;
}

NUIAdaptiveFPS::Stats NUIAdaptiveFPS::getStats() const {
    Stats stats;
    stats.currentTargetFPS = m_currentTargetFPS;
    stats.actualFPS = m_actualFPS; // Use the smoothed actual FPS
    stats.averageFrameTime = m_averageFrameTime; // This is work time (excluding sleep)
    stats.userActive = m_userActive;
    stats.idleTime = m_idleTimer;
    stats.canSustain60 = canSustain60FPS();
    stats.framesSince60FPSChange = m_framesSince60FPSChange;
    return stats;
}

// ============================================================================
// Internal Methods
// ============================================================================

void NUIAdaptiveFPS::updateTargetFPS(double deltaTime) {
    // Handle locked modes
    if (m_mode == Mode::Locked30) {
        m_currentTargetFPS = m_config.fps30;
        return;
    } else if (m_mode == Mode::Locked60) {
        m_currentTargetFPS = m_config.fps60;
        return;
    }

    // Auto mode: adaptive FPS
    m_framesSince60FPSChange++;

    // Check for active conditions
    bool shouldBoosted = m_userActive || m_animationActive || m_audioVisualizationActive;

    // Update idle timer
    if (!shouldBoosted) {
        m_idleTimer += deltaTime;
    } else {
        m_idleTimer = 0.0;
    }

    // Determine target FPS
    double targetFPS = m_config.fps30;

    if (shouldBoosted && canSustain60FPS()) {
        // Boost to 60 FPS
        targetFPS = m_config.fps60;
        m_framesSince60FPSChange = 0;
    } else if (m_idleTimer < m_config.idleTimeout && m_currentTargetFPS > m_config.fps30) {
        // Keep current FPS during grace period
        targetFPS = m_currentTargetFPS;
    } else {
        // Return to 30 FPS
        targetFPS = m_config.fps30;
    }

    // Smooth transition to target FPS
    smoothTransition(targetFPS);

    // Reset activity flag if idle timeout reached
    if (m_idleTimer >= m_config.idleTimeout) {
        m_userActive = false;
    }

    m_wasActive = shouldBoosted;
}

void NUIAdaptiveFPS::smoothTransition(double targetFPS) {
    // Lerp between current and target FPS for smooth transitions
    double diff = targetFPS - m_currentTargetFPS;
    
    // If difference is small enough, snap to target
    if (std::abs(diff) < 0.5) {
        m_currentTargetFPS = targetFPS;
    } else {
        // Smooth interpolation
        m_currentTargetFPS += diff * m_config.transitionSpeed;
    }
}

void NUIAdaptiveFPS::updatePerformanceMetrics(double frameTime) {
    // Add to history
    m_frameTimeHistory.push_back(frameTime);

    // Keep only last N samples
    while (m_frameTimeHistory.size() > static_cast<size_t>(m_config.performanceSampleCount)) {
        m_frameTimeHistory.pop_front();
    }

    // Calculate average
    if (!m_frameTimeHistory.empty()) {
        double sum = std::accumulate(m_frameTimeHistory.begin(), m_frameTimeHistory.end(), 0.0);
        m_averageFrameTime = sum / m_frameTimeHistory.size();
    }
}

void NUIAdaptiveFPS::resetActivity() {
    m_userActive = false;
    m_idleTimer = 0.0;
    m_animationActive = false;
    m_audioVisualizationActive = false;
}

void NUIAdaptiveFPS::logState() {
    // Log every 60 frames (once per second at 60 FPS)
    m_logFrameCounter++;
    if (m_logFrameCounter < 60) {
        return;
    }
    m_logFrameCounter = 0;

    auto stats = getStats();
    std::cout << "[AdaptiveFPS] "
              << "Target: " << static_cast<int>(stats.currentTargetFPS) << " FPS | "
              << "Actual: " << static_cast<int>(stats.actualFPS) << " FPS | "
              << "FrameTime: " << (stats.averageFrameTime * 1000.0) << " ms | "
              << "Active: " << (stats.userActive ? "YES" : "NO") << " | "
              << "Idle: " << stats.idleTime << "s | "
              << "Can60: " << (stats.canSustain60 ? "YES" : "NO")
              << std::endl;
}

} // namespace NomadUI
