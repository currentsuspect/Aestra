#pragma once

#include "NUITypes.h"
#include "NUIComponent.h"
#include "NUIAdaptiveFPS.h"
#include "../Graphics/NUIRenderer.h"
#include <memory>
#include <chrono>

namespace NomadUI {

/**
 * Main application class for the Nomad UI framework.
 * 
 * Manages the render loop, event processing, and root component.
 */
class NUIApp {
public:
    NUIApp();
    virtual ~NUIApp();
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    /**
     * Initialize the application with the given window size.
     */
    bool initialize(int width, int height, const char* title = "Nomad UI");
    
    /**
     * Shutdown and cleanup.
     */
    void shutdown();
    
    /**
     * Run the main loop (blocks until quit).
     */
    void run();
    
    /**
     * Request the application to quit.
     */
    void quit();
    
    /**
     * Check if the application is running.
     */
    bool isRunning() const { return running_; }
    
    // ========================================================================
    // Components
    // ========================================================================
    
    /**
     * Set the root component.
     */
    void setRootComponent(std::shared_ptr<NUIComponent> root);
    
    /**
     * Get the root component.
     */
    std::shared_ptr<NUIComponent> getRootComponent() const { return rootComponent_; }
    
    // ========================================================================
    // Rendering
    // ========================================================================
    
    /**
     * Get the renderer.
     */
    NUIRenderer* getRenderer() { return renderer_.get(); }
    
    /**
     * Set the target frame rate (default: 60 FPS).
     * @deprecated Use adaptive FPS system instead
     */
    void setTargetFPS(int fps);
    
    /**
     * Get the current FPS.
     */
    float getCurrentFPS() const { return currentFPS_; }
    
    /**
     * Get the delta time of the last frame (in seconds).
     */
    double getDeltaTime() const { return deltaTime_; }
    
    // ========================================================================
    // Adaptive FPS System
    // ========================================================================
    
    /**
     * Get the adaptive FPS manager.
     */
    NUIAdaptiveFPS* getAdaptiveFPS() { return &adaptiveFPS_; }
    
    /**
     * Set adaptive FPS mode.
     */
    void setAdaptiveFPSMode(NUIAdaptiveFPS::Mode mode);
    
    /**
     * Enable/disable adaptive FPS logging.
     */
    void setAdaptiveFPSLogging(bool enabled);
    
    // ========================================================================
    // Events
    // ========================================================================
    
    /**
     * Set the focused component.
     */
    void setFocusedComponent(std::shared_ptr<NUIComponent> component);
    
    /**
     * Get the focused component.
     */
    std::shared_ptr<NUIComponent> getFocusedComponent() const { return focusedComponent_; }
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    NUIUpdateCallback onUpdate;
    NUIRenderCallback onRender;
    
protected:
    /**
     * Process platform events (override for custom platform).
     */
    virtual void processEvents();
    
    /**
     * Update the application state.
     */
    virtual void update(double deltaTime);
    
    /**
     * Render the application.
     */
    virtual void render();
    
    /**
     * Handle mouse events.
     */
    void handleMouseEvent(const NUIMouseEvent& event);
    
    /**
     * Handle key events.
     */
    void handleKeyEvent(const NUIKeyEvent& event);
    
    /**
     * Handle resize events.
     */
    void handleResize(int width, int height);
    
private:
    // Renderer
    std::unique_ptr<NUIRenderer> renderer_;
    
    // Components
    std::shared_ptr<NUIComponent> rootComponent_;
    std::shared_ptr<NUIComponent> focusedComponent_;
    std::shared_ptr<NUIComponent> hoveredComponent_;
    
    // Timing
    std::chrono::high_resolution_clock::time_point lastFrameTime_;
    double deltaTime_ = 0.0;
    float currentFPS_ = 0.0f;
    int targetFPS_ = 60;
    double frameTime_ = 1.0 / 60.0;
    
    // Adaptive FPS
    NUIAdaptiveFPS adaptiveFPS_;
    
    // State
    bool running_ = false;
    bool initialized_ = false;
    
    // Window
    int width_ = 0;
    int height_ = 0;
};

} // namespace NomadUI
