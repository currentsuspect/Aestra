// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AppLifecycle.h
 * @brief B-002: Explicit application lifecycle state management
 * 
 * This prevents partial-init bugs by enforcing state transitions and
 * allowing subsystems to query current lifecycle state.
 * 
 * STATE DIAGRAM:
 *   [Created] -> [Initializing] -> [Running] -> [ShuttingDown] -> [Terminated]
 *                      |                             ^
 *                      v                             |
 *                 [InitFailed] ----------------------+
 * 
 * THREAD SAFETY:
 * - State transitions are atomic
 * - Callbacks are invoked on the MAIN THREAD
 * - State queries are safe from any thread
 */

#include <atomic>
#include <functional>
#include <vector>
#include <mutex>
#include <string>

namespace Aestra {

/**
 * @brief Application lifecycle states (B-002)
 */
enum class AppState {
    Created,        ///< App object exists, no initialization yet
    Initializing,   ///< Currently initializing subsystems
    InitFailed,     ///< Initialization failed (terminal state before shutdown)
    Running,        ///< Fully initialized, main loop active
    ShuttingDown,   ///< Shutdown in progress
    Terminated      ///< All subsystems shut down (terminal state)
};

/**
 * @brief Convert AppState to string for logging
 */
inline const char* appStateToString(AppState state) {
    switch (state) {
        case AppState::Created:      return "Created";
        case AppState::Initializing: return "Initializing";
        case AppState::InitFailed:   return "InitFailed";
        case AppState::Running:      return "Running";
        case AppState::ShuttingDown: return "ShuttingDown";
        case AppState::Terminated:   return "Terminated";
        default:                     return "Unknown";
    }
}

/**
 * @brief State transition callback
 */
using StateChangeCallback = std::function<void(AppState oldState, AppState newState)>;

/**
 * @brief B-002: Application lifecycle manager
 * 
 * Singleton that tracks the application's lifecycle state and allows
 * subsystems to register for state change notifications.
 */
class AppLifecycle {
public:
    /**
     * @brief Get the singleton instance
     */
    static AppLifecycle& instance() {
        static AppLifecycle s_instance;
        return s_instance;
    }
    
    /**
     * @brief Get current state (thread-safe)
     */
    AppState getState() const {
        return m_state.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if app is in a specific state (thread-safe)
     */
    bool isState(AppState state) const {
        return getState() == state;
    }
    
    /**
     * @brief Check if app is running (thread-safe)
     */
    bool isRunning() const {
        return isState(AppState::Running);
    }
    
    /**
     * @brief Check if app is shutting down or terminated (thread-safe)
     */
    bool isShuttingDown() const {
        AppState s = getState();
        return s == AppState::ShuttingDown || s == AppState::Terminated;
    }
    
    /**
     * @brief Check if initialization is complete (success or failure)
     */
    bool isInitialized() const {
        AppState s = getState();
        return s == AppState::Running || s == AppState::InitFailed;
    }
    
    /**
     * @brief Transition to a new state
     * @param newState Target state
     * @return true if transition was valid, false if rejected
     * 
     * Valid transitions:
     *   Created -> Initializing
     *   Initializing -> Running | InitFailed
     *   InitFailed -> ShuttingDown
     *   Running -> ShuttingDown
     *   ShuttingDown -> Terminated
     */
    bool transitionTo(AppState newState) {
        AppState expected = m_state.load(std::memory_order_acquire);
        
        // Validate transition
        if (!isValidTransition(expected, newState)) {
            return false;
        }
        
        // Atomic transition
        if (m_state.compare_exchange_strong(expected, newState, 
                                            std::memory_order_acq_rel)) {
            // Notify listeners (on current thread)
            notifyStateChange(expected, newState);
            return true;
        }
        
        return false;  // Another thread changed state
    }
    
    /**
     * @brief Register a state change callback
     * @param callback Function to call on state changes
     * @return ID for unregistering (0 on failure)
     */
    size_t addStateChangeCallback(StateChangeCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        size_t id = ++m_nextCallbackId;
        m_callbacks.push_back({id, std::move(callback)});
        return id;
    }
    
    /**
     * @brief Remove a state change callback
     */
    void removeStateChangeCallback(size_t id) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callbacks.erase(
            std::remove_if(m_callbacks.begin(), m_callbacks.end(),
                          [id](const auto& cb) { return cb.first == id; }),
            m_callbacks.end()
        );
    }
    
    /**
     * @brief Reset lifecycle (for testing only)
     */
    void reset() {
        m_state.store(AppState::Created, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_callbacks.clear();
        m_nextCallbackId = 0;
    }

private:
    AppLifecycle() : m_state(AppState::Created), m_nextCallbackId(0) {}
    
    bool isValidTransition(AppState from, AppState to) const {
        switch (from) {
            case AppState::Created:
                return to == AppState::Initializing;
            case AppState::Initializing:
                return to == AppState::Running || to == AppState::InitFailed;
            case AppState::InitFailed:
                return to == AppState::ShuttingDown;
            case AppState::Running:
                return to == AppState::ShuttingDown;
            case AppState::ShuttingDown:
                return to == AppState::Terminated;
            case AppState::Terminated:
                return false;  // Terminal state
            default:
                return false;
        }
    }
    
    void notifyStateChange(AppState oldState, AppState newState) {
        std::vector<StateChangeCallback> callbacksCopy;
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            for (const auto& cb : m_callbacks) {
                callbacksCopy.push_back(cb.second);
            }
        }
        
        for (const auto& callback : callbacksCopy) {
            callback(oldState, newState);
        }
    }
    
    std::atomic<AppState> m_state;
    std::mutex m_callbackMutex;
    std::vector<std::pair<size_t, StateChangeCallback>> m_callbacks;
    size_t m_nextCallbackId;
};

/**
 * @brief RAII guard for lifecycle state transitions
 * 
 * Usage:
 *   {
 *       LifecycleGuard guard(AppState::Initializing);
 *       // ... initialization code ...
 *       guard.complete(AppState::Running);  // or InitFailed on error
 *   }
 */
class LifecycleGuard {
public:
    explicit LifecycleGuard(AppState enterState) : m_completed(false) {
        AppLifecycle::instance().transitionTo(enterState);
    }
    
    ~LifecycleGuard() {
        if (!m_completed) {
            // Default to failure if not explicitly completed
            AppLifecycle::instance().transitionTo(AppState::InitFailed);
        }
    }
    
    bool complete(AppState exitState) {
        m_completed = true;
        return AppLifecycle::instance().transitionTo(exitState);
    }
    
private:
    bool m_completed;
};

} // namespace Aestra
