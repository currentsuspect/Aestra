// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#define NOMAD_BUILD_ID "Nomad-2025-Core"

/**
 * @file Main.cpp
 * @brief NOMAD DAW - Main Application Entry Point
 * 
 * This is the main entry point for the NOMAD Digital Audio Workstation.
 * It initializes all core systems:
 * - NomadPlat: Platform abstraction (windowing, input, Vulkan/OpenGL)
 * - NomadUI: UI rendering framework
 * - NomadAudio: Real-time audio engine
 * 
 * @version 1.1.0
 * @license Nomad Studios Source-Available License (NSSAL) v1.0
 */

#include "../NomadPlat/include/NomadPlatform.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUICustomWindow.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIAdaptiveFPS.h"
#include "../NomadUI/Core/NUIFrameProfiler.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Graphics/OpenGL/NUIRendererGL.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include "../NomadAudio/include/NomadAudio.h"
#include "../NomadAudio/include/AudioEngine.h"
#include "../NomadAudio/include/AudioGraphBuilder.h"
#include "../NomadAudio/include/AudioCommandQueue.h"
#include "../NomadAudio/include/AudioRT.h"
#include "../NomadAudio/include/PreviewEngine.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadCore/include/NomadProfiler.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "../NomadAudio/include/ClipSource.h"
#include "../NomadAudio/include/WaveformCache.h"
#include "MixerViewModel.h"
#include "TransportBar.h"
#include "AudioSettingsDialog.h"
#include "FileBrowser.h"
#include "FilePreviewPanel.h"
#include "AudioVisualizer.h"
#include "TrackUIComponent.h"
#include "PerformanceHUD.h"
#include "TrackManagerUI.h"
#include "FPSDisplay.h"
#include "ProjectSerializer.h"
#include "ConfirmationDialog.h"
#include "ViewTypes.h"
#include "PatternBrowserPanel.h"
#include "ArsenalPanel.h"
#include "NomadContent.h"
#include "../NomadUI/Widgets/NUISegmentedControl.h"
#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>

// Windows-specific includes removed - use NomadPlat abstraction instead

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Nomad;
using namespace NomadUI;
using namespace Nomad::Audio;

const std::string BROWSER_SETTINGS_FILE = "browser_settings.json";

namespace {
uint64_t estimateCycleHz() {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t c0 = Nomad::Audio::RT::readCycleCounter();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t1 = std::chrono::steady_clock::now();
    const uint64_t c1 = Nomad::Audio::RT::readCycleCounter();
    const double sec = std::chrono::duration_cast<std::chrono::duration<double>>(t1 - t0).count();
    if (sec <= 0.0 || c1 <= c0) return 0;
    return static_cast<uint64_t>(static_cast<double>(c1 - c0) / sec);
#else
    return 0;
#endif
}
} // namespace

// =============================================================================
// SECTION: Key Code Conversion
// =============================================================================

/**
 * @brief Convert Nomad::KeyCode to NomadUI::NUIKeyCode
 * 
 * Nomad::KeyCode uses actual key codes (Enter=13), while NUIKeyCode uses sequential enum values.
 * This function maps between the two systems.
 */
NomadUI::NUIKeyCode convertToNUIKeyCode(int key) {
    using KC = Nomad::KeyCode;
    using NUIKC = NomadUI::NUIKeyCode;
    
    // Map Nomad platform key codes to NomadUI key codes
    if (key == static_cast<int>(KC::Space)) return NUIKC::Space;
    if (key == static_cast<int>(KC::Enter)) return NUIKC::Enter;
    if (key == static_cast<int>(KC::Escape)) return NUIKC::Escape;
    if (key == static_cast<int>(KC::Tab)) return NUIKC::Tab;
    if (key == static_cast<int>(KC::Backspace)) return NUIKC::Backspace;
    if (key == static_cast<int>(KC::Delete)) return NUIKC::Delete;
    
    // Arrow keys
    if (key == static_cast<int>(KC::Left)) return NUIKC::Left;
    if (key == static_cast<int>(KC::Right)) return NUIKC::Right;
    if (key == static_cast<int>(KC::Up)) return NUIKC::Up;
    if (key == static_cast<int>(KC::Down)) return NUIKC::Down;
    
    // Letters A-Z
    if (key >= static_cast<int>(KC::A) && key <= static_cast<int>(KC::Z)) {
        int offset = key - static_cast<int>(KC::A);
        return static_cast<NUIKC>(static_cast<int>(NUIKC::A) + offset);
    }
    
    // Numbers 0-9
    if (key >= static_cast<int>(KC::Num0) && key <= static_cast<int>(KC::Num9)) {
        int offset = key - static_cast<int>(KC::Num0);
        return static_cast<NUIKC>(static_cast<int>(NUIKC::Num0) + offset);
    }
    
    // Function keys F1-F12
    if (key >= static_cast<int>(KC::F1) && key <= static_cast<int>(KC::F12)) {
        int offset = key - static_cast<int>(KC::F1);
        return static_cast<NUIKC>(static_cast<int>(NUIKC::F1) + offset);
    }
    
    return NUIKC::Unknown;
}

// =============================================================================
// SECTION: Helper UI Components
// =============================================================================

/**
 * @brief Custom label for the Scope Indicator that manually draws the Play icon via NUIIcon (SVG).
 * This ensures consistent vector rendering matching the Transport Bar.
 */
class ScopeIndicatorLabel : public NomadUI::NUILabel {
public:
    ScopeIndicatorLabel(const std::string& text) : NUILabel(text) {
        // Play icon (Rounded Triangle) - Matches TransportBar
        const char* playSvg = R"(
            <svg viewBox="0 0 24 24" fill="currentColor">
                <path d="M8 6.82v10.36c0 .79.87 1.27 1.54.84l8.14-5.18c.62-.39.62-1.29 0-1.69L9.54 5.98C8.87 5.55 8 6.03 8 6.82z"/>
            </svg>
        )";
        m_icon = std::make_shared<NomadUI::NUIIcon>(playSvg);
    }

    void onRender(NomadUI::NUIRenderer& renderer) override {
        // Calculate bounds and geometry
        auto bounds = getBounds();
        float fontSize = getFontSize();
        float iconSize = fontSize * 1.0f;   // Icon size relative to font
        float padding = 6.0f;               // Spacing between icon and text
        
        // Vertical centering
        float centerY = bounds.y + bounds.height * 0.5f;
        float iconY = centerY - iconSize * 0.5f;
        
        // 1. Draw SVG Icon
        if (m_icon) {
            m_icon->setColor(getTextColor());
            m_icon->setBounds(NomadUI::NUIRect(bounds.x, iconY, iconSize, iconSize));
            m_icon->onRender(renderer);
        }
        
        // 2. Adjust Text Position
        // Extract raw text without the special hex char if present
        std::string fullText = getText();
        std::string displayLabel = fullText;
        
        // If text starts with the triangle bytes (E2 96 B6), strip them
        if (fullText.size() >= 3 && 
            (unsigned char)fullText[0] == 0xE2 && 
            (unsigned char)fullText[1] == 0x96 && 
            (unsigned char)fullText[2] == 0xB6) {
            
            // +1 for the space that follows usually
            size_t substrIdx = 3; 
            if (fullText.size() > 3 && fullText[3] == ' ') substrIdx++;
            
            displayLabel = fullText.substr(substrIdx);
        }
        
        // Draw the text offset by icon width
        float textX = bounds.x + iconSize + padding;
        float textY = bounds.y + (bounds.height - renderer.measureText(displayLabel, fontSize).height) * 0.5f;
        
        renderer.drawText(displayLabel, NomadUI::NUIPoint(std::round(textX), std::round(textY)), fontSize, getTextColor());
    }

private:
    std::shared_ptr<NomadUI::NUIIcon> m_icon;
};

// OverlayLayer class is now in OverlayLayer.h
// NomadContent class is now in NomadContent.h/cpp


/**
 * @brief Root component that contains the custom window
 */
class NomadRootComponent : public NUIComponent {
public:
    NomadRootComponent() = default;
    
    void setCustomWindow(std::shared_ptr<NUICustomWindow> window) {
        m_customWindow = window;
        addChild(m_customWindow);
    }
    
    void setAudioSettingsDialog(std::shared_ptr<AudioSettingsDialog> dialog) {
        // Store reference to dialog for debugging
        m_audioSettingsDialog = dialog;
    }
    
    void setFPSDisplay(std::shared_ptr<FPSDisplay> fpsDisplay) {
        m_fpsDisplay = fpsDisplay;
        addChild(m_fpsDisplay);
    }
    
    void setPerformanceHUD(std::shared_ptr<PerformanceHUD> perfHUD) {
        m_performanceHUD = perfHUD;
        addChild(m_performanceHUD);
    }
    
    std::shared_ptr<PerformanceHUD> getPerformanceHUD() const {
        return m_performanceHUD;
    }
    
    void onUpdate(double deltaTime) override {
        NUIComponent::updateGlobalTooltip(deltaTime);
        NUIComponent::onUpdate(deltaTime);
    }

    void onRender(NUIRenderer& renderer) override {
        // Don't draw background here - let custom window handle it
        // Just render children (custom window and audio settings dialog)
        renderChildren(renderer);
        
        // Audio settings dialog is handled by its own render method
        
        // Fix: Render global tooltips last so they appear on top of everything
        NUIComponent::renderGlobalTooltip(renderer);
    }
    
    void onResize(int width, int height) override {
        if (m_customWindow) {
            m_customWindow->setBounds(NUIRect(0, 0, width, height));
        }
        
        // Resize all children (including audio settings dialog)
        for (auto& child : getChildren()) {
            if (child) {
                child->onResize(width, height);
            }
        }
        
        NUIComponent::onResize(width, height);
    }
    
private:
    std::shared_ptr<NUICustomWindow> m_customWindow;
    std::shared_ptr<AudioSettingsDialog> m_audioSettingsDialog;
    std::shared_ptr<FPSDisplay> m_fpsDisplay;
    std::shared_ptr<PerformanceHUD> m_performanceHUD;
};

/**
 * @brief Get the application data directory path
 * 
 * Returns a platform-specific path for storing application data using NomadPlat abstraction.
 * Creates the directory if it doesn't exist.
 */
std::string getAppDataPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        // Fallback if platform not initialized
        return std::filesystem::current_path().string();
    }
    
    std::string appDataDir = utils->getAppDataPath("Nomad");
    
    // Create directory if it doesn't exist
    std::error_code ec;
    if (!std::filesystem::create_directories(appDataDir, ec) && ec) {
        Log::warning("Failed to create app data directory: " + appDataDir + " (" + ec.message() + ")");
    }
    
    return appDataDir;
}

/**
 * @brief Get the autosave file path
 */
std::string getAutosavePath() {
    return (std::filesystem::path(getAppDataPath()) / "autosave.nomadproj").string();
}

/**
 * @brief Main application class
 * 
 * Manages the lifecycle of all NOMAD subsystems and the main event loop.
 */
class NomadApp {
public:
    NomadApp() 
        : m_running(false)
        , m_audioInitialized(false)
    {
        // Configure adaptive FPS system
        NomadUI::NUIAdaptiveFPS::Config fpsConfig;
        fpsConfig.fps30 = 30.0;
        fpsConfig.fps60 = 60.0;
        fpsConfig.idleTimeout = 2.0;                // 2 seconds idle before lowering FPS
        fpsConfig.performanceThreshold = 0.018;     // 18ms max frame time for 60 FPS
        fpsConfig.performanceSampleCount = 10;      // Average over 10 frames
        fpsConfig.transitionSpeed = 0.05;           // Smooth transition
        fpsConfig.enableLogging = false;            // Disable by default (can be toggled)
        
        m_adaptiveFPS = std::make_unique<NomadUI::NUIAdaptiveFPS>(fpsConfig);
    }

    ~NomadApp() {
        shutdown();
    }

    /**
     * @brief Initialize all subsystems
     */
    bool initialize() {
        Log::info("NOMAD DAW v1.0.0 - Initializing...");

        // Initialize platform
        if (!Platform::initialize()) {
            Log::error("Failed to initialize platform");
            return false;
        }
        Log::info("Platform initialized");

    // Create window using NUIPlatformBridge
    m_window = std::make_unique<NUIPlatformBridge>();
        
        WindowDesc desc;
        desc.title = "NOMAD DAW v1.0";
        desc.width = 1280;
        desc.height = 720;
        desc.resizable = true;
        desc.decorated = false;  // Borderless for custom title bar

        if (!m_window->create(desc)) {
            Log::error("Failed to create window");
            return false;
        }
        
        Log::info("Window created");

        // Create OpenGL context
        if (!m_window->createGLContext()) {
            Log::error("Failed to create OpenGL context");
            return false;
        }

        if (!m_window->makeContextCurrent()) {
            Log::error("Failed to make OpenGL context current");
            return false;
        }

        Log::info("OpenGL context created");

        // Initialize UI renderer (this will initialize GLAD internally)
        try {
            // Use raw pointer for initialization to avoid unique_ptr casting issues
            auto* glRenderer = new NUIRendererGL();
            if (!glRenderer->initialize(desc.width, desc.height)) {
                delete glRenderer; // Clean up on failure
                Log::error("Failed to initialize UI renderer");
                return false;
            }
            
            // Enable render caching by default for performance
            glRenderer->setCachingEnabled(true);
            
            // Transfer ownership to unique_ptr
            m_renderer.reset(glRenderer);
            
            Log::info("UI renderer initialized");
            
            // Enable debug logging for FBO cache (temporary for testing)
            #ifdef _DEBUG
            if (m_renderer->getRenderCache()) {
                // m_renderer->getRenderCache()->setDebugEnabled(true);
                // Log::info("FBO cache debug logging enabled");
            }
            #endif
        }
        catch (const std::exception& e) {
            std::string errorMsg = "Exception during renderer initialization: ";
            errorMsg += e.what();
            Log::error(errorMsg);
            return false;
        }

        // Initialize audio engine
        m_audioManager = std::make_unique<AudioDeviceManager>();
        m_audioEngine = std::make_unique<AudioEngine>();
        if (!m_audioManager->initialize()) {
            Log::error("Failed to initialize audio engine");
            // Continue without audio for now
            m_audioInitialized = false;
        } else {
            Log::info("Audio engine initialized");
            
            try {
                // Get default audio device with error handling
                
                // First check if any devices are available
                // Retry up to 3 times to work around WASAPI enumeration failures
                std::vector<AudioDeviceInfo> devices;
                int retryCount = 0;
                const int maxRetries = 3;
                
                while (devices.empty() && retryCount < maxRetries) {
                    if (retryCount > 0) {
                        Log::info("Retry " + std::to_string(retryCount) + "/" + std::to_string(maxRetries) + " - waiting for WASAPI...");
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    devices = m_audioManager->getDevices();
                    retryCount++;
                }
                
                if (devices.empty()) {
                    Log::warning("No audio devices found. Please check your audio drivers.");
                    Log::warning("Continuing without audio support.");
                    m_audioInitialized = false;
                } else {
                    Log::info("Audio devices found");
                    
                    // Find first output device (instead of relying on getDefaultOutputDevice which can fail)
                    AudioDeviceInfo outputDevice;
                    bool foundOutput = false;
                    
                    for (const auto& device : devices) {
                        if (device.maxOutputChannels > 0) {
                            outputDevice = device;
                            foundOutput = true;
                            break;
                        }
                    }
                    
                    if (!foundOutput) {
                        Log::warning("No output audio device found");
                        m_audioInitialized = false;
                    } else {
                        Log::info("Using audio device: " + outputDevice.name);
                        
                        // Configure audio stream
                        AudioStreamConfig config;
                        config.deviceId = outputDevice.id;
                        config.sampleRate = 48000;
                        
                        // Adaptive buffer size: Exclusive mode can use tiny buffers, Shared mode needs larger
                        // The driver will try Exclusive first, then fallback to Shared if blocked
                        // Shared mode will auto-adjust to a safe buffer size if 128 is too small
                        config.bufferSize = 256;  // Safe default - works well for both modes (5.3ms @ 48kHz)
                        
                        config.numInputChannels = 0;
                        config.numOutputChannels = 2;
                        
                        if (m_audioEngine) {
                            m_audioEngine->setSampleRate(config.sampleRate);
                            m_audioEngine->setBufferConfig(config.bufferSize, config.numOutputChannels);
                        }
                        
                        // Store config for later restoration
                        m_mainStreamConfig = config;

                        // Open audio stream with a simple callback
                        if (m_audioManager->openStream(config, audioCallback, this)) {
                            Log::info("Audio stream opened");
                            
                            // Start the audio stream
	                            if (m_audioManager->startStream()) {
	                                Log::info("Audio stream started");
	                                m_audioInitialized = true;
	                                
	                                // Enable auto-buffer scaling to recover from underruns.
	                                // This runs on the main thread and will increase buffer size
	                                // if the driver reports repeated underruns.
	                                m_audioManager->setAutoBufferScaling(true, 5);
	                                
	                                double actualRate = static_cast<double>(m_audioManager->getStreamSampleRate());
                                if (actualRate <= 0.0) {
                                    actualRate = static_cast<double>(config.sampleRate);
                                }
                                // Keep engine, track managers, and graph in sync with the real device rate.
                                if (m_audioEngine) {
                                    m_audioEngine->setSampleRate(static_cast<uint32_t>(actualRate));
                                    m_audioEngine->setBufferConfig(config.bufferSize, config.numOutputChannels);
                                    if (m_content && m_content->getTrackManager()) {
                                        m_audioEngine->setMeterSnapshots(m_content->getTrackManager()->getMeterSnapshots());
                                        m_audioEngine->setContinuousParams(m_content->getTrackManager()->getContinuousParams());
                                        m_audioEngine->setChannelSlotMap(m_content->getTrackManager()->getChannelSlotMapShared());
                                        auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), actualRate);
                                        m_audioEngine->setGraph(graph);
                                    }
                                }
                                m_mainStreamConfig.sampleRate = static_cast<uint32_t>(actualRate);
                                if (m_audioEngine) {
                                    const uint64_t hz = estimateCycleHz();
                                    if (hz > 0) {
                                        m_audioEngine->telemetry().cycleHz.store(hz, std::memory_order_relaxed);
                                    }
                                }
                                if (m_content && m_content->getTrackManager()) {
                                    m_content->getTrackManager()->setOutputSampleRate(actualRate);
                                }
                                if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                                    m_content->getTrackManagerUI()->getTrackManager()->setOutputSampleRate(actualRate);
                                }
                                
                                // Load metronome click sounds (low pitch for downbeat, high pitch for upbeats)
                                if (m_audioEngine) {
                                    // Paths relative to build/bin/Release, going up to project root
                                    m_audioEngine->loadMetronomeClicks(
                                        "../../../NomadAudio/assets/nomad_metronome.wav",      // Downbeat (low pitch)
                                        "../../../NomadAudio/assets/nomad_metronome_up.wav"   // Upbeat (high pitch)
                                    );
                                    m_audioEngine->setBPM(120.0f);  // Default BPM
                                    // Metronome disabled by default - user enables via toggle button
                                }
                            } else {
                                Log::error("Failed to start audio stream");
                                m_audioInitialized = false;
                            }
                        } else {
                            Log::warning("Failed to open audio stream");
                            m_audioInitialized = false;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::string errorMsg = "Exception while initializing audio: ";
                errorMsg += e.what();
                Log::error(errorMsg);
                Log::warning("Continuing without audio support.");
                m_audioInitialized = false;
            } catch (...) {
                Log::error("Unknown exception while initializing audio");
                Log::warning("Continuing without audio support.");
                m_audioInitialized = false;
            }
        }

        // Initialize Nomad theme
        auto& themeManager = NUIThemeManager::getInstance();
        themeManager.setActiveTheme("nomad-dark");
        Log::info("Theme system initialized");

        // Load YAML configuration for pixel-perfect customization
        // Note: This would be implemented in the config loader
        Log::info("Loading UI configuration...");
        // NUIConfigLoader::getInstance().loadConfig("NomadUI/Config/nomad_ui_config.yaml");
        Log::info("UI configuration loaded");
        
        // Create root component
        m_rootComponent = std::make_shared<NomadRootComponent>();
        m_rootComponent->setBounds(NUIRect(0, 0, desc.width, desc.height));
        
        // Create custom window with title bar
        m_customWindow = std::make_shared<NUICustomWindow>();
        m_customWindow->setTitle("NOMAD DAW v1.0");
        m_customWindow->setBounds(NUIRect(0, 0, desc.width, desc.height));
        
        // Create content area
        m_content = std::make_shared<NomadContent>();
        m_content->setPlatformBridge(m_window.get());
        m_content->setAudioStatus(m_audioInitialized);
        m_customWindow->setContent(m_content.get());
        
        // Add view focus toggle to title bar (for proper click handling)
        if (auto titleBar = m_customWindow->getTitleBar()) {
            if (m_content->getViewToggle()) {
                titleBar->addChild(m_content->getViewToggle());
            }
        }
        
        // Pass platform window to TrackManagerUI for cursor control (Task: Selection Tool Enhancements)
        if (m_content->getTrackManagerUI()) {
            m_content->getTrackManagerUI()->setPlatformWindow(m_window.get());
        }
        
        // Restore File Browser state (Favorites, History, etc.)
        if (m_content->getFileBrowser()) {
            m_content->getFileBrowser()->loadState(BROWSER_SETTINGS_FILE);
        }

        // Build initial audio graph for engine (uses default tracks created in NomadContent)
        if (m_audioEngine && m_content && m_content->getTrackManager()) {
            m_audioEngine->setMeterSnapshots(m_content->getTrackManager()->getMeterSnapshots());
            m_audioEngine->setContinuousParams(m_content->getTrackManager()->getContinuousParams());
            m_audioEngine->setChannelSlotMap(m_content->getTrackManager()->getChannelSlotMapShared());
            auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), m_mainStreamConfig.sampleRate);
            m_audioEngine->setGraph(graph);
        }
        
        // Wire metronome toggle callback from TransportBar to AudioEngine
        if (m_content && m_content->getTransportBar() && m_audioEngine) {
            m_content->getTransportBar()->setOnMetronomeToggle([this](bool enabled) {
                if (m_audioEngine) {
                    m_audioEngine->setMetronomeEnabled(enabled);
                    Log::info(std::string("Metronome ") + (enabled ? "enabled" : "disabled"));
                }
            });
            // Also wire tempo changes to keep BPM in sync
            m_content->getTransportBar()->setOnTempoChange([this](float bpm) {
                if (m_audioEngine) {
                    m_audioEngine->setBPM(bpm);
                    Log::info("BPM changed to " + std::to_string(static_cast<int>(bpm)));
                }
            });
            // Wire time signature changes from TransportInfoContainer
            if (auto* infoContainer = m_content->getTransportBar()->getInfoContainer()) {
                if (auto* timeSigDisplay = infoContainer->getTimeSignatureDisplay()) {
                    timeSigDisplay->setOnTimeSignatureChange([this](int beatsPerBar) {
                        if (m_audioEngine) {
                            m_audioEngine->setBeatsPerBar(beatsPerBar);
                            Log::info("Time signature changed to " + std::to_string(beatsPerBar) + "/4");
                        }
                        // Sync TrackManagerUI grid
                        if (m_content && m_content->getTrackManagerUI()) {
                            m_content->getTrackManagerUI()->setBeatsPerBar(beatsPerBar);
                        }
                    });
                }
            }
        }
        
        // Wire loop preset callback from TrackManagerUI to AudioEngine
        if (m_content && m_content->getTrackManagerUI() && m_audioEngine) {
            m_content->getTrackManagerUI()->setOnLoopPresetChanged([this](int preset) {
                if (!m_audioEngine) return;
                
                // Preset: 0=Off, 1=1Bar, 2=2Bars, 3=4Bars, 4=8Bars, 5=Selection
                if (preset == 0) {
                    m_audioEngine->setLoopEnabled(false);
                    Log::info("Loop disabled");
                } else if (preset >= 1 && preset <= 4) {
                    // Calculate loop region in beats based on preset
                    int beatsPerBar = m_audioEngine->getBeatsPerBar();
                    double loopBars = (preset == 1) ? 1.0 : (preset == 2) ? 2.0 : (preset == 3) ? 4.0 : 8.0;
                    double loopBeats = loopBars * beatsPerBar;
                    
                    // FL Studio-style: loop from beat 0 (not current position)
                    m_audioEngine->setLoopRegion(0.0, loopBeats);
                    m_audioEngine->setLoopEnabled(true);
                    if (auto trackManagerUI = m_content->getTrackManagerUI()) {
                        trackManagerUI->setLoopRegion(0.0, loopBeats, true);  // Update visual markers
                    }
                    Log::info("Loop enabled: 0.0 - " + std::to_string(loopBeats) + " beats (" + std::to_string(static_cast<int>(loopBars)) + " bars)");
                } else if (preset == 5) {
                    // Selection-based loop (FL Studio: selection overrides bar looping)
                    if (auto trackManagerUI = m_content->getTrackManagerUI()) {
                        auto selection = trackManagerUI->getSelectionBeatRange();
                        if (selection.second > selection.first) {
                            m_audioEngine->setLoopRegion(selection.first, selection.second);
                            m_audioEngine->setLoopEnabled(true);
                            if (auto trackManagerUI = m_content->getTrackManagerUI()) {
                                trackManagerUI->setLoopRegion(selection.first, selection.second, true);  // Update visual markers
                            }
                            Log::info("Looping selection: " + std::to_string(selection.first) + " - " + std::to_string(selection.second));
                        } else {
                            // No valid selection - fall back to smart 1-bar loop
                            int beatsPerBar = m_audioEngine->getBeatsPerBar();
                            double currentBeat = 0.0;
                            if (m_content && m_content->getTrackManager()) {
                                double posSeconds = m_content->getTrackManager()->getPosition();
                                double bpm = m_audioEngine->getBPM();
                                currentBeat = (posSeconds * bpm) / 60.0;
                            }
                            double currentBar = std::floor(currentBeat / beatsPerBar);
                            double loopStartBeat = currentBar * beatsPerBar;
                            double loopEndBeat = loopStartBeat + beatsPerBar;
                            
                            m_audioEngine->setLoopRegion(loopStartBeat, loopEndBeat);
                            m_audioEngine->setLoopEnabled(true);
                            Log::warning("Loop Selection: No valid selection found, defaulting to 1-bar loop");
                        }
                    }
                }
            });
            
            // FL Studio-style: Enable 1-bar loop by default on startup
            if (m_audioEngine) {
                int beatsPerBar = m_audioEngine->getBeatsPerBar();
                m_audioEngine->setLoopRegion(0.0, static_cast<double>(beatsPerBar));
                m_audioEngine->setLoopEnabled(true);
                Log::info("Default 1-bar loop enabled (FL Studio mode)");
            }
        }
        
        // TODO: Implement async project loading with progress indicator
        // Currently disabled because loading audio files synchronously causes UI freeze
        // The autosave contains 50+ tracks and references to large WAV files
        // Future implementation should:
        //   1. Show a loading splash/progress bar
        //   2. Load track metadata first (instant)
        //   3. Load audio files in background thread
        //   4. Update UI as each file loads
        // For now, start fresh each time
        Log::info("[STARTUP] Project auto-load disabled (TODO: implement async loading)");
        ProjectSerializer::LoadResult loadResult;
        loadResult.ok = false;
        if (loadResult.ok) {
            if (m_content->getTransportBar()) {
                m_content->getTransportBar()->setTempo(static_cast<float>(loadResult.tempo));
                m_content->getTransportBar()->setPosition(loadResult.playhead);
            }
            if (m_content->getTrackManager()) {
                m_content->getTrackManager()->setPosition(loadResult.playhead);
            }
        }
        
        // v3.0: Latency compensation is now managed by the Audio Engine's internal clock 
        // and PlaylistRuntimeSnapshot offsets. Per-track latency setters are deprecated.
        
        // Wire up transport bar callbacks
        if (m_content->getTransportBar()) {
            auto* transport = m_content->getTransportBar();
            
            transport->setOnPlay([this]() {
                Log::info("Transport: Play");
                // Stop preview when transport starts
                if (m_content) {
                    m_content->stopSoundPreview();
                }
                
                // **Focus-aware playback routing**
                if (m_content && m_content->getViewFocus() == ViewFocus::Arsenal) {
                    // Arsenal Focus - Play active pattern
                    Log::info("[Focus] Arsenal mode - Playing pattern");
                    if (m_content->getTrackManager()) {
                        auto& patternMgr = m_content->getTrackManager()->getPatternManager();
                        PatternID activePattern = m_content->getActivePatternID();
                        if (activePattern.isValid()) {
                            m_content->getTrackManager()->playPatternInArsenal(activePattern);
                        } else {
                            Log::warning("[Focus] No active pattern to play");
                        }
                    }
                } else {
                    // Timeline Focus - Play arrangement
                    Log::info("[Focus] Timeline mode - Playing arrangement");
                    if (m_content && m_content->getTrackManagerUI()) {
                        m_content->getTrackManagerUI()->getTrackManager()->play();
                    }
                    
                    // Rebuild graph immediately before starting playback to ensure it's current
                    if (m_audioEngine && m_content && m_content->getTrackManager()) {
                        double sampleRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                        if (m_audioManager) {
                            double actual = static_cast<double>(m_audioManager->getStreamSampleRate());
                            if (actual > 0.0) sampleRate = actual;
                        }
                        // Rebuild the graph with latest track data
                        m_audioEngine->setMeterSnapshots(m_content->getTrackManager()->getMeterSnapshots());
                        m_audioEngine->setContinuousParams(m_content->getTrackManager()->getContinuousParams());
                        m_audioEngine->setChannelSlotMap(m_content->getTrackManager()->getChannelSlotMapShared());
                        auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), sampleRate);
                        m_audioEngine->setGraph(graph);
                        // Clear the dirty flag since we just rebuilt
                        m_content->getTrackManager()->consumeGraphDirty();
                        
                        // Notify audio engine transport
                        AudioQueueCommand cmd;
                        cmd.type = AudioQueueCommandType::SetTransportState;
                        cmd.value1 = 1.0f;
                        double posSeconds = m_content->getTrackManager()->getPosition();
                        cmd.samplePos = static_cast<uint64_t>(posSeconds * sampleRate);
                        m_audioEngine->commandQueue().push(cmd);
                        
                        Log::info("Transport: Graph rebuilt with " + std::to_string(graph.tracks.size()) + " tracks");
                        
                        // Build arrangement waveform for scrolling display
                        if (m_content->getWaveformVisualizer()) {
                            auto& playlist = m_content->getTrackManager()->getPlaylistModel();
                            auto& sourceManager = m_content->getTrackManager()->getSourceManager();
                            double totalBeats = playlist.getTotalDurationBeats();
                            double projectDuration = playlist.beatToSeconds(totalBeats);
                            
                            // Find the first audio clip with valid data
                            ClipSource* firstSource = nullptr;
                            double clipStartTime = 0.0;
                            for (size_t laneIdx = 0; laneIdx < playlist.getLaneCount() && !firstSource; ++laneIdx) {
                                PlaylistLaneID laneId = playlist.getLaneId(laneIdx);
                                auto* lane = playlist.getLane(laneId);
                                if (!lane) continue;
                                for (const auto& clip : lane->clips) {
                                    if (!clip.patternId.isValid()) continue;
                                    auto pattern = m_content->getTrackManager()->getPatternManager().getPattern(clip.patternId);
                                    if (!pattern || !pattern->isAudio()) continue;
                                    const auto* audioPayload = std::get_if<AudioSlicePayload>(&pattern->payload);
                                    if (!audioPayload) continue;
                                    firstSource = sourceManager.getSource(audioPayload->audioSourceId);
                                    if (firstSource && firstSource->isReady()) {
                                        // Get clip start time in seconds
                                        clipStartTime = playlist.beatToSeconds(clip.startBeat);
                                        break;
                                    }
                                    firstSource = nullptr;
                                }
                            }
                            
                            if (firstSource && firstSource->isReady()) {
                                auto cache = std::make_shared<WaveformCache>();
                                auto buffer = firstSource->getBuffer();
                                if (buffer && buffer->numFrames > 0) {
                                    cache->buildFromBuffer(*buffer);
                                    double sampleRate = static_cast<double>(buffer->sampleRate);
                                    m_content->getWaveformVisualizer()->setArrangementWaveform(cache, projectDuration, clipStartTime, sampleRate);
                                    Log::info("Arrangement waveform built: " + std::to_string(buffer->numFrames) + " frames at " + std::to_string(clipStartTime) + "s");
                                }
                            } else {
                                // No clips with audio - just set duration for placeholder display
                                m_content->getWaveformVisualizer()->setArrangementWaveform(nullptr, projectDuration, 0.0, 48000.0);
                            }
                        }
                    }
                }
            });
            
            transport->setOnPause([this]() {
                Log::info("Transport: Pause");
                if (m_content && m_content->getTrackManagerUI()) {
                    m_content->getTrackManagerUI()->getTrackManager()->pause();
                }
                if (m_audioEngine) {
                    AudioQueueCommand cmd;
                    cmd.type = AudioQueueCommandType::SetTransportState;
                    cmd.value1 = 0.0f;
                    // Preserve current position on pause (avoid unintended seek-to-zero).
                    if (m_content && m_content->getTrackManager()) {
                        double sampleRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                        if (m_audioManager) {
                            double actual = static_cast<double>(m_audioManager->getStreamSampleRate());
                            if (actual > 0.0) sampleRate = actual;
                        }
                        double posSeconds = m_content->getTrackManager()->getPosition();
                        cmd.samplePos = static_cast<uint64_t>(posSeconds * sampleRate);
                    }
                    m_audioEngine->commandQueue().push(cmd);
                }
            });
            
            transport->setOnStop([this, transport]() {
                Log::info("Transport: Stop");
                // Stop preview when transport stops
                if (m_content) {
                m_content->stopSoundPreview();
            }
            if (m_content && m_content->getTrackManagerUI()) {
                m_content->getTrackManagerUI()->getTrackManager()->stop();
            }
            // Reset position to 0
            transport->setPosition(0.0);
            if (m_audioEngine) {
                double sampleRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                if (m_audioManager) {
                    double actual = static_cast<double>(m_audioManager->getStreamSampleRate());
                    if (actual > 0.0) sampleRate = actual;
                }
                AudioQueueCommand cmd;
                cmd.type = AudioQueueCommandType::SetTransportState;
                cmd.value1 = 0.0f;
                cmd.samplePos = 0;
                m_audioEngine->commandQueue().push(cmd);
            }
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->setPosition(0.0);
            }
        });
            
            transport->setOnTempoChange([this](float bpm) {
                std::stringstream ss;
                ss << "Transport: Tempo changed to " << bpm << " BPM";
                Log::info(ss.str());
                
                // Update AudioEngine BPM
                if (m_audioEngine) {
                    m_audioEngine->setBPM(bpm);
                }
                
                // Update TrackManager/PlaylistModel BPM
                if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                    m_content->getTrackManagerUI()->getTrackManager()->getPlaylistModel().setBPM(static_cast<double>(bpm));
                }
            });

            // Keep transport time in sync for scrubbing and playback
            if (m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
                auto waveformVis = m_content->getWaveformVisualizer();
                trackManager->setOnPositionUpdate([transport, waveformVis](double pos) {
                    transport->setPosition(pos);
                    // Update scrolling waveform display
                    if (waveformVis) {
                        waveformVis->setTransportPosition(pos);
                    }
                });
            }
        }
        
        // Create audio settings dialog (pass TrackManager so test sound can be added as a track)
        auto trackManager = m_content->getTrackManagerUI() ? m_content->getTrackManagerUI()->getTrackManager() : nullptr;
        m_audioSettingsDialog = std::make_shared<AudioSettingsDialog>(m_audioManager.get(), trackManager);
        m_audioSettingsDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
        m_audioSettingsDialog->setOnApply([this]() {
            Log::info("Audio settings applied");
            
            // Sync buffer size and sample rate from actual driver to AudioEngine
            // Use actual values from driver (may differ from requested)
            if (m_audioManager && m_audioEngine) {
                uint32_t actualSampleRate = m_audioManager->getStreamSampleRate();
                uint32_t actualBufferSize = m_audioManager->getStreamBufferSize();
                
                // Fallback to dialog values if driver reports 0
                if (actualSampleRate == 0 && m_audioSettingsDialog) {
                    actualSampleRate = m_audioSettingsDialog->getSelectedSampleRate();
                }
                if (actualBufferSize == 0 && m_audioSettingsDialog) {
                    actualBufferSize = m_audioSettingsDialog->getSelectedBufferSize();
                }
                
                // Always update AudioEngine with actual driver values
                if (actualSampleRate > 0) {
                    m_mainStreamConfig.sampleRate = actualSampleRate;
                    m_audioEngine->setSampleRate(actualSampleRate);
                    Log::info("AudioEngine sample rate synced to: " + std::to_string(actualSampleRate));
                }
                
                if (actualBufferSize > 0) {
                    m_mainStreamConfig.bufferSize = actualBufferSize;
                    m_audioEngine->setBufferConfig(actualBufferSize, m_mainStreamConfig.numOutputChannels);
                    Log::info("AudioEngine buffer size synced to: " + std::to_string(actualBufferSize));
                }
                
                // Sync Resampling Quality
                if (m_audioSettingsDialog) {
                    auto mode = m_audioSettingsDialog->getSelectedResamplingMode();
                    Nomad::Audio::Interpolators::InterpolationQuality quality;
                    
                    switch (mode) {
                        case Nomad::Audio::ResamplingMode::Fast:    quality = Nomad::Audio::Interpolators::InterpolationQuality::Cubic; break;
                        case Nomad::Audio::ResamplingMode::Medium:  quality = Nomad::Audio::Interpolators::InterpolationQuality::Sinc8; break;
                        case Nomad::Audio::ResamplingMode::High:    quality = Nomad::Audio::Interpolators::InterpolationQuality::Sinc16; break;
                        case Nomad::Audio::ResamplingMode::Ultra:   quality = Nomad::Audio::Interpolators::InterpolationQuality::Sinc32; break;
                        case Nomad::Audio::ResamplingMode::Extreme: quality = Nomad::Audio::Interpolators::InterpolationQuality::Sinc64; break;
                        case Nomad::Audio::ResamplingMode::Perfect: quality = Nomad::Audio::Interpolators::InterpolationQuality::Sinc64; break;
                        default: quality = Nomad::Audio::Interpolators::InterpolationQuality::Cubic; break;
                    }
                    
                    m_audioEngine->setInterpolationQuality(quality);
                    Log::info("AudioEngine interpolation quality set to: " + std::to_string(static_cast<int>(quality)));
                }
                
                // Sync Dithering Mode
                if (m_audioSettingsDialog) {
                    auto ditherMode = m_audioSettingsDialog->getSelectedDitheringMode();
                    m_audioEngine->setDitheringMode(ditherMode);
                    Log::info("AudioEngine dithering mode set to: " + std::to_string(static_cast<int>(ditherMode)));
                }
            }
            
            // Update audio status
            if (m_content) {
                bool trackManagerActive = m_content->getTrackManagerUI() &&
                                        m_content->getTrackManagerUI()->getTrackManager() &&
                                        (m_content->getTrackManagerUI()->getTrackManager()->isPlaying() ||
                                         m_content->getTrackManagerUI()->getTrackManager()->isRecording());
                m_content->setAudioStatus(m_audioInitialized || trackManagerActive);
            }
        });
        m_audioSettingsDialog->setOnCancel([this]() {
            Log::info("Audio settings cancelled");
        });
        m_audioSettingsDialog->setOnStreamRestore([this]() {
            // Restore main audio stream after test sound
            Log::info("Restoring main audio stream...");
            
            // Get fresh device list to avoid stale device IDs
            auto devices = m_audioManager->getDevices();
            if (devices.empty()) {
                Log::error("No audio devices available for stream restore");
                return;
            }
            
            // Find first output device
            uint32_t deviceId = 0;
            for (const auto& dev : devices) {
                if (dev.maxOutputChannels > 0) {
                    deviceId = dev.id;
                    Log::info("Using device for restore: " + dev.name + " (ID: " + std::to_string(dev.id) + ")");
                    break;
                }
            }
            
            if (deviceId == 0) {
                Log::error("No output device found for stream restore");
                return;
            }
            
            // Update config with fresh device ID
            m_mainStreamConfig.deviceId = deviceId;
            if (m_audioEngine) {
                m_audioEngine->setSampleRate(m_mainStreamConfig.sampleRate);
                m_audioEngine->setBufferConfig(m_mainStreamConfig.bufferSize, m_mainStreamConfig.numOutputChannels);
            }
            
            if (m_audioManager->openStream(m_mainStreamConfig, audioCallback, this)) {
                if (m_audioManager->startStream()) {
                    Log::info("Main audio stream restored successfully");
                    m_audioInitialized = true;
                    double actualRate = static_cast<double>(m_audioManager->getStreamSampleRate());
                    if (actualRate <= 0.0) actualRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                    if (m_content && m_content->getTrackManager()) {
                        m_content->getTrackManager()->setOutputSampleRate(actualRate);
                    }
                    if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                        m_content->getTrackManagerUI()->getTrackManager()->setOutputSampleRate(actualRate);
                    }
                } else {
                    Log::error("Failed to start restored audio stream");
                    m_audioInitialized = false;
                }
            } else {
                Log::error("Failed to open restored audio stream");
                m_audioInitialized = false;
            }
        });
        // Add dialog to root component AFTER custom window so it renders on top
        Log::info("Audio settings dialog created");
        
        // Create confirmation dialog for unsaved changes prompts
        m_confirmationDialog = std::make_shared<ConfirmationDialog>();
        m_confirmationDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
        Log::info("Confirmation dialog created");
        
        // Add custom window to root component
        m_rootComponent->setCustomWindow(m_customWindow);
        
        // Add audio settings dialog to root component (after custom window for proper z-ordering)
        m_rootComponent->addChild(m_audioSettingsDialog);
        m_rootComponent->setAudioSettingsDialog(m_audioSettingsDialog);
        
        // Add confirmation dialog (on top of everything except FPS display)
        m_rootComponent->addChild(m_confirmationDialog);
        
        // Create and add FPS display overlay (on top of everything)
        auto fpsDisplay = std::make_shared<FPSDisplay>(m_adaptiveFPS.get());
        m_rootComponent->setFPSDisplay(fpsDisplay);
        m_fpsDisplay = fpsDisplay;
        Log::info("FPS display overlay created");
        
        // Create and add Performance HUD (F12 to toggle)
        auto perfHUD = std::make_shared<PerformanceHUD>();
        perfHUD->setVisible(false); // Hidden by default
        perfHUD->setAudioEngine(m_audioEngine.get());
        m_rootComponent->setPerformanceHUD(perfHUD);
        m_performanceHUD = perfHUD;
        Log::info("Performance HUD created (press F12 to toggle)");
        
        // Debug: Verify dialog was added
        std::stringstream ss2;
        ss2 << "Dialog added to root component, pointer: " << m_audioSettingsDialog.get();
        Log::info(ss2.str());
        
        // Connect window and renderer to bridge
        m_window->setRootComponent(m_rootComponent.get());
        m_window->setRenderer(m_renderer.get());
        
        // Connect custom window to platform window for dragging and window controls
        m_customWindow->setWindowHandle(m_window.get());
        
        Log::info("Custom window created");
        
        // Setup window callbacks
        setupCallbacks();
        
        // Setup cursor visibility callback for TrackManagerUI (split tool hides cursor to show scissors)
        if (m_content && m_content->getTrackManagerUI()) {
            m_content->getTrackManagerUI()->setOnCursorVisibilityChanged([this](bool visible) {
                if (m_window) {
                    m_window->setCursorVisible(visible);
                }
            });
        }

        m_running = true;
        Log::info("NOMAD DAW initialized successfully");
        return true;
    }

    /**
     * @brief Main application loop
     */
    void run() {
        // Prepare renderer
        m_renderer->beginFrame();
        m_renderer->clear(NUIColor(0.06f, 0.06f, 0.07f));

        // Render UI root (instrumented)
        if (m_rootComponent) {
            NOMAD_ZONE("UI_Render");
            m_rootComponent->onRender(*m_renderer);
        }

        // End renderer frame
        m_renderer->endFrame();
        // Start track manager if tracks are loaded
        if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager() &&
            m_content->getTrackManagerUI()->getTrackManager()->getTrackCount() > 0) {
            Log::info("Starting track manager playback");
            // Track manager will be controlled by transport bar callbacks
        }

        // Main event loop
	        double deltaTime = 0.0; // Declare outside so it's visible in all zones
	        double autoSaveTimer = 0.0; // Timer for auto-save (in seconds)
	        const double autoSaveInterval = 60.0; // Auto-save every 60 seconds
	        double underrunCheckTimer = 0.0; // Periodic underrun check
        
        while (m_running && m_window->processEvents()) {
            // Begin profiler frame
            Profiler::getInstance().beginFrame();
            
            // Begin frame timing BEFORE any work
            auto frameStart = m_adaptiveFPS->beginFrame();
            
            {
                NOMAD_ZONE("Input_Poll");
                // Calculate delta time for smooth animations
                static auto lastTime = std::chrono::high_resolution_clock::now();
                auto currentTime = std::chrono::high_resolution_clock::now();
                deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
                lastTime = currentTime;
                
                // Signal audio visualization activity if visualizer is active
                if (m_content && m_content->getAudioVisualizer()) {
                    // Check if audio is actually playing or visualizing
                    bool audioActive = m_audioInitialized && 
                                       (m_content->getTrackManagerUI() && 
                                        m_content->getTrackManagerUI()->getTrackManager() &&
                                        m_content->getTrackManagerUI()->getTrackManager()->isPlaying());
                    m_adaptiveFPS->setAudioVisualizationActive(audioActive);
                }
            }
            
            {
                NOMAD_ZONE("UI_Update");
                // Update all UI components (for animations, VU meters, etc.)
                if (m_rootComponent) {
                    NOMAD_ZONE("Root_OnUpdate");
                    m_rootComponent->onUpdate(deltaTime);
                }

                // Feed master peaks from AudioEngine into the VU meter.
                // This avoids any RT-thread calls into UI and keeps metering in sync
                // with the actual engine output.
                if (m_audioEngine && m_content && m_content->getAudioVisualizer()) {
                    m_content->getAudioVisualizer()->setPeakLevels(
                        m_audioEngine->getPeakL(),
                        m_audioEngine->getPeakR(),
                        m_audioEngine->getRmsL(),
                        m_audioEngine->getRmsR()
                    );
                }

                // Feed recent master output to the mini waveform meter (non-RT).
                if (m_audioEngine && m_content && m_content->getWaveformVisualizer()) {
                    static std::vector<float> waveformScratch;
                    const uint32_t framesToRead = std::min<uint32_t>(
                        m_audioEngine->getWaveformHistoryCapacity(), 256);
                    if (framesToRead > 0) {
                        waveformScratch.resize(static_cast<size_t>(framesToRead) * 2);
                        const uint32_t read = m_audioEngine->copyWaveformHistory(
                            waveformScratch.data(), framesToRead);
                        if (read > 0) {
                            m_content->getWaveformVisualizer()->setInterleavedWaveform(
                                waveformScratch.data(), read);
                        }
                    }
                }
                
                // Sync transport position from AudioEngine during playback
                // AudioEngine is the authoritative source for playback position
                if (m_content && m_content->getTransportBar() && m_audioEngine) {
                    // Only sync while UI transport is actually in Playing state.
                    // This prevents a just-triggered Stop from being overwritten
                    // by a few more audio blocks before the RT thread processes it.
                    auto trackManager = m_content->getTrackManager();
                    const bool userScrubbing = trackManager && trackManager->isUserScrubbing();

                    // Track play state transitions to reset loop detection properly
                    static bool wasPlaying = false;
                    static double lastPosition = 0.0;
                    
                    const bool isPlaying = !userScrubbing &&
                        m_audioEngine->isTransportPlaying() &&
                        m_content->getTransportBar()->getState() == TransportState::Playing;
                    
                    if (isPlaying) {
                        double currentPosition = m_audioEngine->getPositionSeconds();
                        
                        // Reset lastPosition when transitioning from stopped to playing
                        // This prevents false loop detection after stop/play cycles
                        if (!wasPlaying) {
                            lastPosition = currentPosition;
                        }
                        
                        // Sync TrackManager without feeding seeks back into the engine.
                        if (trackManager) {
                            trackManager->syncPositionFromEngine(currentPosition);
                        }
                        
                        // Detect loop (position jumped backward significantly)
                        if (currentPosition < lastPosition - 0.1) {
                            // Loop occurred - force timer reset
                            m_content->getTransportBar()->setPosition(0.0);
                            lastPosition = 0.0; // Also reset lastPosition for the loop
                        } else {
                            m_content->getTransportBar()->setPosition(currentPosition);
                        }
                        
                        lastPosition = currentPosition;
                    }
                    wasPlaying = isPlaying;
                }
                
                // Update sound previews
                if (m_content) {
                    m_content->updateSoundPreview();
                }

                // Rebuild audio graph for engine when track data changes.
                // IMPORTANT: while playing, do not push transport samplePos from this path,
                // otherwise we can create tiny unintended seeks -> audible crackles.
                if (m_audioEngine && m_content && m_content->getTrackManager() &&
                    m_content->getTrackManager()->consumeGraphDirty()) {
                    double graphSampleRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                    if (m_audioManager) {
                        double actual = static_cast<double>(m_audioManager->getStreamSampleRate());
                        if (actual > 0.0) {
                            graphSampleRate = actual;
                        }
                    }
                    m_audioEngine->setMeterSnapshots(m_content->getTrackManager()->getMeterSnapshots());
                    m_audioEngine->setContinuousParams(m_content->getTrackManager()->getContinuousParams());
                    m_audioEngine->setChannelSlotMap(m_content->getTrackManager()->getChannelSlotMapShared());
                    auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), graphSampleRate);
                    m_audioEngine->setGraph(graph);
                    const bool playing = m_content->getTrackManager()->isPlaying();
                    if (!playing) {
                        // When stopped, keep engine position aligned to UI position.
                        AudioQueueCommand cmd;
                        cmd.type = AudioQueueCommandType::SetTransportState;
                        cmd.value1 = 0.0f;
                        double posSeconds = m_content->getTrackManager()->getPosition();
                        cmd.samplePos = static_cast<uint64_t>(posSeconds * graphSampleRate);
                        m_audioEngine->commandQueue().push(cmd);
                    }
                }
                
	                // Auto-save check (only when modified and not playing to avoid audio glitches)
	                autoSaveTimer += deltaTime;
	                if (autoSaveTimer >= autoSaveInterval) {
                    autoSaveTimer = 0.0;
                    
                    if (m_content && m_content->getTrackManager()) {
                        bool isModified = m_content->getTrackManager()->isModified();
                        bool isPlaying = m_content->getTrackManager()->isPlaying();
                        bool isRecording = m_content->getTrackManager()->isRecording();
                        
                        // Only auto-save if modified and not in active audio operation
                        if (isModified && !isPlaying && !isRecording) {
                            Log::info("[AutoSave] Saving project...");
                            if (saveProject()) {
                                Log::info("[AutoSave] Project saved successfully");
                            } else {
                                Log::warning("[AutoSave] Failed to save project");
	                }
	                
	                // Periodically check driver underruns and auto-scale buffer if needed.
	                underrunCheckTimer += deltaTime;
	                if (underrunCheckTimer >= 1.0) {
	                    underrunCheckTimer = 0.0;
	                    if (m_audioManager) {
	                        m_audioManager->checkAndAutoScaleBuffer();
	                    }
	                }
	            }
                    }
                }
            }

            {
                NOMAD_ZONE("Render_Prep");
                
                // Poll Preview Position if needed
                if (m_content && m_content->isPlayingPreview()) {
                    m_content->updatePreviewPlayhead();
                }
                
                render();
            }
            
            // End frame timing BEFORE swapBuffers (to exclude VSync wait)
            double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);
            
            {
                NOMAD_ZONE("GPU_Submit");
                // SwapBuffers (may block on VSync)
                m_window->swapBuffers();
            }
            
            // Sleep if needed (usually 0 since VSync already throttles us)
            if (sleepTime > 0.0) {
                m_adaptiveFPS->sleep(sleepTime);
            }
            
            // Update profiler stats before ending frame
            if (m_content && m_content->getTrackManagerUI()) {
                auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
                if (trackManager) {
                    // Connect command sink for RT updates
                    if (m_audioEngine) {
                        trackManager->setCommandSink([this](const AudioQueueCommand& cmd) {
                            if (m_audioEngine) {
                                m_audioEngine->commandQueue().push(cmd);
                            }
                        });
                    }
                    if (m_content && m_content->getTrackManager()) {
                        m_content->getTrackManager()->setCommandSink([this](const AudioQueueCommand& cmd) {
                            if (m_audioEngine) {
                                m_audioEngine->commandQueue().push(cmd);
                            }
                        });
                    }
                    // Keep track managers aware of current sample rate
                    double actualRate = static_cast<double>(m_mainStreamConfig.sampleRate);
                    if (m_audioManager) {
                        double actual = static_cast<double>(m_audioManager->getStreamSampleRate());
                        if (actual > 0.0) actualRate = actual;
                    }
                    trackManager->setOutputSampleRate(actualRate);
                    if (m_content && m_content->getTrackManager()) {
                        m_content->getTrackManager()->setOutputSampleRate(actualRate);
                    }
                    Profiler::getInstance().setAudioLoad(trackManager->getAudioLoadPercent());
                }
            }
            
            // Count widgets recursively
            uint32_t widgetCount = 0;
            if (m_rootComponent) {
                std::function<void(NUIComponent*)> countWidgets = [&](NUIComponent* comp) {
                    if (comp) {
                        // Only count visible components
                        if (comp->isVisible()) {
                            widgetCount++;
                            for (const auto& child : comp->getChildren()) {
                                countWidgets(child.get());
                            }
                        }
                        else {
                            // If this component is hidden, don't traverse its children
                        }
                    }
                };
                countWidgets(m_rootComponent.get());
            }
            Profiler::getInstance().setWidgetCount(widgetCount);
            
            // End profiler frame
            Profiler::getInstance().endFrame();
        }

        Log::info("Exiting main loop");
    }

    /**
     * @brief Shutdown all subsystems
     */
    void shutdown() {
        Log::info("[SHUTDOWN] Entering shutdown function...");
        
        // Note: m_running is already false when we exit the main loop
        // Use a separate flag to prevent double-shutdown
        static bool shutdownComplete = false;
        if (shutdownComplete) {
            Log::info("[SHUTDOWN] Already completed, returning");
            return;
        }
        shutdownComplete = true;

        // Save project state before tearing down (with error handling)
        try {
            Log::info("[SHUTDOWN] Saving project state...");
            if (saveProject()) {
                Log::info("[SHUTDOWN] Project saved successfully");
            } else {
                Log::warning("[SHUTDOWN] Project save returned false");
            }
        } catch (const std::exception& e) {
            Log::error("[SHUTDOWN] Exception during save: " + std::string(e.what()));
        } catch (...) {
            Log::error("[SHUTDOWN] Unknown exception during save");
        }

        Log::info("Shutting down NOMAD DAW...");

        // Stop and close audio
        if (m_audioInitialized && m_audioManager) {
            m_audioManager->stopStream();
            m_audioManager->closeStream();
            m_audioManager->shutdown();
            Log::info("Audio engine shutdown");
        }

        // Stop track manager
        if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
            m_content->getTrackManagerUI()->getTrackManager()->stop();
            Log::info("Track manager shutdown");
        }

        // Shutdown UI renderer
        if (m_renderer) {
            m_renderer->shutdown();
            Log::info("UI renderer shutdown");
        }
        
        // Save File Browser state
        if (m_content && m_content->getFileBrowser()) {
            m_content->getFileBrowser()->saveState(BROWSER_SETTINGS_FILE);
        }

        // Destroy window
        if (m_window) {
            m_window->destroy();
            m_window.reset();
            Log::info("Window destroyed");
        }

        // Shutdown platform
        Platform::shutdown();
        Log::info("Platform shutdown");

        m_running = false;
        Log::info("NOMAD DAW shutdown complete");
    }

    ProjectSerializer::LoadResult loadProject() {
        if (!m_content || !m_content->getTrackManager()) {
            Log::warning("[loadProject] Content or TrackManager not available");
            return {};
        }
        
        // Check if autosave file exists before attempting to load
        if (!std::filesystem::exists(m_projectPath)) {
            Log::info("[loadProject] No previous project file found at: " + m_projectPath);
            return {};  // Return empty result, not an error
        }
        
        try {
            Log::info("[loadProject] Loading from: " + m_projectPath);
            return ProjectSerializer::load(m_projectPath, m_content->getTrackManager());
        } catch (const std::exception& e) {
            Log::error("[loadProject] Exception: " + std::string(e.what()));
            return {};
        } catch (...) {
            Log::error("[loadProject] Unknown exception");
            return {};
        }
    }

    bool saveProject() {
        if (!m_content || !m_content->getTrackManager()) return false;
        double tempo = 120.0;
        double playhead = m_content->getTrackManager()->getPosition();
        if (m_content->getTransportBar()) {
            tempo = m_content->getTransportBar()->getTempo();
        }
        bool saved = ProjectSerializer::save(m_projectPath, m_content->getTrackManager(), tempo, playhead);
        if (saved && m_content->getTrackManager()) {
            m_content->getTrackManager()->setModified(false);
        }
        return saved;
    }
    
    /**
     * @brief Request application close with optional save prompt
     * 
     * If there are unsaved changes, shows a confirmation dialog.
     * Otherwise, closes immediately.
     */
    void requestClose() {
        Log::info("[requestClose] Close requested");
        
        // Check if there are unsaved changes
        bool hasUnsavedChanges = false;
        if (m_content && m_content->getTrackManager()) {
            hasUnsavedChanges = m_content->getTrackManager()->isModified();
            Log::info("[requestClose] isModified() = " + std::string(hasUnsavedChanges ? "true" : "false"));
        } else {
            Log::info("[requestClose] No content or track manager");
        }
        
        // For now, ALWAYS show the dialog to test it works
        // TODO: Restore hasUnsavedChanges check once we verify dialog works
        if (m_confirmationDialog) {
            Log::info("[requestClose] Showing confirmation dialog");
            // Show confirmation dialog
            m_confirmationDialog->show(
                "Close NOMAD",
                "Are you sure you want to close NOMAD?",
                [this](DialogResponse response) {
                    switch (response) {
                        case DialogResponse::Save:
                            if (saveProject()) {
                                Log::info("Project saved before exit");
                            } else {
                                Log::error("Failed to save project before exit");
                            }
                            m_running = false;
                            break;
                        case DialogResponse::DontSave:
                            Log::info("Exiting without saving");
                            m_running = false;
                            break;
                        case DialogResponse::Cancel:
                            Log::info("Close cancelled by user");
                            // Don't close - user cancelled
                            break;
                        default:
                            break;
                    }
                }
            );
        } else {
            // No dialog available, close immediately
            Log::warning("[requestClose] No confirmation dialog, closing immediately");
            m_running = false;
        }
    }

private:
    /**
     * @brief Setup window event callbacks
     */
    void setupCallbacks() {
        // Register Global Drag Capture Hooks to ensure we track mouse outside window
        auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
        dragManager.setOnDragStart([this](const NomadUI::DragData&) {
            if (m_window) {
                m_window->setMouseCapture(true);
                // Log::info("[DragDrop] Global Drag Started - Mouse Captured");
            }
        });
        
        dragManager.setOnDragEnd([this](const NomadUI::DragData&, const NomadUI::DropResult&) {
            if (m_window) {
                m_window->setMouseCapture(false);
                // Log::info("[DragDrop] Global Drag Ended - Mouse Released");
            }
        });

        // Window resize callback
        m_window->setResizeCallback([this](int width, int height) {
            // Signal activity to adaptive FPS
            m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::WindowResize);
            
            if (m_renderer) {
                m_renderer->resize(width, height);
            }
            std::stringstream ss;
            ss << "Window resized: " << width << "x" << height;
            Log::info(ss.str());
        });

        // Window close callback
        m_window->setCloseCallback([this]() {
            Log::info("Window close requested");
            requestClose();
        });

		        // Key callback
		        m_window->setKeyCallback([this](int key, bool pressed) {
 	            // Signal activity to adaptive FPS
 	            if (pressed) {
 	                m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::KeyPress);
 	            }

                // Maintain an authoritative modifier state so we can recover cleanly if KeyUp is lost.
                auto setModifier = [this](NomadUI::NUIModifiers bit, bool down) {
                    int mods = static_cast<int>(m_keyModifiers);
                    const int flag = static_cast<int>(bit);
                    mods = down ? (mods | flag) : (mods & ~flag);
                    m_keyModifiers = static_cast<NomadUI::NUIModifiers>(mods);
                };
                if (key == static_cast<int>(KeyCode::Control)) {
                    setModifier(NomadUI::NUIModifiers::Ctrl, pressed);
                } else if (key == static_cast<int>(KeyCode::Shift)) {
                    setModifier(NomadUI::NUIModifiers::Shift, pressed);
                } else if (key == static_cast<int>(KeyCode::Alt)) {
                    setModifier(NomadUI::NUIModifiers::Alt, pressed);
                }

                NomadUI::NUIKeyEvent event;
                event.keyCode = convertToNUIKeyCode(key);
                event.pressed = pressed;
                event.released = !pressed;
                event.modifiers = m_keyModifiers;
 	            
 	            // First, try to handle key events in the audio settings dialog if it's visible
 	            if (m_audioSettingsDialog && m_audioSettingsDialog->isVisible()) {
                 if (m_audioSettingsDialog->onKeyEvent(event)) {
                     return; // Dialog handled the event
                 }
             }
            
	            // Handle confirmation dialog key events if visible
 	            if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
                 if (m_confirmationDialog->onKeyEvent(event)) {
                     return; // Dialog handled the event
                 }
 	            }
		            
 		            // If the FileBrowser search box is focused, it owns typing and shortcuts should not trigger.
 		            if (m_content) {
 		                auto fileBrowser = m_content->getFileBrowser();
 		                if (fileBrowser && fileBrowser->isSearchBoxFocused()) {
                            fileBrowser->onKeyEvent(event);
 		                    return;
 		                }
 		            }
 	            
                    // Give the focused component first shot at key events (prevents focus leakage).
                    if (auto* focused = NomadUI::NUIComponent::getFocusedComponent()) {
                        if (focused->onKeyEvent(event)) {
                            return;
                        }
                    }
 
                    // TrackManagerUI shortcuts (v3.1)
                    if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->isVisible()) {
                        if (m_content->getTrackManagerUI()->onKeyEvent(event)) {
                             return;
                        }
                    }

                const bool hasModifiers = (event.modifiers & NomadUI::NUIModifiers::Ctrl) ||
                                          (event.modifiers & NomadUI::NUIModifiers::Shift) ||
                                          (event.modifiers & NomadUI::NUIModifiers::Alt);

 	            // Handle global key shortcuts
 	            if (key == static_cast<int>(KeyCode::Escape) && pressed) {
                // If confirmation dialog is open, let it handle escape
                if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
                    return; // Already handled above
                }
                // If audio settings dialog is open, close it
                if (m_audioSettingsDialog && m_audioSettingsDialog->isVisible()) {
                    m_audioSettingsDialog->hide();
                } else if (m_customWindow && m_customWindow->isFullScreen()) {
                    Log::info("Escape key pressed - exiting fullscreen");
                    m_customWindow->exitFullScreen();
                } else {
                    Log::info("Escape key pressed - requesting close");
                    requestClose();
                }
            } else if (key == static_cast<int>(KeyCode::F11) && pressed) {
                if (m_customWindow) {
                    Log::info("F11 pressed - toggling fullscreen");
                    m_customWindow->toggleFullScreen();
                }
            } else if (key == static_cast<int>(KeyCode::Space) && pressed) {
                // Space bar to play/stop (not pause)
                if (m_content && m_content->getTransportBar()) {
                    auto* transport = m_content->getTransportBar();
                    if (transport->getState() == TransportState::Playing) {
                        transport->stop();
                    } else {
                        transport->play();
                    }
                }
            } else if (key == static_cast<int>(KeyCode::P) && pressed && !hasModifiers) {
                // P key to open audio settings (Preferences)
                if (m_audioSettingsDialog) {
                    m_audioSettingsDialog->show();
                }
            } else if (key == static_cast<int>(KeyCode::M) && pressed && !hasModifiers) {
                // M key to toggle Mixer
                if (m_content) {
                    m_content->toggleView(Audio::ViewType::Mixer);
                }
            } else if (key == static_cast<int>(KeyCode::S) && pressed && !hasModifiers) {
                // S key to toggle Sequencer
                if (m_content) {
                    m_content->toggleView(Audio::ViewType::Sequencer);
                }
            } else if (key == static_cast<int>(KeyCode::R) && pressed && !hasModifiers) {
                // R key to toggle Piano Roll
                if (m_content) {
                    m_content->toggleView(Audio::ViewType::PianoRoll);
                }
            } else if (key == static_cast<int>(KeyCode::B) && pressed && (event.modifiers & NomadUI::NUIModifiers::Alt)) {
                // Alt+B to toggle File Browser
                if (m_content) {
                    m_content->toggleFileBrowser();
                }
            } else if (key == static_cast<int>(KeyCode::F) && pressed && !hasModifiers) {
                // F key to cycle through FPS modes (Auto â†’ 30 â†’ 60 â†’ Auto)
                auto currentMode = m_adaptiveFPS->getMode();
                NomadUI::NUIAdaptiveFPS::Mode newMode;
                std::string modeName;
                
                switch (currentMode) {
                    case NomadUI::NUIAdaptiveFPS::Mode::Auto:
                        newMode = NomadUI::NUIAdaptiveFPS::Mode::Locked30;
                        modeName = "Locked 30 FPS";
                        break;
                    case NomadUI::NUIAdaptiveFPS::Mode::Locked30:
                        newMode = NomadUI::NUIAdaptiveFPS::Mode::Locked60;
                        modeName = "Locked 60 FPS";
                        break;
                    case NomadUI::NUIAdaptiveFPS::Mode::Locked60:
                        newMode = NomadUI::NUIAdaptiveFPS::Mode::Auto;
                        modeName = "Auto (Adaptive)";
                        break;
                }
                
                m_adaptiveFPS->setMode(newMode);
                
            } else if (key == static_cast<int>(KeyCode::F12) && pressed && !hasModifiers) {
                // F12 key to toggle FPS display overlay AND Performance HUD
                if (m_fpsDisplay) {
                    m_fpsDisplay->toggle();
                }
                
                if (m_performanceHUD) {
                    m_performanceHUD->toggle();
                }
            } else if (key == static_cast<int>(KeyCode::L) && pressed && !hasModifiers) {
                // L key to toggle adaptive FPS logging
                auto config = m_adaptiveFPS->getConfig();
                config.enableLogging = !config.enableLogging;
                m_adaptiveFPS->setConfig(config);
                
            } else if (key == static_cast<int>(KeyCode::B) && pressed && !hasModifiers) {
                // B key to toggle render batching
                if (m_renderer) {
                    static bool batchingEnabled = true;
                    batchingEnabled = !batchingEnabled;
                    m_renderer->setBatchingEnabled(batchingEnabled);
                }
            } else if (key == static_cast<int>(KeyCode::D) && pressed && !hasModifiers) {
                // D key to toggle dirty region tracking
                if (m_renderer) {
                    static bool dirtyTrackingEnabled = true;
                    dirtyTrackingEnabled = !dirtyTrackingEnabled;
                    m_renderer->setDirtyRegionTrackingEnabled(dirtyTrackingEnabled);
                }
            } else if (key == static_cast<int>(KeyCode::O) && pressed && !hasModifiers) {
                // O key export profiler data to JSON
                Profiler::getInstance().exportToJSON("nomad_profile.json");
            } else if (key == static_cast<int>(KeyCode::Tab) && pressed && !hasModifiers) {
                // Tab key to toggle playlist visibility
                 if (m_content && m_content->getTrackManagerUI()) {
                     auto trackManagerUI = m_content->getTrackManagerUI();
                     trackManagerUI->togglePlaylist();
                     Log::info("Playlist view toggled via shortcut: " + std::string(trackManagerUI->isPlaylistVisible() ? "Visible" : "Hidden"));
                 }

                // Clip manipulation shortcuts (require Ctrl modifier)
                if (pressed && (event.modifiers & NomadUI::NUIModifiers::Ctrl) && m_content) {
                    auto trackManager = m_content->getTrackManagerUI();
                    if (!trackManager) return;

                    if (key == static_cast<int>(KeyCode::C)) {
                        trackManager->copySelectedClip();
                        Log::info("Clip copied to clipboard");
                    } else if (key == static_cast<int>(KeyCode::X)) {
                        trackManager->cutSelectedClip();
                        Log::info("Clip cut to clipboard");
                    } else if (key == static_cast<int>(KeyCode::V)) {
                        trackManager->pasteClip();
                        Log::info("Clip pasted");
                    } else if (key == static_cast<int>(KeyCode::D)) {
                        trackManager->duplicateSelectedClip();
                        Log::info("Clip duplicated");
                    }
                }

                // Split clip (S key without modifiers)
                if (pressed && !hasModifiers && key == static_cast<int>(KeyCode::S) && m_content) {
                    auto trackManager = m_content->getTrackManagerUI();
                    if (trackManager) {
                        trackManager->splitSelectedClipAtPlayhead();
                        Log::info("Clip split at playhead");
                    }
                }

                // Tool switching shortcuts (number keys 1-4)
                if (pressed && !hasModifiers && m_content) {
                    auto trackManager = m_content->getTrackManagerUI();
                    if (trackManager) {
                        if (key == static_cast<int>(KeyCode::Num1)) {
                            trackManager->setActiveTool(Nomad::Audio::PlaylistTool::Select);
                        } else if (key == static_cast<int>(KeyCode::Num2)) {
                            trackManager->setActiveTool(Nomad::Audio::PlaylistTool::Split);
                        } else if (key == static_cast<int>(KeyCode::Num3)) {
                            trackManager->setActiveTool(Nomad::Audio::PlaylistTool::MultiSelect);
                        } else if (key == static_cast<int>(KeyCode::Num4)) {
                            trackManager->setActiveTool(Nomad::Audio::PlaylistTool::Loop);
                        }
                    }
                }
 	            }
 	        });

        // Mouse move callback - signal activity to adaptive FPS
        m_window->setMouseMoveCallback([this](int x, int y) {
            m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseMove);
            m_lastMouseX = x;
            m_lastMouseY = y;
            
            // Update drag manager position
            auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
            if (dragManager.isDragging()) {
                dragManager.updateDrag(NomadUI::NUIPoint(static_cast<float>(x), static_cast<float>(y)));
            }
        });
        
        // Mouse button callback - signal activity to adaptive FPS
        m_window->setMouseButtonCallback([this](int button, bool pressed) {
            if (pressed) {
                m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseClick);
            }
            
            // Global drag end handling on mouse release
            if (!pressed && button == 0) { // Left mouse release
                auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
                if (dragManager.isDragging()) {
                    dragManager.endDrag(NomadUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY)));
                }
            }
        });
        
        // Mouse wheel callback - signal activity to adaptive FPS
        m_window->setMouseWheelCallback([this](float delta) {
            m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::Scroll);
        });
        
        // Mouse button and move callbacks are handled by NUIPlatformBridge
        // Events will be forwarded to root component and its children

        // DPI change callback
        m_window->setDPIChangeCallback([this](float dpiScale) {
            std::stringstream ss;
            ss << "DPI changed: " << dpiScale;
            Log::info(ss.str());
        });

        // Window focus callback (authoritative reset for modifiers + focus)
        m_window->setFocusCallback([this](bool focused) {
            if (focused) return;

            m_keyModifiers = NomadUI::NUIModifiers::None;
            NomadUI::NUIComponent::clearFocusedComponent();
        });
    }

    /**
     * @brief Render a frame
     */
    void render() {
        if (!m_renderer || !m_rootComponent) {
            return;
        }

        // Clear background with same color as title bar and window for flush appearance
        auto& themeManager = NUIThemeManager::getInstance();
        NUIColor bgColor = themeManager.getColor("background"); // Match title bar color
        
        // Debug: Log the color once
        static bool colorLogged = false;
        if (!colorLogged) {
            std::stringstream ss;
            ss << "Clear color: R=" << bgColor.r << " G=" << bgColor.g << " B=" << bgColor.b;
            Log::info(ss.str());
            colorLogged = true;
        }
        
        m_renderer->clear(bgColor);

        m_renderer->beginFrame();

        // Render root component (which contains custom window)
        m_rootComponent->onRender(*m_renderer);
        
        // Render drag ghost on top of everything (if dragging)
        NUIDragDropManager::getInstance().renderDragGhost(*m_renderer);

        m_renderer->endFrame();
    }

    /**
     * @brief Audio callback function
     * 
     * IMPORTANT: This runs in real-time audio thread - minimize work and NO logging!
     */
    static int audioCallback(float* outputBuffer, const float* inputBuffer,
                             uint32_t nFrames, double streamTime, void* userData) {
        NomadApp* app = static_cast<NomadApp*>(userData);
        if (!app || !outputBuffer) {
            return 1;
        }

        // RT init (FTZ/DAZ) - once per audio thread, no OS calls.
        Nomad::Audio::RT::initAudioThread();
        const uint64_t cbStartCycles = Nomad::Audio::RT::readCycleCounter();

        // Sample rate is fixed for the lifetime of the stream; avoid driver queries in the callback.
        double actualRate = static_cast<double>(app->m_mainStreamConfig.sampleRate);
        if (actualRate <= 0.0) actualRate = 48000.0;

        if (app->m_audioEngine) {
            app->m_audioEngine->setSampleRate(static_cast<uint32_t>(actualRate));
            app->m_audioEngine->processBlock(outputBuffer, inputBuffer, nFrames, streamTime);
        } else {
            std::fill(outputBuffer, outputBuffer + nFrames * 2, 0.0f);
        }
        
        // Preview mixing (RT-safe path to be refactored later)
        if (app->m_content && app->m_content->getPreviewEngine()) {
            auto previewEngine = app->m_content->getPreviewEngine();
            previewEngine->setOutputSampleRate(actualRate);
            previewEngine->process(outputBuffer, nFrames);
        }
        
        // Generate test sound if active (directly in callback, no track needed)
        if (app->m_audioSettingsDialog && app->m_audioSettingsDialog->isPlayingTestSound()) {
            // Use cached stream sample rate (do not query drivers from RT thread).
            const double sampleRate = actualRate;
            const double frequency = 440.0; // A4
            const double amplitude = 0.05; // 5% volume (gentle test tone)
            const double twoPi = 2.0 * 3.14159265358979323846;
            const double phaseIncrement = twoPi * frequency / sampleRate;
            
            double& phase = app->m_audioSettingsDialog->getTestSoundPhase();
            
            // Safety check: reset phase if it's corrupted
            if (phase < 0.0 || phase > twoPi || std::isnan(phase) || std::isinf(phase)) {
                phase = 0.0;
            }
            
            for (uint32_t i = 0; i < nFrames; ++i) {
                float sample = static_cast<float>(amplitude * std::sin(phase));
                
                // Safety clamp
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                
                // Replace output buffer (don't mix) for cleaner test tone
                outputBuffer[i * 2] = sample;     // Left
                outputBuffer[i * 2 + 1] = sample; // Right
                
                phase += phaseIncrement;
                while (phase >= twoPi) {
                    phase -= twoPi;
                }
            }
        }

        // Note: Visualizer update disabled in audio callback to prevent allocations
        // We can update it from the main thread instead at 60fps

        const uint64_t cbEndCycles = Nomad::Audio::RT::readCycleCounter();
        if (app->m_audioEngine && cbEndCycles > cbStartCycles) {
            auto& tel = app->m_audioEngine->telemetry();
            tel.lastBufferFrames.store(nFrames, std::memory_order_relaxed);
            tel.lastSampleRate.store(static_cast<uint32_t>(actualRate), std::memory_order_relaxed);

            const uint64_t hz = tel.cycleHz.load(std::memory_order_relaxed);
            if (hz > 0) {
                const uint64_t deltaCycles = cbEndCycles - cbStartCycles;
                const uint64_t ns = (deltaCycles * 1000000000ull) / hz;
                tel.lastCallbackNs.store(ns, std::memory_order_relaxed);

                uint64_t prevMax = tel.maxCallbackNs.load(std::memory_order_relaxed);
                while (ns > prevMax &&
                       !tel.maxCallbackNs.compare_exchange_weak(prevMax, ns,
                                                               std::memory_order_relaxed,
                                                               std::memory_order_relaxed)) {
                }

                const uint32_t sr = tel.lastSampleRate.load(std::memory_order_relaxed);
                if (sr > 0) {
                    const uint64_t budgetNs = (static_cast<uint64_t>(nFrames) * 1000000000ull) / sr;
                    if (ns > budgetNs) {
                        tel.xruns.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }

        return 0;
    }

private:
    std::unique_ptr<NUIPlatformBridge> m_window;
    std::unique_ptr<NUIRenderer> m_renderer;
    std::unique_ptr<AudioDeviceManager> m_audioManager;
    std::unique_ptr<AudioEngine> m_audioEngine;
    std::shared_ptr<NomadRootComponent> m_rootComponent;
    std::shared_ptr<NUICustomWindow> m_customWindow;
    std::shared_ptr<NomadContent> m_content;
    std::shared_ptr<AudioSettingsDialog> m_audioSettingsDialog;
    std::shared_ptr<ConfirmationDialog> m_confirmationDialog;
    std::shared_ptr<FPSDisplay> m_fpsDisplay;
    std::shared_ptr<PerformanceHUD> m_performanceHUD;
    std::unique_ptr<NomadUI::NUIAdaptiveFPS> m_adaptiveFPS;
    NomadUI::NUIFrameProfiler m_profiler;  // Legacy profiler (can be removed later)
    bool m_running;
    bool m_audioInitialized;
    bool m_pendingClose{false};  // Set when user requested close but awaiting dialog response
    AudioStreamConfig m_mainStreamConfig;  // Store main audio stream configuration
    std::string m_projectPath{getAutosavePath()};
    NomadUI::NUIModifiers m_keyModifiers{NomadUI::NUIModifiers::None};
    
    // Mouse tracking for global drag-and-drop handling
    int m_lastMouseX{0};
    int m_lastMouseY{0};
};

/**
 * @brief Application entry point
 */
int main(int argc, char* argv[]) {
    // Initialize logging with console logger
    Log::init(std::make_shared<ConsoleLogger>(LogLevel::Info));
    Log::setLevel(LogLevel::Info);

    try {
        NomadApp app;
        
        if (!app.initialize()) {
            Log::error("Failed to initialize NOMAD DAW");
            return 1;
        }

        app.run();

        Log::info("NOMAD DAW exited successfully");
        return 0;
    }
    catch (const std::exception& e) {
        std::stringstream ss;
        ss << "Fatal error: " << e.what();
        Log::error(ss.str());
        return 1;
    }
    catch (...) {
        Log::error("Unknown fatal error");
        return 1;
    }
}
