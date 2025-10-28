/**
 * @file Main.cpp
 * @brief NOMAD DAW - Main Application Entry Point
 * 
 * This is the main entry point for the NOMAD Digital Audio Workstation.
 * It initializes all core systems:
 * - NomadPlat: Platform abstraction (windowing, input, OpenGL)
 * - NomadUI: UI rendering framework
 * - NomadAudio: Real-time audio engine
 * 
 * @version 1.0.0
 * @license Proprietary
 */

#include "../NomadPlat/include/NomadPlatform.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUICustomWindow.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Core/NUIAdaptiveFPS.h"
#include "../NomadUI/Core/NUIFrameProfiler.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadUI/Graphics/OpenGL/NUIRendererGL.h"
#include "../NomadUI/Platform/NUIPlatformBridge.h"
#include "../NomadAudio/include/NomadAudio.h"
#include "../NomadCore/include/NomadLog.h"
#include "TransportBar.h"
#include "AudioSettingsDialog.h"
#include "FileBrowser.h"
#include "AudioVisualizer.h"
#include "TrackUIComponent.h"
#include "TrackManagerUI.h"
#include "FPSDisplay.h"

#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace Nomad;
using namespace NomadUI;
using namespace Nomad::Audio;

/**
 * @brief Main application class
 * 
 * Manages the lifecycle of all NOMAD subsystems and the main event loop.
 */
/**
 * @brief Main content area for NOMAD DAW
 */
class NomadContent : public NomadUI::NUIComponent {
public:
    NomadContent() {
        // Get layout dimensions from theme for initial setup
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();

        // Create transport bar
        m_transportBar = std::make_shared<TransportBar>();
        addChild(m_transportBar);

        // Create file browser - starts right below transport bar
        m_fileBrowser = std::make_shared<NomadUI::FileBrowser>();
        // Initial positioning using configurable dimensions - will be updated in onResize
        m_fileBrowser->setBounds(NomadUI::NUIRect(0, layout.transportBarHeight, layout.fileBrowserWidth, 620));
        m_fileBrowser->setOnFileOpened([this](const NomadUI::FileItem& file) {
            Log::info("File opened: " + file.path);
            // TODO: Load audio file or project
        });
        m_fileBrowser->setOnSoundPreview([this](const NomadUI::FileItem& file) {
            Log::info("Sound preview requested: " + file.path);
            playSoundPreview(file);
        });
        m_fileBrowser->setOnFileSelected([this](const NomadUI::FileItem& file) {
            // Stop any currently playing preview when selecting a file
            stopSoundPreview();
        });
        addChild(m_fileBrowser);

        // Create compact audio meter - positioned inside transport bar (right side)
        m_audioVisualizer = std::make_shared<NomadUI::AudioVisualizer>();
        float visualizerWidth = 80.0f;
        float visualizerHeight = 40.0f;
        float vuY = layout.componentPadding; // Will be centered in onResize
        m_audioVisualizer->setBounds(NomadUI::NUIRect(1100, vuY, visualizerWidth, visualizerHeight));
        m_audioVisualizer->setMode(NomadUI::AudioVisualizationMode::CompactMeter);
        m_audioVisualizer->setShowStereo(true);
        addChild(m_audioVisualizer);

        // Create track manager for multi-track functionality
        m_trackManager = std::make_shared<TrackManager>();
        addDemoTracks();

        // Create track manager UI
        m_trackManagerUI = std::make_shared<TrackManagerUI>(m_trackManager);
        float trackAreaWidth = 800.0f; // Will be updated in onResize
        float trackAreaHeight = 500.0f;
        m_trackManagerUI->setBounds(NomadUI::NUIRect(layout.fileBrowserWidth, layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
        addChild(m_trackManagerUI);

        // Initialize sound preview system
        m_previewTrack = m_trackManager->addTrack("Preview");
        m_previewTrack->setVolume(0.5f); // Lower volume for preview
        m_previewTrack->setColor(0xFFFF8800); // Orange color for preview track
        m_previewTrack->setSystemTrack(true); // Mark as system track (not affected by transport)
        m_previewIsPlaying = false;
        m_previewStartTime = std::chrono::steady_clock::time_point(); // Default construct
        m_previewDuration = 5.0; // 5 seconds preview
        
        Log::info("Sound preview system initialized");
    }
    
    void setAudioStatus(bool active) {
        m_audioActive = active;
    }
    
    TransportBar* getTransportBar() const {
        return m_transportBar.get();
    }
    
    std::shared_ptr<NomadUI::AudioVisualizer> getAudioVisualizer() const {
        return m_audioVisualizer;
    }
    
    std::shared_ptr<TrackManager> getTrackManager() const {
        return m_trackManager;
    }
    
    std::shared_ptr<TrackManagerUI> getTrackManagerUI() const {
        return m_trackManagerUI;
    }
    
    void addDemoTracks() {
        Log::info("addDemoTracks() called - starting demo track creation");

        // Create 50 empty tracks by default for playlist testing
        const int DEFAULT_TRACK_COUNT = 50;
        const int MAX_TRACK_COUNT = 50; // Maximum tracks for now (will optimize later)
        
        for (int i = 1; i <= DEFAULT_TRACK_COUNT; ++i) {
            auto track = m_trackManager->addTrack("Track " + std::to_string(i));
            // Cycle through colors
            if (i % 3 == 1) track->setColor(0xFFbb86fc); // Purple accent
            else if (i % 3 == 2) track->setColor(0xFF00bcd4); // Cyan
            else track->setColor(0xFF9a9aa3); // Gray
        }

        // Refresh the UI to show the new tracks
        if (m_trackManagerUI) {
            m_trackManagerUI->refreshTracks();
        }
        Log::info("addDemoTracks() completed - created " + std::to_string(DEFAULT_TRACK_COUNT) + " tracks");
    }

    // Generate a simple test WAV file for demo purposes
    bool generateTestWavFile(const std::string& filename, float frequency, double duration) {
        Log::info("generateTestWavFile called for: " + filename);

        // Check if file already exists
        std::ifstream checkFile(filename);
        if (checkFile) {
            checkFile.close();
            Log::info("File already exists: " + filename);
            return true; // File already exists
        }
        checkFile.close();

        Log::info("Generating test WAV file: " + filename + " (" + std::to_string(frequency) + " Hz, " + std::to_string(duration) + "s)");

        // WAV file parameters
        const uint32_t sampleRate = 44100;
        const uint16_t numChannels = 2;
        const uint16_t bitsPerSample = 16;
        const uint32_t totalSamples = static_cast<uint32_t>(sampleRate * duration * numChannels);

        Log::info("WAV parameters: " + std::to_string(sampleRate) + " Hz, " + std::to_string(numChannels) + " channels, " + std::to_string(bitsPerSample) + " bits, " + std::to_string(totalSamples) + " samples");

        // Create WAV header
        struct WavHeader {
            char riff[4] = {'R', 'I', 'F', 'F'};
            uint32_t fileSize;
            char wave[4] = {'W', 'A', 'V', 'E'};
            char fmt[4] = {'f', 'm', 't', ' '};
            uint32_t fmtSize = 16;
            uint16_t audioFormat = 1; // PCM
            uint16_t numChannels;
            uint32_t sampleRate;
            uint32_t byteRate;
            uint16_t blockAlign;
            uint16_t bitsPerSample;
            char data[4] = {'d', 'a', 't', 'a'};
            uint32_t dataSize;
        } header;

        header.numChannels = numChannels;
        header.sampleRate = sampleRate;
        header.byteRate = sampleRate * numChannels * (bitsPerSample / 8);
        header.blockAlign = numChannels * (bitsPerSample / 8);
        header.bitsPerSample = bitsPerSample;
        header.dataSize = totalSamples * (bitsPerSample / 8);
        header.fileSize = 36 + header.dataSize; // Header size + data size

        Log::info("Generated WAV header: fileSize=" + std::to_string(header.fileSize) + ", dataSize=" + std::to_string(header.dataSize));

        // Generate audio data
        std::vector<int16_t> audioData(totalSamples);
        Log::info("Created audio data buffer with " + std::to_string(audioData.size()) + " samples");

        for (uint32_t i = 0; i < totalSamples / numChannels; ++i) {
            double phase = 2.0 * M_PI * frequency * i / sampleRate;
            int16_t sample = static_cast<int16_t>(30000 * sin(phase)); // 16-bit sample with some headroom

            // Stereo (left and right channels)
            audioData[i * numChannels + 0] = sample;
            audioData[i * numChannels + 1] = sample;
        }

        Log::info("Generated audio samples, first sample: " + std::to_string(audioData[0]));

        // Write WAV file
        std::ofstream wavFile(filename, std::ios::binary);
        if (!wavFile) {
            Log::error("Failed to create test WAV file: " + filename);
            return false;
        }

        Log::info("Opened WAV file for writing: " + filename);
        wavFile.write(reinterpret_cast<char*>(&header), sizeof(header));
        Log::info("Wrote WAV header");
        wavFile.write(reinterpret_cast<char*>(audioData.data()), audioData.size() * sizeof(int16_t));
        Log::info("Wrote audio data");
        wavFile.close();
        Log::info("Closed WAV file");

        // Verify file was created
        std::ifstream verifyFile(filename, std::ios::binary);
        if (verifyFile) {
            verifyFile.seekg(0, std::ios::end);
            std::streamsize fileSize = verifyFile.tellg();
            verifyFile.close();
            Log::info("Test WAV file generated successfully: " + filename + " (size: " + std::to_string(fileSize) + " bytes)");
            return true;
        } else {
            Log::error("Failed to verify WAV file creation: " + filename);
            return false;
        }
    }
    
    void playSoundPreview(const NomadUI::FileItem& file) {
        Log::info("Playing sound preview for: " + file.path);

        // Stop any currently playing preview
        stopSoundPreview();

        // Check if preview track exists
        if (!m_previewTrack) {
            Log::error("Preview track not initialized");
            return;
        }

        // Load the audio file into the preview track
        Log::info("Attempting to load audio file...");
        bool loaded = false;
        try {
            loaded = m_previewTrack->loadAudioFile(file.path);
        } catch (const std::exception& e) {
            Log::error("Exception loading audio file: " + std::string(e.what()));
            return;
        }

        if (loaded) {
            Log::info("Preview audio loaded successfully");

            // Set volume for preview (lower than normal)
            m_previewTrack->setVolume(0.5f);

            // Start playing the preview
            m_previewTrack->play();
            m_previewIsPlaying = true;
            m_previewStartTime = std::chrono::steady_clock::now();
            m_currentPreviewFile = file.path;

            Log::info("Sound preview started - Track state: " + 
                      std::to_string(static_cast<int>(m_previewTrack->getState())));
        } else {
            Log::warning("Failed to load preview audio: " + file.path);
        }
    }

    void stopSoundPreview() {
        if (m_previewTrack && m_previewIsPlaying) {
            m_previewTrack->stop();
            m_previewTrack->setPosition(0.0);
            m_previewIsPlaying = false;
            m_currentPreviewFile.clear();
            Log::info("Sound preview stopped");
        }
    }

    void updateSoundPreview() {
        if (m_previewIsPlaying) {
            // Check if 5 seconds have elapsed
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_previewStartTime);

            if (elapsed.count() >= m_previewDuration) {
                stopSoundPreview();
            }
        }
    }
    
    void onRender(NomadUI::NUIRenderer& renderer) override {
        // Note: bounds are set by parent (NUICustomWindow) to be below the title bar
        NomadUI::NUIRect bounds = getBounds();
        float width = bounds.width;
        float height = bounds.height;

        // Get configurable transport bar height from theme
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();
        float transportHeight = layout.transportBarHeight;

        // Get Liminal Dark v2.0 theme colors
        NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");        // #e6e6eb - Soft white
        NomadUI::NUIColor accentColor = themeManager.getColor("accentCyan");       // #00bcd4 - Accent cyan
        NomadUI::NUIColor statusColor = m_audioActive ?
            themeManager.getColor("accentLime") : themeManager.getColor("error");  // #9eff61 for active, #ff4d4d for error

        // Draw main content area (below transport bar)
        NomadUI::NUIRect contentArea(bounds.x, bounds.y + transportHeight,
                                     width, height - transportHeight);
        renderer.fillRect(contentArea, themeManager.getColor("backgroundPrimary"));

        // Draw content area border
        renderer.strokeRect(contentArea, 1, themeManager.getColor("border"));

        // Render children (transport bar, file browser, track manager, visualizer)
        renderChildren(renderer);

    }
    
    
    void onResize(int width, int height) override {
        // Get layout dimensions from theme
        auto& themeManager = NomadUI::NUIThemeManager::getInstance();
        const auto& layout = themeManager.getLayoutDimensions();

        // Update transport bar bounds using configurable dimensions
        if (m_transportBar) {
            NomadUI::NUIRect contentBounds = getBounds();
            m_transportBar->setBounds(NomadUI::NUIRect(contentBounds.x, contentBounds.y,
                                                       static_cast<float>(width), layout.transportBarHeight));
            m_transportBar->onResize(width, static_cast<int>(layout.transportBarHeight));
        }

        // Update file browser bounds using full window dimensions
        if (m_fileBrowser) {
            float fileBrowserWidth = std::min(layout.fileBrowserWidth, width * 0.25f); // 25% of width or max configured width
            float fileBrowserHeight = height - layout.transportBarHeight; // Full height to bottom
            NomadUI::NUIRect contentBounds = getBounds();
            m_fileBrowser->setBounds(NomadUI::NUIRect(contentBounds.x, contentBounds.y + layout.transportBarHeight,
                                                       fileBrowserWidth, fileBrowserHeight));
        }

        // Update audio visualizer position - centered in transport bar
        if (m_audioVisualizer) {
            NomadUI::NUIRect contentBounds = getBounds();
            float visualizerWidth = 80.0f; // Could be made configurable
            float visualizerHeight = 40.0f;
            float vuY = contentBounds.y + (layout.transportBarHeight - visualizerHeight) / 2.0f; // Vertically centered
            m_audioVisualizer->setBounds(NomadUI::NUIRect(contentBounds.x + width - visualizerWidth - layout.panelMargin,
                                                           vuY, visualizerWidth, visualizerHeight));
        }

        // Update track manager UI bounds using full window dimensions (no margins)
        if (m_trackManagerUI) {
            float trackAreaX = layout.fileBrowserWidth; // Use configured file browser width
            float trackAreaWidth = width - trackAreaX; // Full width to window edge
            float trackAreaHeight = height - layout.transportBarHeight; // Full height to bottom
            // Position track manager to start immediately after transport bar, matching file browser positioning
            NomadUI::NUIRect contentBounds = getBounds();
            m_trackManagerUI->setBounds(NomadUI::NUIRect(trackAreaX, contentBounds.y + layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
        }

        NomadUI::NUIComponent::onResize(width, height);
    }
    
private:
    std::shared_ptr<TransportBar> m_transportBar;
    std::shared_ptr<NomadUI::FileBrowser> m_fileBrowser;
    std::shared_ptr<NomadUI::AudioVisualizer> m_audioVisualizer;
    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<TrackManagerUI> m_trackManagerUI;
    std::shared_ptr<Track> m_previewTrack;  // Dedicated track for sound previews
    bool m_audioActive = false;

    // Sound preview state
    bool m_previewIsPlaying = false;
    std::chrono::steady_clock::time_point m_previewStartTime{}; // Default initialized
    double m_previewDuration = 5.0; // 5 seconds
    std::string m_currentPreviewFile;
};

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
    
    void onRender(NUIRenderer& renderer) override {
        // Debug output
        static bool firstRender = true;
        if (firstRender) {
            std::cout << "NomadRootComponent::onRender() called" << std::endl;
            std::cout << "  Bounds: " << getBounds().x << "," << getBounds().y << " " 
                      << getBounds().width << "x" << getBounds().height << std::endl;
            std::cout << "  Children count: " << getChildren().size() << std::endl;
            firstRender = false;
        }
        
        // Don't draw background here - let custom window handle it
        // Just render children (custom window and audio settings dialog)
        renderChildren(renderer);
        
        // Audio settings dialog is handled by its own render method
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
};

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
        std::stringstream ss;
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
        
        ss.str("");
        ss << "Window created: " << desc.width << "x" << desc.height;
        Log::info(ss.str());

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
            m_renderer = std::make_unique<NUIRendererGL>();
            if (!m_renderer->initialize(desc.width, desc.height)) {
                Log::error("Failed to initialize UI renderer");
                return false;
            }
            
            ss.str("");
            ss << "UI renderer initialized: " << m_renderer->getBackendName();
            Log::info(ss.str());
        }
        catch (const std::exception& e) {
            ss.str("");
            ss << "Exception during renderer initialization: " << e.what();
            Log::error(ss.str());
            return false;
        }

        // Initialize audio engine
        std::cout << "Main::initialize: Initializing audio engine" << std::endl;
        std::cout.flush();
        m_audioManager = std::make_unique<AudioDeviceManager>();
        if (!m_audioManager->initialize()) {
            Log::error("Failed to initialize audio engine");
            // Continue without audio for now
            m_audioInitialized = false;
        } else {
            ss.str("");
            ss << "Audio engine initialized: " << Audio::getBackendName();
            Log::info(ss.str());
            
            try {
                // Get default audio device with error handling
                std::cout << "Main::initialize: Getting default output device" << std::endl;
                std::cout.flush();
                
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
                    ss.str("");
                    ss << "Found " << devices.size() << " audio device(s)";
                    Log::info(ss.str());
                    
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
                        ss.str("");
                        ss << "Using audio device: " << outputDevice.name << " (ID: " << outputDevice.id << ")";
                        Log::info(ss.str());
                        
                        // Configure audio stream
                        AudioStreamConfig config;
                        config.deviceId = outputDevice.id;
                        config.sampleRate = 48000;
                        config.bufferSize = 128;  // Ultra-low-latency default for WASAPI Exclusive (2.67ms @ 48kHz)
                        config.numInputChannels = 0;
                        config.numOutputChannels = 2;
                        
                        // Store config for later restoration
                        m_mainStreamConfig = config;

                        std::cout << "Main::initialize: Opening audio stream" << std::endl;
                        std::cout.flush();
                        
                        // Open audio stream with a simple callback
                        if (m_audioManager->openStream(config, audioCallback, this)) {
                            ss.str("");
                            ss << "Audio stream opened: " << config.sampleRate << " Hz, "
                               << config.bufferSize << " samples, " << config.numOutputChannels << " channels";
                            Log::info(ss.str());
                            
                            // Start the audio stream
                            if (m_audioManager->startStream()) {
                                Log::info("========================================");
                                Log::info("AUDIO STREAM STARTED SUCCESSFULLY!!!");
                                Log::info("========================================");
                                m_audioInitialized = true;
                            } else {
                                Log::error("!!!! FAILED TO START AUDIO STREAM !!!!");
                                m_audioInitialized = false;
                            }
                        } else {
                            Log::warning("Failed to open audio stream");
                            m_audioInitialized = false;
                        }
                    }
                }
            } catch (const std::exception& e) {
                ss.str("");
                ss << "Exception while initializing audio: " << e.what();
                Log::error(ss.str());
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
        m_content->setAudioStatus(m_audioInitialized);
        m_customWindow->setContent(m_content.get());
        
        // Configure latency compensation for all tracks (if audio was initialized)
        if (m_audioInitialized && m_audioManager && m_content->getTrackManager()) {
            double inputLatencyMs = 0.0;
            double outputLatencyMs = 0.0;
            m_audioManager->getLatencyCompensationValues(inputLatencyMs, outputLatencyMs);
            
            // Apply latency compensation to all tracks
            auto trackManager = m_content->getTrackManager();
            size_t trackCount = trackManager->getTrackCount();
            for (size_t i = 0; i < trackCount; ++i) {
                auto track = trackManager->getTrack(i);
                if (track && !track->isSystemTrack()) {
                    track->setLatencyCompensation(inputLatencyMs, outputLatencyMs);
                }
            }
        }
        
        // Wire up transport bar callbacks
        if (m_content->getTransportBar()) {
            auto* transport = m_content->getTransportBar();
            
            transport->setOnPlay([this]() {
                Log::info("Transport: Play");
                if (m_content && m_content->getTrackManagerUI()) {
                    m_content->getTrackManagerUI()->getTrackManager()->play();
                }
            });
            
            transport->setOnPause([this]() {
                Log::info("Transport: Pause");
                if (m_content && m_content->getTrackManagerUI()) {
                    m_content->getTrackManagerUI()->getTrackManager()->pause();
                }
            });
            
            transport->setOnStop([this, transport]() {
                Log::info("Transport: Stop");
                if (m_content && m_content->getTrackManagerUI()) {
                    m_content->getTrackManagerUI()->getTrackManager()->stop();
                }
                // Reset position to 0
                transport->setPosition(0.0);
            });
            
            transport->setOnTempoChange([this](float bpm) {
                std::stringstream ss;
                ss << "Transport: Tempo changed to " << bpm << " BPM";
                Log::info(ss.str());
                // TODO: Update track manager tempo
            });
        }
        
        // Create audio settings dialog (pass TrackManager so test sound can be added as a track)
        auto trackManager = m_content->getTrackManagerUI() ? m_content->getTrackManagerUI()->getTrackManager() : nullptr;
        m_audioSettingsDialog = std::make_shared<AudioSettingsDialog>(m_audioManager.get(), trackManager);
        m_audioSettingsDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
        m_audioSettingsDialog->setOnApply([this]() {
            Log::info("Audio settings applied");
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
            
            if (m_audioManager->openStream(m_mainStreamConfig, audioCallback, this)) {
                if (m_audioManager->startStream()) {
                    Log::info("Main audio stream restored successfully");
                    m_audioInitialized = true;
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
        
        // Add custom window to root component
        m_rootComponent->setCustomWindow(m_customWindow);
        
        // Add audio settings dialog to root component (after custom window for proper z-ordering)
        m_rootComponent->addChild(m_audioSettingsDialog);
        m_rootComponent->setAudioSettingsDialog(m_audioSettingsDialog);
        
        // Create and add FPS display overlay (on top of everything)
        auto fpsDisplay = std::make_shared<FPSDisplay>(m_adaptiveFPS.get());
        m_rootComponent->setFPSDisplay(fpsDisplay);
        m_fpsDisplay = fpsDisplay;
        Log::info("FPS display overlay created");
        
        // Debug: Verify dialog was added
        std::stringstream ss2;
        ss2 << "Dialog added to root component, pointer: " << m_audioSettingsDialog.get();
        Log::info(ss2.str());
        
        // Connect window and renderer to bridge
        m_window->setRootComponent(m_rootComponent.get());
        m_window->setRenderer(m_renderer.get());
        
        // Connect custom window to platform window for dragging and window controls
        m_customWindow->setWindowHandle(m_window.get());
        
        // Debug: Check if custom window has children (should have title bar)
        auto children = m_customWindow->getChildren();
        ss.str("");
        ss << "Custom window has " << children.size() << " children";
        Log::info(ss.str());
        
        Log::info("Custom window created");
        
        // Setup window callbacks
        setupCallbacks();

        m_running = true;
        Log::info("NOMAD DAW initialized successfully");
        return true;
    }

    /**
     * @brief Main application loop
     */
    void run() {
        if (!m_running) {
            Log::error("Cannot run - application not initialized");
            return;
        }

        m_window->show();
        Log::info("Entering main loop...");

        // Audio stream is already started during initialization if audio is enabled
        // No need to start it again here

        // Start track manager if tracks are loaded
        if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager() &&
            m_content->getTrackManagerUI()->getTrackManager()->getTrackCount() > 0) {
            Log::info("Starting track manager playback");
            // Track manager will be controlled by transport bar callbacks
        }

        // Main event loop
        while (m_running && m_window->processEvents()) {
            // 🔥 PROFILER: Begin frame timing
            m_profiler.beginFrame();
            
            // Begin frame timing BEFORE any work
            auto frameStart = m_adaptiveFPS->beginFrame();
            
            // Calculate delta time for smooth animations
            static auto lastTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            double deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
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
            
            // Update all UI components (for animations, VU meters, etc.)
            if (m_rootComponent) {
                m_rootComponent->onUpdate(deltaTime);
            }
            
            // Update sound previews
            if (m_content) {
                m_content->updateSoundPreview();
            }

            render();
            
            // 🔥 PROFILER: Mark end of render work
            m_profiler.markRenderEnd();
            
            // End frame timing BEFORE swapBuffers (to exclude VSync wait)
            double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);
            
            // SwapBuffers (may block on VSync)
            m_window->swapBuffers();
            
            // 🔥 PROFILER: Mark end of swap
            m_profiler.markSwapEnd();
            
            // Sleep if needed (usually 0 since VSync already throttles us)
            if (sleepTime > 0.0) {
                m_adaptiveFPS->sleep(sleepTime);
            }
            
            // 🔥 PROFILER: End frame
            m_profiler.endFrame();
            
            // 🔥 PROFILER: Print stats every 100 frames
            static int profilerFrameCount = 0;
            if (++profilerFrameCount >= 100) {
                m_profiler.printStats();
                profilerFrameCount = 0;
            }
        }

        Log::info("Exiting main loop");
    }

    /**
     * @brief Shutdown all subsystems
     */
    void shutdown() {
        if (!m_running) {
            return;
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

private:
    /**
     * @brief Setup window event callbacks
     */
    void setupCallbacks() {
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
            m_running = false;
        });

        // Key callback
        m_window->setKeyCallback([this](int key, bool pressed) {
            // Signal activity to adaptive FPS
            if (pressed) {
                m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::KeyPress);
            }
            
            // Debug: Log key presses
            if (pressed) {
                std::stringstream ss;
                ss << "Key pressed: " << key;
                Log::info(ss.str());
                std::cout << "Key pressed: " << key << " (P should be 80)" << std::endl;
            }
            
            // First, try to handle key events in the audio settings dialog if it's visible
            if (m_audioSettingsDialog && m_audioSettingsDialog->isVisible()) {
                NomadUI::NUIKeyEvent event;
                event.keyCode = static_cast<NomadUI::NUIKeyCode>(key);
                event.pressed = pressed;
                event.released = !pressed;
                
                if (m_audioSettingsDialog->onKeyEvent(event)) {
                    return; // Dialog handled the event
                }
            }
            
            // Handle global key shortcuts
            // Debug: Log all key presses
            if (pressed) {
                std::cout << "Key pressed: " << key << " (P should be 80)" << std::endl;
            }
            
            if (key == static_cast<int>(KeyCode::Escape) && pressed) {
                // If audio settings dialog is open, close it
                if (m_audioSettingsDialog && m_audioSettingsDialog->isVisible()) {
                    m_audioSettingsDialog->hide();
                } else if (m_customWindow && m_customWindow->isFullScreen()) {
                    Log::info("Escape key pressed - exiting fullscreen");
                    m_customWindow->exitFullScreen();
                } else {
                    Log::info("Escape key pressed - exiting");
                    m_running = false;
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
            } else if (key == static_cast<int>(KeyCode::P) && pressed) {
                // P key to open audio settings (Preferences)
                Log::info("===== P KEY HANDLER START =====");
                if (m_audioSettingsDialog) {
                    Log::info("Dialog pointer is valid, calling show()");
                    m_audioSettingsDialog->show();
                    Log::info("show() method called successfully");
                } else {
                    Log::info("ERROR: m_audioSettingsDialog is NULL!");
                }
                Log::info("===== P KEY HANDLER END =====");
            } else if (key == static_cast<int>(KeyCode::F) && pressed) {
                // F key to cycle through FPS modes (Auto → 30 → 60 → Auto)
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
                
                std::stringstream ss;
                ss << "FPS Mode changed to: " << modeName;
                Log::info(ss.str());
                std::cout << "FPS Mode: " << modeName << std::endl;
            } else if (key == static_cast<int>(KeyCode::G) && pressed) {
                // G key to toggle FPS display overlay
                if (m_fpsDisplay) {
                    m_fpsDisplay->toggle();
                    std::stringstream ss;
                    ss << "FPS Display: " << (m_fpsDisplay->isVisible() ? "SHOWN" : "HIDDEN");
                    Log::info(ss.str());
                    std::cout << ss.str() << std::endl;
                }
            } else if (key == static_cast<int>(KeyCode::L) && pressed) {
                // L key to toggle adaptive FPS logging
                auto config = m_adaptiveFPS->getConfig();
                config.enableLogging = !config.enableLogging;
                m_adaptiveFPS->setConfig(config);
                
                std::stringstream ss;
                ss << "Adaptive FPS logging: " << (config.enableLogging ? "ENABLED" : "DISABLED");
                Log::info(ss.str());
                std::cout << ss.str() << std::endl;
            } else if (key == static_cast<int>(KeyCode::B) && pressed) {
                // B key to toggle render batching
                if (m_renderer) {
                    static bool batchingEnabled = true;
                    batchingEnabled = !batchingEnabled;
                    m_renderer->setBatchingEnabled(batchingEnabled);
                    
                    std::stringstream ss;
                    ss << "Render Batching: " << (batchingEnabled ? "ENABLED" : "DISABLED");
                    Log::info(ss.str());
                    std::cout << ss.str() << std::endl;
                }
            } else if (key == static_cast<int>(KeyCode::D) && pressed) {
                // D key to toggle dirty region tracking
                if (m_renderer) {
                    static bool dirtyTrackingEnabled = true;
                    dirtyTrackingEnabled = !dirtyTrackingEnabled;
                    m_renderer->setDirtyRegionTrackingEnabled(dirtyTrackingEnabled);
                    
                    std::stringstream ss;
                    ss << "Dirty Region Tracking: " << (dirtyTrackingEnabled ? "ENABLED" : "DISABLED");
                    Log::info(ss.str());
                    std::cout << ss.str() << std::endl;
                }
            } else if (key == static_cast<int>(KeyCode::C) && pressed) {
                // C key to toggle render caching
                if (m_renderer) {
                    static bool cachingEnabled = true;
                    cachingEnabled = !cachingEnabled;
                    m_renderer->setCachingEnabled(cachingEnabled);
                    
                    std::stringstream ss;
                    ss << "Render Caching: " << (cachingEnabled ? "ENABLED" : "DISABLED");
                    Log::info(ss.str());
                    std::cout << ss.str() << std::endl;
                }
            } else if (key == static_cast<int>(KeyCode::O) && pressed) {
                // O key to print optimization stats
                if (m_renderer) {
                    size_t batchedQuads = 0, dirtyRegions = 0, cachedWidgets = 0, cacheMemoryBytes = 0;
                    m_renderer->getOptimizationStats(batchedQuads, dirtyRegions, cachedWidgets, cacheMemoryBytes);
                    
                    std::cout << "\n╔══════════════════════════════════════════════════════╗\n";
                    std::cout << "║        RENDERING OPTIMIZATION STATS                  ║\n";
                    std::cout << "╠══════════════════════════════════════════════════════╣\n";
                    std::cout << "║  Batched Quads:     " << batchedQuads << " quads\n";
                    std::cout << "║  Dirty Regions:     " << dirtyRegions << " regions\n";
                    std::cout << "║  Cached Widgets:    " << cachedWidgets << " widgets\n";
                    std::cout << "║  Cache Memory:      " << (cacheMemoryBytes / 1024.0f / 1024.0f) << " MB\n";
                    std::cout << "╚══════════════════════════════════════════════════════╝\n" << std::endl;
                    
                    Log::info("Optimization stats printed (press O to view)");
                }
            }
        });
        
        // Mouse move callback - signal activity to adaptive FPS
        m_window->setMouseMoveCallback([this](int x, int y) {
            m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseMove);
        });
        
        // Mouse button callback - signal activity to adaptive FPS
        m_window->setMouseButtonCallback([this](int button, bool pressed) {
            if (pressed) {
                m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseClick);
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

        // Clear output buffer first
        std::fill(outputBuffer, outputBuffer + nFrames * 2, 0.0f);

        // Process audio through track manager
        if (app->m_content && app->m_content->getTrackManagerUI()) {
            auto trackManager = app->m_content->getTrackManagerUI()->getTrackManager();
            if (trackManager) {
                // Let track manager mix all playing tracks
                trackManager->processAudio(outputBuffer, nFrames, streamTime);
            }
        }
        
        // Generate test sound if active (directly in callback, no track needed)
        if (app->m_audioSettingsDialog && app->m_audioSettingsDialog->isPlayingTestSound()) {
            // Use actual sample rate from config instead of hardcoded value
            const double sampleRate = static_cast<double>(app->m_mainStreamConfig.sampleRate);
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

        // Send audio data to visualizer (thread-safe, uses atomic/mutex internally)
        if (app->m_content && app->m_content->getAudioVisualizer()) {
            auto visualizer = app->m_content->getAudioVisualizer();
            
            // Separate interleaved stereo buffer into left/right channels
            std::vector<float> leftChannel(nFrames);
            std::vector<float> rightChannel(nFrames);
            
            for (uint32_t i = 0; i < nFrames; ++i) {
                leftChannel[i] = outputBuffer[i * 2];       // Left
                rightChannel[i] = outputBuffer[i * 2 + 1];  // Right
            }
            
            // Update visualizer with processed audio
            visualizer->setAudioData(leftChannel.data(), rightChannel.data(), nFrames, 48000.0);
        }

        return 0;
    }

private:
    std::unique_ptr<NUIPlatformBridge> m_window;
    std::unique_ptr<NUIRenderer> m_renderer;
    std::unique_ptr<AudioDeviceManager> m_audioManager;
    std::shared_ptr<NomadRootComponent> m_rootComponent;
    std::shared_ptr<NUICustomWindow> m_customWindow;
    std::shared_ptr<NomadContent> m_content;
    std::shared_ptr<AudioSettingsDialog> m_audioSettingsDialog;
    std::shared_ptr<FPSDisplay> m_fpsDisplay;
    std::unique_ptr<NomadUI::NUIAdaptiveFPS> m_adaptiveFPS;
    NomadUI::NUIFrameProfiler m_profiler;  // 🔥 Frame profiler for surgical timing analysis
    bool m_running;
    bool m_audioInitialized;
    AudioStreamConfig m_mainStreamConfig;  // Store main audio stream configuration
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
