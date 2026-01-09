// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

/**
 * @file AppBootstrap.h
 * @brief B-001: Application bootstrap modules for isolated initialization
 * 
 * This file separates the initialization of major subsystems into discrete,
 * testable modules. Each module can be initialized/shutdown independently.
 * 
 * THREADING MODEL (B-004):
 * - All bootstrap functions run on the MAIN THREAD during startup
 * - Audio initialization spawns the AUDIO THREAD (real-time, lock-free)
 * - UI runs on the MAIN THREAD (event loop)
 * - Background workers (autosave, waveform decode) use WORKER THREADS
 */

#include <memory>
#include <string>
#include <functional>

// Forward declarations
namespace NomadUI {
    class NUIPlatformBridge;
    class NUIRenderer;
}

namespace Nomad {
    class AsyncLogger;
    
    namespace Audio {
        class AudioDeviceManager;
        class AudioEngine;
    }
}

namespace Nomad {

/**
 * @brief Bootstrap result with error information
 */
struct BootstrapResult {
    bool success = false;
    std::string errorMessage;
    
    static BootstrapResult Success() { return {true, ""}; }
    static BootstrapResult Failure(const std::string& msg) { return {false, msg}; }
    
    operator bool() const { return success; }
};

/**
 * @brief B-001: Logging subsystem initialization
 * 
 * Thread Safety: Must be called from MAIN THREAD before any other subsystem.
 * Dependencies: None
 */
namespace LoggingBootstrap {
    /**
     * @brief Initialize the async logging system
     * @param logFilePath Path for file logger (empty = console only)
     * @return Shared pointer to async logger, or nullptr on failure
     */
    std::shared_ptr<AsyncLogger> initialize(const std::string& logFilePath = "");
    
    /**
     * @brief Shutdown logging (flushes pending messages)
     */
    void shutdown();
}

/**
 * @brief B-001: Platform subsystem initialization
 * 
 * Thread Safety: Must be called from MAIN THREAD.
 * Dependencies: Logging (optional but recommended)
 */
namespace PlatformBootstrap {
    /**
     * @brief Initialize platform layer (window system, input, etc.)
     */
    BootstrapResult initialize();
    
    /**
     * @brief Shutdown platform
     */
    void shutdown();
}

/**
 * @brief B-001: Window and rendering subsystem
 * 
 * Thread Safety: Must be called from MAIN THREAD. Render calls MAIN THREAD only.
 * Dependencies: Platform
 */
namespace WindowBootstrap {
    struct WindowConfig {
        std::string title = "NOMAD DAW";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        bool decorated = false;  // Borderless for custom title bar
    };
    
    /**
     * @brief Create window with OpenGL context
     * @param config Window configuration
     * @param outWindow Output: platform bridge
     * @param outRenderer Output: renderer
     */
    BootstrapResult initialize(
        const WindowConfig& config,
        std::unique_ptr<NomadUI::NUIPlatformBridge>& outWindow,
        std::unique_ptr<NomadUI::NUIRenderer>& outRenderer
    );
    
    /**
     * @brief Shutdown window and renderer
     */
    void shutdown(
        std::unique_ptr<NomadUI::NUIPlatformBridge>& window,
        std::unique_ptr<NomadUI::NUIRenderer>& renderer
    );
}

/**
 * @brief B-001: Audio subsystem initialization
 * 
 * Thread Safety: Initialize from MAIN THREAD. Audio callback runs on AUDIO THREAD.
 * Dependencies: Platform, Logging
 * 
 * AUDIO THREAD CONSTRAINTS (B-005):
 * - NO memory allocations
 * - NO mutex locks (use lock-free queues)
 * - NO system calls that may block
 * - NO logging in hot path (use atomic flags for error reporting)
 */
namespace AudioBootstrap {
    struct AudioConfig {
        uint32_t sampleRate = 48000;
        uint32_t bufferSize = 256;
        int numOutputChannels = 2;
        int numInputChannels = 0;  // Set from device
        std::string preferredDeviceId;  // Empty = default device
    };
    
    /**
     * @brief Audio callback function type
     * 
     * WARNING: This runs on the AUDIO THREAD (real-time).
     * See B-005 constraints above.
     */
    using AudioCallback = int(*)(float* outputBuffer, const float* inputBuffer,
                                  uint32_t nFrames, double streamTime, void* userData);
    
    /**
     * @brief Initialize audio subsystem
     * @param config Audio configuration
     * @param callback Audio callback function
     * @param userData User data for callback
     * @param outManager Output: device manager
     * @param outEngine Output: audio engine
     */
    BootstrapResult initialize(
        const AudioConfig& config,
        AudioCallback callback,
        void* userData,
        std::unique_ptr<Nomad::Audio::AudioDeviceManager>& outManager,
        std::unique_ptr<Nomad::Audio::AudioEngine>& outEngine
    );
    
    /**
     * @brief Shutdown audio (stops stream, releases devices)
     */
    void shutdown(
        std::unique_ptr<Nomad::Audio::AudioDeviceManager>& manager,
        std::unique_ptr<Nomad::Audio::AudioEngine>& engine
    );
}

/**
 * @brief B-001: COM initialization (Windows-specific)
 * 
 * Thread Safety: Must be called from MAIN THREAD before audio.
 * Required for: ASIO drivers, some Windows audio APIs
 */
namespace COMBootstrap {
    /**
     * @brief Initialize COM as STA (Single-Threaded Apartment)
     */
    BootstrapResult initialize();
    
    /**
     * @brief Uninitialize COM
     */
    void shutdown();
}

} // namespace Nomad
