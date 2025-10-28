#pragma once

#include <chrono>
#include <deque>
#include <cmath>

namespace NomadUI {

/**
 * @brief Adaptive FPS Manager
 * 
 * Intelligently manages frame pacing between 30 and 60 FPS based on:
 * - User activity (mouse, keyboard, drag, resize)
 * - Active animations or scrolling
 * - Audio visualization updates
 * - System performance metrics
 * 
 * @details
 * **Performance Goals:**
 * - Idle: 30 FPS (~33.3ms/frame) for low CPU/thermal load
 * - Active: 60 FPS (~16.6ms/frame) for smooth interactions
 * - Auto-adjust based on sustained frame time performance
 * 
 * **Design Principles:**
 * - Smooth transitions (no sudden FPS snaps)
 * - Performance guards (revert if system can't sustain 60 FPS)
 * - Idle detection (return to 30 FPS after inactivity)
 * - Audio thread independence (no impact on audio callbacks)
 */
class NUIAdaptiveFPS {
public:
    /**
     * @brief FPS mode enumeration
     */
    enum class Mode {
        Auto,       ///< Adaptive: switches between 30 and 60 FPS automatically
        Locked30,   ///< Always 30 FPS
        Locked60    ///< Always 60 FPS
    };

    /**
     * @brief Activity type that triggers FPS boost
     */
    enum class ActivityType {
        MouseMove,
        MouseClick,
        MouseDrag,
        Scroll,
        KeyPress,
        WindowResize,
        Animation,
        AudioVisualization
    };

    /**
     * @brief Configuration for adaptive FPS system
     */
    struct Config {
        double fps30 = 30.0;                    ///< Target FPS for idle state
        double fps60 = 60.0;                    ///< Target FPS for active state
        double idleTimeout = 2.0;               ///< Seconds of inactivity before lowering to 30 FPS
        double performanceThreshold = 0.018;    ///< Max frame time (18ms) to sustain 60 FPS
        int performanceSampleCount = 10;        ///< Number of frames to average for performance check
        double transitionSpeed = 0.05;          ///< Lerp factor for smooth FPS transitions (0-1)
        bool enableLogging = false;             ///< Enable debug logging
    };

public:
    NUIAdaptiveFPS();
    explicit NUIAdaptiveFPS(const Config& config);
    ~NUIAdaptiveFPS();

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set the FPS mode
     */
    void setMode(Mode mode);

    /**
     * @brief Get the current FPS mode
     */
    Mode getMode() const { return m_mode; }

    /**
     * @brief Set configuration
     */
    void setConfig(const Config& config);

    /**
     * @brief Get current configuration
     */
    const Config& getConfig() const { return m_config; }

    // ========================================================================
    // Activity Tracking
    // ========================================================================

    /**
     * @brief Signal user activity (triggers FPS boost)
     * @param type Type of activity
     */
    void signalActivity(ActivityType type);

    /**
     * @brief Check if any animation is currently active
     */
    void setAnimationActive(bool active);

    /**
     * @brief Check if audio visualization is updating
     */
    void setAudioVisualizationActive(bool active);

    // ========================================================================
    // Frame Timing
    // ========================================================================

    /**
     * @brief Begin frame timing measurement
     * @return Current time point for frame timing
     */
    std::chrono::high_resolution_clock::time_point beginFrame();

    /**
     * @brief End frame timing and calculate sleep duration
     * @param frameStart Time point from beginFrame()
     * @param deltaTime Actual delta time since last frame (in seconds)
     * @return Sleep duration in seconds (0 if no sleep needed)
     */
    double endFrame(const std::chrono::high_resolution_clock::time_point& frameStart, double deltaTime);

    /**
     * @brief Sleep for the calculated duration
     * @param sleepDuration Duration in seconds
     */
    void sleep(double sleepDuration);

    // ========================================================================
    // State Queries
    // ========================================================================

    /**
     * @brief Get current target FPS
     */
    double getCurrentTargetFPS() const { return m_currentTargetFPS; }

    /**
     * @brief Get current target frame time (in seconds)
     */
    double getCurrentTargetFrameTime() const { return 1.0 / m_currentTargetFPS; }

    /**
     * @brief Get average frame time over last N frames
     */
    double getAverageFrameTime() const;

    /**
     * @brief Check if system can sustain 60 FPS
     */
    bool canSustain60FPS() const;

    /**
     * @brief Check if user is currently active
     */
    bool isUserActive() const { return m_userActive; }

    /**
     * @brief Get idle time in seconds
     */
    double getIdleTime() const { return m_idleTimer; }

    /**
     * @brief Get statistics for debugging
     */
    struct Stats {
        double currentTargetFPS;
        double actualFPS;
        double averageFrameTime;
        bool userActive;
        double idleTime;
        bool canSustain60;
        int framesSince60FPSChange;
    };
    Stats getStats() const;

private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Update target FPS based on activity and performance
     */
    void updateTargetFPS(double deltaTime);

    /**
     * @brief Smooth transition between FPS targets
     */
    void smoothTransition(double targetFPS);

    /**
     * @brief Update performance metrics
     */
    void updatePerformanceMetrics(double frameTime);

    /**
     * @brief Reset activity state
     */
    void resetActivity();

    /**
     * @brief Log current state (if logging enabled)
     */
    void logState();

private:
    // Configuration
    Config m_config;
    Mode m_mode;

    // Timing
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    double m_currentTargetFPS;
    
    // Activity tracking
    bool m_userActive;
    double m_idleTimer;
    bool m_animationActive;
    bool m_audioVisualizationActive;
    std::chrono::high_resolution_clock::time_point m_lastActivityTime;

    // Performance tracking
    std::deque<double> m_frameTimeHistory;
    double m_averageFrameTime;
    double m_actualFPS;
    int m_framesSince60FPSChange;

    // State tracking
    bool m_wasActive;
    int m_logFrameCounter;
};

} // namespace NomadUI
