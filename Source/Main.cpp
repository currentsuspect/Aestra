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
#include "../NomadUI/Core/NUIContextMenu.h"
#include "../NomadUI/Widgets/NUIMenuBar.h"
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
#include "../NomadCore/include/NomadUnifiedProfiler.h"
#include "../NomadAudio/include/MiniAudioDecoder.h"
#include "../NomadAudio/include/ClipSource.h"
#include "../NomadAudio/include/WaveformCache.h"
#include "MixerViewModel.h"
#include "TransportBar.h"
#include "SettingsDialog.h"
#include "AudioSettingsPage.h"
#include "GeneralSettingsPage.h"
#include "AppearanceSettingsPage.h"
#include "FileBrowser.h"
#include "FilePreviewPanel.h"
#include "AudioVisualizer.h"
#include "TrackUIComponent.h"
#include "UnifiedHUD.h"
#include "TrackManagerUI.h"
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
#include <objbase.h>

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
    
    void setSettingsDialog(std::shared_ptr<SettingsDialog> dialog) {
        m_settingsDialog = dialog;
    }
    
    void setUnifiedHUD(std::shared_ptr<UnifiedHUD> hud) {
        m_unifiedHUD = hud;
        addChild(m_unifiedHUD);
    }
    
    std::shared_ptr<UnifiedHUD> getUnifiedHUD() const {
        return m_unifiedHUD;
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
    std::shared_ptr<SettingsDialog> m_settingsDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;
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
     * @brief Access content manager (needed for audio callbacks)
     */
    std::shared_ptr<NomadContent> getNomadContent() const { return m_content; }

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
                        
                        config.numInputChannels = outputDevice.maxInputChannels;
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
                                
                                // Register input callback using 'this' as context to allow lazy resolution
                                // This fixes the issue where TrackManager is null during initial audio setup
                                m_audioEngine->setInputCallback([](const float* input, uint32_t n, void* user) {
                                    auto* app = static_cast<NomadApp*>(user); 
                                    // Use public accessor since m_content is private
                                    if (app) {
                                        auto content = app->getNomadContent();
                                        if (content && content->getTrackManager()) {
                                            content->getTrackManager()->processInput(input, n);
                                        }
                                    }
                                }, this);
                                
                                if (m_content && m_content->getTrackManager()) {
                                    m_content->getTrackManager()->setInputChannelCount(config.numInputChannels);
                                    
                                    // Latency Compensation
                                    // Currently using input + output latency.
                                    // In future, we might want to allow user offset calibration.
                                    double inLat = 0.0, outLat = 0.0;
                                    m_audioManager->getLatencyCompensationValues(inLat, outLat);
                                    // Convert ms to samples
                                    double totalLatencyMs = inLat + outLat;
                                    uint32_t latencySamples = static_cast<uint32_t>(totalLatencyMs / 1000.0 * actualRate);
                                    m_content->getTrackManager()->setLatencySamples(latencySamples);
                                    
                                    Nomad::Log::info("Latency Compensation: " + std::to_string(totalLatencyMs) + " ms (" + std::to_string(latencySamples) + " samples)");

                                    // Connect TrackManager command dispatch to AudioEngine queue (Fix for transport/metronome)
                                    if (m_audioEngine) {
                                        m_content->getTrackManager()->setCommandSink([this](const Nomad::Audio::AudioQueueCommand& cmd) {
                                            if (m_audioEngine) {
                                                m_audioEngine->commandQueue().push(cmd);
                                            }
                                        });
                                    }
                                }
                                
                                Nomad::Log::info("Audio Stream Opened: Input Channels = " + std::to_string(config.numInputChannels));
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
                                    // If we are using WASAPI, we assume Input matches Stream Rate (Full Duplex).
                                    // But if we could detect input rate separately, we would set it here.
                                    // For now, set Input = Output (actualRate). 
                                    // IF the user has a mismatch, they need to configure devices to match or rely on OS resampling.
                                    // However, knowing the actual stream rate is better than the requested one (config.sampleRate).
                                    m_content->getTrackManager()->setInputSampleRate(actualRate);
                                }
                                if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                                    m_content->getTrackManagerUI()->getTrackManager()->setOutputSampleRate(actualRate);
                                    m_content->getTrackManagerUI()->getTrackManager()->setInputSampleRate(actualRate);
                                }
                                
                                // Load metronome click sounds (low pitch for downbeat, high pitch for upbeats)
                                if (m_audioEngine) {
                                    // Paths relative to project root (CWD)
                                    // If CWD is project root:
                                    m_audioEngine->loadMetronomeClicks(
                                        "NomadAudio/assets/nomad_metronome.wav",      // Downbeat (low pitch)
                                        "NomadAudio/assets/nomad_metronome_up.wav"    // Upbeat (high pitch)
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
        
        // Wire up TransportBar callbacks to AudioEngine
        if (m_content && m_content->getTransportBar() && m_audioEngine) {
            m_content->getTransportBar()->setOnMetronomeToggle([this](bool active) {
                if (m_audioEngine) {
                    m_audioEngine->setMetronomeEnabled(active);
                    Nomad::Log::info("Metronome toggled: " + std::string(active ? "ON" : "OFF"));
                }
            });
            m_content->getTransportBar()->setOnTempoChange([this](float bpm) {
                if (m_audioEngine) m_audioEngine->setBPM(bpm);
            });
            m_content->getTransportBar()->setOnTimeSignatureChange([this](int beats) {
                if (m_audioEngine) m_audioEngine->setBeatsPerBar(beats);
            });
        }

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
            
            // Create menu bar and add as child (like Arsenal/Timeline toggle)
            m_menuBar = std::make_shared<NomadUI::NUIMenuBar>();
            m_menuBar->addItem("File", [this]() {
                Log::info("File menu clicked");
                showFileMenu();
            });
            m_menuBar->addItem("Edit", [this]() {
                Log::info("Edit menu clicked");
                showEditMenu();
            });
            m_menuBar->addItem("View", [this]() {
                Log::info("View menu clicked");
                showViewMenu();
            });
            m_menuBar->setBounds(NomadUI::NUIRect(10.0f, 4.0f, 120.0f, 24.0f));
            titleBar->addChild(m_menuBar);
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

            // Sync input channel count that was likely missed during early audio init
            // This is critical for recording to work, as the stream opens before TrackManager exists
            if (m_audioInitialized) {
                m_content->getTrackManager()->setInputChannelCount(m_mainStreamConfig.numInputChannels);
                Log::info("Synced input channel count to TrackManager: " + std::to_string(m_mainStreamConfig.numInputChannels));
            }
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
        // Create Unified Settings Dialog
        m_settingsDialog = std::make_shared<SettingsDialog>();
        
        // Add pages
        m_settingsDialog->addPage(std::make_shared<GeneralSettingsPage>());
        
        // Audio page needs dependencies
        auto audioPage = std::make_shared<AudioSettingsPage>(m_audioManager.get(), m_audioEngine.get());
        m_audioSettingsPage = audioPage; // Store for callback access
        
        // Wire stream restore callback for test sound
        audioPage->setOnStreamRestore([this]() {
            Log::info("Restoring main audio stream...");
             // Get fresh device list to avoid stale device IDs
            auto devices = m_audioManager->getDevices();
            if (devices.empty()) return;

             // Find first output device
            uint32_t deviceId = 0;
            for (const auto& dev : devices) {
                if (dev.maxOutputChannels > 0) {
                    deviceId = dev.id;
                    break;
                }
            }
            if (deviceId == 0) return;
            
            m_mainStreamConfig.deviceId = deviceId;
            if (m_audioEngine) {
                 m_audioEngine->setSampleRate(m_mainStreamConfig.sampleRate);
                 m_audioEngine->setBufferConfig(m_mainStreamConfig.bufferSize, m_mainStreamConfig.numOutputChannels);
            }
             if (m_audioManager->openStream(m_mainStreamConfig, audioCallback, this)) {
                 if (m_audioManager->startStream()) {
                     Log::info("Restored stream");
                     m_audioInitialized = true;
                     
                     // === CRITICAL FIX: Propagate sample rate to TrackManager for recording ===
                     // This ensures ASIO/any driver changes are reflected in recording sample rate
                     double actualRate = static_cast<double>(m_audioManager->getStreamSampleRate());
                     if (actualRate > 0.0) {
                         if (m_content && m_content->getTrackManager()) {
                             m_content->getTrackManager()->setInputSampleRate(actualRate);
                             m_content->getTrackManager()->setOutputSampleRate(actualRate);
                             Log::info("TrackManager sample rate updated to: " + std::to_string(static_cast<int>(actualRate)) + " Hz");
                         }
                         if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                             m_content->getTrackManagerUI()->getTrackManager()->setInputSampleRate(actualRate);
                             m_content->getTrackManagerUI()->getTrackManager()->setOutputSampleRate(actualRate);
                         }
                     }
                 }
             }
        });
        
        m_settingsDialog->addPage(audioPage);
        m_settingsDialog->addPage(std::make_shared<AppearanceSettingsPage>());
        
        m_settingsDialog->setBounds(NUIRect(0, 0, 950, 600));
        Log::info("Unified Settings dialog created");
        // Add dialog to root component AFTER custom window so it renders on top
        Log::info("Audio settings dialog created");
        
        // Create confirmation dialog for unsaved changes prompts
        m_confirmationDialog = std::make_shared<ConfirmationDialog>();
        m_confirmationDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
        Log::info("Confirmation dialog created");
        
        // Add custom window to root component
        m_rootComponent->setCustomWindow(m_customWindow);
        
        // Add audio settings dialog to root component (after custom window for proper z-ordering)
        m_rootComponent->addChild(m_settingsDialog);
        m_rootComponent->setSettingsDialog(m_settingsDialog);
        
        // Add confirmation dialog (on top of everything except HUD)
        m_rootComponent->addChild(m_confirmationDialog);
        
        // Create and add Unified Performance HUD (F12 to toggle)
        auto unifiedHUD = std::make_shared<UnifiedHUD>(m_adaptiveFPS.get());
        unifiedHUD->setVisible(false); // Hidden by default
        unifiedHUD->setAudioEngine(m_audioEngine.get());
        m_rootComponent->setUnifiedHUD(unifiedHUD);
        m_unifiedHUD = unifiedHUD;
        Log::info("Unified Performance HUD created (press F12 to toggle)");
        
        // Debug: Verify dialog was added
        std::stringstream ss2;
        ss2 << "Dialog added to root component, pointer: " << m_settingsDialog.get();
        Log::info(ss2.str());
        
        // Connect window and renderer to bridge
        m_window->setRootComponent(m_rootComponent.get());
        m_window->setRenderer(m_renderer.get());
        
        // Connect custom window to platform window for dragging and window controls
        m_customWindow->setWindowHandle(m_window.get());
        
        Log::info("Custom window created");
        
        // Setup window callbacks
        setupCallbacks();
        
        // Initialize custom software cursor system
        if (m_useCustomCursor) {
            initializeCustomCursors();
            
            // Always hide system cursor when custom cursor is enabled
            if (m_window) {
                m_window->setCursorVisible(false);
            }
        }
        
        // Setup cursor visibility callback for TrackManagerUI (split tool hides cursor to show scissors)
        // With custom cursor enabled, this callback is mostly unused but kept for compatibility
        if (m_content && m_content->getTrackManagerUI()) {
            m_content->getTrackManagerUI()->setOnCursorVisibilityChanged([this](bool visible) {
                // With custom cursor, we always keep system cursor hidden
                // Only restore if custom cursor is disabled
                if (m_window && !m_useCustomCursor) {
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
            UnifiedProfiler::getInstance().beginFrame();
            
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
                    UnifiedProfiler::getInstance().setAudioLoad(trackManager->getAudioLoadPercent());
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
            UnifiedProfiler::getInstance().setWidgetCount(widgetCount);
            
            // End profiler frame
            UnifiedProfiler::getInstance().endFrame();
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
 	            
 	            // First, try to handle key events in the settings dialog if it's visible
 	            if (m_settingsDialog && m_settingsDialog->isVisible()) {
                 if (m_settingsDialog->onKeyEvent(event)) {
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
                if (m_settingsDialog && m_settingsDialog->isVisible()) {
                    m_settingsDialog->hide();
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
                if (m_settingsDialog) {
                    if (m_settingsDialog->isVisible()) {
                        m_settingsDialog->hide();
                    } else {
                        m_settingsDialog->show();
                    }
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
                // F12 key to toggle Unified Performance HUD
                if (m_unifiedHUD) {
                    m_unifiedHUD->toggle();
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
                UnifiedProfiler::getInstance().exportToJSON("nomad_profile.json");
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
                        // trackManager->cutSelectedClip();
                        Log::info("Clip cut (Not impl)");
                    } else if (key == static_cast<int>(KeyCode::V)) {
                        trackManager->pasteClipboardAtCursor();
                        Log::info("Clip pasted");
                    } else if (key == static_cast<int>(KeyCode::D)) {
                        // trackManager->duplicateSelectedClip();
                        Log::info("Clip duplicated (Not impl)");
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
        
        // Events will be forwarded to root component and its children

        // DPI change callback
        m_window->setDPIChangeCallback([this](float dpiScale) {
            std::stringstream ss;
            ss << "DPI changed: " << dpiScale;
            Log::info(ss.str());
        });

        // Window focus callback (authoritative reset for modifiers + focus + cursor)
        m_window->setFocusCallback([this](bool focused) {
            m_windowFocused = focused;
            
            if (focused) {
                // Re-hide system cursor when window regains focus
                if (m_useCustomCursor && m_window) {
                    m_window->setCursorVisible(false);
                }
                return;
            }

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
        
        {
            NOMAD_ZONE("Render_Clear");
            m_renderer->clear(bgColor);
        }

        {
            NOMAD_ZONE("Render_BeginFrame");
            m_renderer->beginFrame();
        }

        {
            NOMAD_ZONE("Render_UITree");
            // Render root component (which contains custom window)
            m_rootComponent->onRender(*m_renderer);
        }
        
        {
            NOMAD_ZONE("Render_DragDrop");
            // Render drag ghost on top of everything (if dragging)
            NUIDragDropManager::getInstance().renderDragGhost(*m_renderer);
        }

        // Render custom software cursor on top of everything
        if (m_useCustomCursor && m_windowFocused) {
            NOMAD_ZONE("Render_CustomCursor");
            renderCustomCursor();
        }

        {
            NOMAD_ZONE("Render_EndFrame");
            m_renderer->endFrame();
        }
    }

    // ==============================
    // Custom Software Cursor
    // ==============================
    
    /**
     * @brief Initialize custom cursor icons
     */
    void initializeCustomCursors() {
        // Arrow cursor (default pointer)
        m_cursorArrow = std::make_shared<NomadUI::NUIIcon>(R"(
            <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M5 2L5 18L9 14L12 21L14 20L11 13L17 13L5 2Z" fill="white" stroke="black" stroke-width="1.5"/>
            </svg>
        )");
        
        // Hand cursor (for clickable elements)
        m_cursorHand = std::make_shared<NomadUI::NUIIcon>(R"(
            <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M12 6V3C12 2.45 12.45 2 13 2C13.55 2 14 2.45 14 3V10H15V4C15 3.45 15.45 3 16 3C16.55 3 17 3.45 17 4V10H18V5C18 4.45 18.45 4 19 4C19.55 4 20 4.45 20 5V15C20 18.31 17.31 21 14 21H12C8.69 21 6 18.31 6 15V12C6 11.45 6.45 11 7 11C7.55 11 8 11.45 8 12V14H9V6C9 5.45 9.45 5 10 5C10.55 5 11 5.45 11 6V10H12V6Z" fill="white" stroke="black" stroke-width="1"/>
            </svg>
        )");
        
        // I-Beam cursor (for text input)
        m_cursorIBeam = std::make_shared<NomadUI::NUIIcon>(R"(
            <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20" stroke="white" stroke-width="2" stroke-linecap="round"/>
                <path d="M9 4H11M15 4H13M11 4V20M13 4V20M11 4C11 4 11 4 12 4C13 4 13 4 13 4M11 20H9M15 20H13M11 20C11 20 11 20 12 20C13 20 13 20 13 20" stroke="black" stroke-width="3" stroke-linecap="round" opacity="0.3"/>
            </svg>
        )");
        
        // Horizontal resize cursor
        m_cursorResizeH = std::make_shared<NomadUI::NUIIcon>(R"(
            <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M18 12L22 12M22 12L19 9M22 12L19 15M6 12L2 12M2 12L5 9M2 12L5 15M12 6V18" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
            </svg>
        )");
        
        // Vertical resize cursor
        m_cursorResizeV = std::make_shared<NomadUI::NUIIcon>(R"(
            <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
                <path d="M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
                <path d="M12 6L12 2M12 2L9 5M12 2L15 5M12 18L12 22M12 22L9 19M12 22L15 19M6 12H18" stroke="black" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" opacity="0.3"/>
            </svg>
        )");
        
        Log::info("Custom cursor icons initialized");
    }
    
    /**
     * @brief Render the custom software cursor
     */
    void renderCustomCursor() {
        if (!m_renderer) return;
        
        // Get current cursor style from TrackManagerUI if Split/Paint tool active
        NomadUI::NUICursorStyle style = NomadUI::NUICursorStyle::Arrow;
        
        if (m_content && m_content->getTrackManagerUI()) {
            auto trackUI = m_content->getTrackManagerUI();
            
            // Check if minimap wants resize cursor - if so, let TrackManagerUI render it
            if (trackUI->isMinimapResizeCursorActive()) {
                return;
            }
            
            Nomad::Audio::PlaylistTool tool = trackUI->getCurrentTool();
            
            // Split and Paint tools render their own cursors ONLY when inside the grid
            // Outside the grid, we render the default arrow cursor
            if (tool == Nomad::Audio::PlaylistTool::Split || 
                tool == Nomad::Audio::PlaylistTool::Paint) {
                // Check if mouse is inside the grid area
                NomadUI::NUIRect trackBounds = trackUI->getBounds();
                auto& themeManager = NomadUI::NUIThemeManager::getInstance();
                const auto& layout = themeManager.getLayoutDimensions();
                const float controlAreaWidth = layout.trackControlsWidth;
                const float gridStartX = trackBounds.x + controlAreaWidth + 5.0f;
                const float headerHeight = 38.0f;
                const float rulerHeight = 28.0f;
                const float horizontalScrollbarHeight = 24.0f;
                const float trackAreaTop = trackBounds.y + headerHeight + horizontalScrollbarHeight + rulerHeight;
                
                NomadUI::NUIRect gridBounds(gridStartX, trackAreaTop, 
                                           trackBounds.width - controlAreaWidth - 20.0f,
                                           trackBounds.height - headerHeight - rulerHeight - horizontalScrollbarHeight);
                
                float mx = static_cast<float>(m_lastMouseX);
                float my = static_cast<float>(m_lastMouseY);
                
                if (gridBounds.contains(NomadUI::NUIPoint(mx, my))) {
                    // Inside grid - TrackManagerUI renders the tool cursor, skip default
                    return;
                }
                // Outside grid - fall through to render default arrow cursor
            }
        }
        
        // Select cursor icon based on style
        std::shared_ptr<NomadUI::NUIIcon> cursorIcon;
        float offsetX = 0.0f, offsetY = 0.0f;
        float size = 20.0f;
        
        switch (m_activeCursorStyle) {
            case NomadUI::NUICursorStyle::Hand:
            case NomadUI::NUICursorStyle::Grab:
            case NomadUI::NUICursorStyle::Grabbing:
                cursorIcon = m_cursorHand;
                offsetX = -6.0f;
                offsetY = -2.0f;
                break;
            case NomadUI::NUICursorStyle::IBeam:
                cursorIcon = m_cursorIBeam;
                offsetX = -size / 2.0f;
                offsetY = -size / 2.0f;
                break;
            case NomadUI::NUICursorStyle::ResizeEW:
                cursorIcon = m_cursorResizeH;
                offsetX = -size / 2.0f;
                offsetY = -size / 2.0f;
                break;
            case NomadUI::NUICursorStyle::ResizeNS:
                cursorIcon = m_cursorResizeV;
                offsetX = -size / 2.0f;
                offsetY = -size / 2.0f;
                break;
            default:
                cursorIcon = m_cursorArrow;
                offsetX = 0.0f;
                offsetY = 0.0f;
                break;
        }
        
        if (cursorIcon) {
            float x = static_cast<float>(m_lastMouseX) + offsetX;
            float y = static_cast<float>(m_lastMouseY) + offsetY;
            cursorIcon->setBounds(NomadUI::NUIRect(x, y, size, size));
            cursorIcon->onRender(*m_renderer);
        }
    }

    // ==============================
    // Title Bar Menu Actions
    // ==============================
    
    /**
     * @brief Show File menu dropdown
     */
    void showFileMenu() {
        auto menu = std::make_shared<NomadUI::NUIContextMenu>();
        
        menu->addItem("New Project", [this]() {
            Log::info("File > New Project");
            // Close current project and start fresh
            if (m_content && m_content->getTrackManager()) {
                // TODO: Check for unsaved changes first
                m_content->getTrackManager()->stop();
                // Clear tracks would go here
            }
        });
        
        menu->addItem("Open Project...", [this]() {
            Log::info("File > Open Project");
            // TODO: Show file dialog
        });
        
        menu->addSeparator();
        
        menu->addItem("Save", [this]() {
            Log::info("File > Save");
            saveCurrentProject();
        });
        
        menu->addItem("Save As...", [this]() {
            Log::info("File > Save As");
            // TODO: Show save dialog
        });
        
        menu->addSeparator();
        
        menu->addItem("Export Audio...", [this]() {
            Log::info("File > Export Audio");
            // TODO: Show export dialog
        });
        
        menu->addSeparator();
        
        menu->addItem("Settings...", [this]() {
            Log::info("File > Settings");
            if (m_settingsDialog) {
                m_settingsDialog->show();
            }
        });
        
        menu->addSeparator();
        
        menu->addItem("Exit", [this]() {
            Log::info("File > Exit");
            m_running = false;
        });
        
        // Position menu below File label (left-aligned)
        showDropdownMenu(menu, 10.0f);
    }
    
    /**
     * @brief Show Edit menu dropdown
     */
    void showEditMenu() {
        auto menu = std::make_shared<NomadUI::NUIContextMenu>();
        
        menu->addItem("Undo", [this]() {
            Log::info("Edit > Undo");
            // TODO: Implement undo system
        });
        
        menu->addItem("Redo", [this]() {
            Log::info("Edit > Redo");
            // TODO: Implement redo system
        });
        
        menu->addSeparator();
        
        menu->addItem("Cut", []() {
            Log::info("Edit > Cut");
            // TODO: Implement cut (Ctrl+X)
        });
        
        menu->addItem("Copy", []() {
            Log::info("Edit > Copy");
            // TODO: Implement copy (Ctrl+C)
        });
        
        menu->addItem("Paste", []() {
            Log::info("Edit > Paste");
            // TODO: Implement paste (Ctrl+V)
        });
        
        menu->addItem("Delete", []() {
            Log::info("Edit > Delete");
            // TODO: Implement delete (Del)
        });
        
        menu->addSeparator();
        
        menu->addItem("Select All", []() {
            Log::info("Edit > Select All");
            // TODO: Implement select all (Ctrl+A)
        });
        
        // Position menu below Edit label
        showDropdownMenu(menu, 55.0f);
    }
    
    /**
     * @brief Show View menu dropdown
     */
    void showViewMenu() {
        auto menu = std::make_shared<NomadUI::NUIContextMenu>();
        
        // View toggle items - using addItem since mixer/piano roll toggle state is complex
        menu->addItem("Show Mixer", [this]() {
            Log::info("View > Mixer");
            // Mixer visibility is typically handled by TransportBar button
        });
        
        menu->addItem("Show Piano Roll", [this]() {
            Log::info("View > Piano Roll");
            if (m_content && m_content->getTrackManagerUI()) {
                // Open piano roll for selected pattern
            }
        });
        
        menu->addSeparator();
        
        // Unified Performance HUD checkbox
        bool unifiedHudVisible = m_unifiedHUD && m_unifiedHUD->isVisible();
        menu->addCheckbox("Performance Stats (F12)", unifiedHudVisible, [this](bool) {
            Log::info("View > Performance Stats toggled");
            if (m_unifiedHUD) {
                m_unifiedHUD->toggle();
            }
        });
        
        // Performance HUD checkbox (FPS, Audio, Frame Timing)
        bool hudVisible = m_unifiedHUD && m_unifiedHUD->isVisible();
        menu->addCheckbox("Performance Stats (F12)", hudVisible, [this](bool) {
            Log::info("View > Performance Stats toggled");
            if (m_unifiedHUD) {
                m_unifiedHUD->toggle();
            }
        });
        
        // Position menu below View label
        showDropdownMenu(menu, 105.0f);
    }
    
    /**
     * @brief Helper to show a dropdown menu at the titlebar
     */
    void showDropdownMenu(std::shared_ptr<NomadUI::NUIContextMenu> menu, float xOffset) {
        if (!menu || !m_rootComponent || !m_customWindow) return;
        
        // Position below titlebar
        float titleBarHeight = 32.0f;
        menu->setPosition(xOffset, titleBarHeight);
        
        // Add to root component for proper layering
        m_rootComponent->addChild(menu);
        
        // Store reference to close when clicking outside
        m_activeMenu = menu;
    }
    
    /**
     * @brief Save the current project
     */
    void saveCurrentProject() {
        if (!m_content || !m_content->getTrackManager()) {
            Log::warning("Cannot save: No project data");
            return;
        }
        
        std::string savePath = getAutosavePath();
        double tempo = 120.0;
        if (m_content->getTransportBar()) {
            tempo = static_cast<double>(m_content->getTransportBar()->getTempo());
        }
        double playhead = m_content->getTrackManager()->getPosition();
        
        // Pass shared_ptr directly (not raw pointer)
        if (ProjectSerializer::save(savePath, m_content->getTrackManager(), tempo, playhead)) {
            Log::info("Project saved to: " + savePath);
        } else {
            Log::error("Failed to save project");
        }
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
        if (app->m_audioSettingsPage && app->m_audioSettingsPage->isPlayingTestSound()) {
            // Use cached stream sample rate (do not query drivers from RT thread).
            const double sampleRate = actualRate;
            const double frequency = 440.0; // A4
            const double amplitude = 0.05; // 5% volume (gentle test tone)
            const double twoPi = 2.0 * 3.14159265358979323846;
            const double phaseIncrement = twoPi * frequency / sampleRate;
            
            double& phase = app->m_audioSettingsPage->getTestSoundPhase();
            
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
    std::shared_ptr<SettingsDialog> m_settingsDialog;
    std::shared_ptr<AudioSettingsPage> m_audioSettingsPage; // Accessed by audio callback
    std::shared_ptr<ConfirmationDialog> m_confirmationDialog;
    std::shared_ptr<UnifiedHUD> m_unifiedHUD;
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
    
    // Active dropdown menu (for closing when clicking outside)
    std::shared_ptr<NomadUI::NUIContextMenu> m_activeMenu;
    
    // Menu bar (File/Edit/View)
    std::shared_ptr<NomadUI::NUIMenuBar> m_menuBar;
    
    // Custom software cursor system
    bool m_useCustomCursor{true};
    bool m_windowFocused{true};
    std::shared_ptr<NomadUI::NUIIcon> m_cursorArrow;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorHand;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorIBeam;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeH;
    std::shared_ptr<NomadUI::NUIIcon> m_cursorResizeV;
    NomadUI::NUICursorStyle m_activeCursorStyle{NomadUI::NUICursorStyle::Arrow};
};

/**
 * @brief Application entry point
 */
int main(int argc, char* argv[]) {
    // Initialize COM as STA (Single-Threaded Apartment) for ASIO support
    // This MUST be done before any other library (like RtAudio/WASAPI) initializes it as MTA.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

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
