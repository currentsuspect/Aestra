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
 * @version 1.0.0
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
#include "../NomadAudio/include/PreviewEngine.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadCore/include/NomadProfiler.h"
#include "TransportBar.h"
#include "AudioSettingsDialog.h"
#include "FileBrowser.h"
#include "AudioVisualizer.h"
#include "TrackUIComponent.h"
#include "PerformanceHUD.h"
#include "TrackManagerUI.h"
#include "FPSDisplay.h"
#include "ProjectSerializer.h"
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

        // Create track manager for multi-track functionality (add first so it renders behind transport)
        m_trackManager = std::make_shared<TrackManager>();
        addDemoTracks();

        // Create track manager UI (add before transport so it renders behind)
        m_trackManagerUI = std::make_shared<TrackManagerUI>(m_trackManager);
        float trackAreaWidth = 800.0f; // Will be updated in onResize
        float trackAreaHeight = 500.0f;
        m_trackManagerUI->setBounds(NomadUI::NUIRect(layout.fileBrowserWidth, layout.transportBarHeight, trackAreaWidth, trackAreaHeight));
        addChild(m_trackManagerUI);

        // Create file browser - starts right below transport bar
        m_fileBrowser = std::make_shared<NomadUI::FileBrowser>();
        // Initial positioning using configurable dimensions - will be updated in onResize
        m_fileBrowser->setBounds(NomadUI::NUIRect(0, layout.transportBarHeight, layout.fileBrowserWidth, 620));
        m_fileBrowser->setOnFileOpened([this](const NomadUI::FileItem& file) {
            Log::info("File opened: " + file.path);
            loadSampleIntoSelectedTrack(file.path);
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

        // Create transport bar (add before VU meter so VU renders on top)
        m_transportBar = std::make_shared<TransportBar>();
        addChild(m_transportBar);

        // Create compact audio meter - positioned inside transport bar (right side)
        // Add AFTER transport so it renders on top
        m_audioVisualizer = std::make_shared<NomadUI::AudioVisualizer>();
        float visualizerWidth = 80.0f;
        float visualizerHeight = 40.0f;
        float vuY = layout.componentPadding; // Will be centered in onResize
        m_audioVisualizer->setBounds(NomadUI::NUIRect(1100, vuY, visualizerWidth, visualizerHeight));
        m_audioVisualizer->setMode(NomadUI::AudioVisualizationMode::CompactMeter);
        m_audioVisualizer->setShowStereo(true);
        addChild(m_audioVisualizer);

        // Initialize preview engine
        m_previewEngine = std::make_unique<PreviewEngine>();
        m_previewIsPlaying = false;
        m_previewStartTime = std::chrono::steady_clock::time_point(); // Default construct
        m_previewDuration = 5.0; // 5 seconds preview
        
        // Wire TrackManager audio output to AudioVisualizer for VU meter display
        // This captures the master stereo output and sends it to the visualizer
        m_trackManager->setOnAudioOutput([this](const float* leftChannel, const float* rightChannel, size_t numSamples, double sampleRate) {
            if (m_audioVisualizer) {
                m_audioVisualizer->setAudioData(leftChannel, rightChannel, numSamples, sampleRate);
            }
        });
        
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

    PreviewEngine* getPreviewEngine() const { return m_previewEngine.get(); }

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

        if (!m_previewEngine) {
            Log::error("Preview engine not initialized");
            return;
        }

        auto result = m_previewEngine->play(file.path, -6.0f, m_previewDuration);
        if (result == PreviewResult::Success) {
            m_previewIsPlaying = true;
            m_previewStartTime = std::chrono::steady_clock::now();
            m_currentPreviewFile = file.path;
            Log::info("Sound preview started");
        } else {
            Log::warning("Failed to load preview audio: " + file.path);
        }
    }

    void stopSoundPreview() {
        if (m_previewEngine && m_previewIsPlaying) {
            Log::info("stopSoundPreview called - wasPlaying: true");
            m_previewEngine->stop();
            m_previewIsPlaying = false;
            m_currentPreviewFile.clear();
            Log::info("Sound preview stopped");
        }
    }
    
    void loadSampleIntoSelectedTrack(const std::string& filePath) {
        Log::info("=== Loading sample into selected track: " + filePath + " ===");
        
        // Stop any preview that might be playing
        stopSoundPreview();
        
        // Log all track states after stopping preview
        for (int i = 0; i < m_trackManager->getTrackCount(); ++i) {
            auto track = m_trackManager->getTrack(i);
            if (track) {
                Log::info("Track " + std::to_string(i) + " (" + track->getName() + "): state=" + 
                         std::to_string(static_cast<int>(track->getState())) + 
                         ", isPlaying=" + std::string(track->isPlaying() ? "true" : "false"));
            }
        }
        
        // Get the currently selected track from TrackManagerUI
        if (!m_trackManagerUI) {
            Log::error("TrackManagerUI not initialized");
            return;
        }
        
        // Get the track manager
        auto trackManager = m_trackManagerUI->getTrackManager();
        if (!trackManager) {
            Log::error("TrackManager not found");
            return;
        }
        
        // Check if there are any tracks
        size_t trackCount = trackManager->getTrackCount();
        if (trackCount == 0) {
            Log::warning("No tracks available - creating a new track");
            trackManager->addTrack("Sample Track");
            trackCount = trackManager->getTrackCount();
        }
        
        // Find the first track
        // TODO: Implement track selection in TrackManagerUI to get actually selected track
        std::shared_ptr<Nomad::Audio::Track> targetTrack = trackManager->getTrack(0);
        
        if (!targetTrack) {
            Log::error("Could not find target track");
            return;
        }
        
        Log::info("Loading sample into track: " + targetTrack->getName());
        
        // Load the audio file
        bool loaded = false;
        try {
            loaded = targetTrack->loadAudioFile(filePath);
        } catch (const std::exception& e) {
            Log::error("Exception loading sample: " + std::string(e.what()));
            return;
        }
        
        if (loaded) {
            Log::info("Sample loaded successfully into track: " + targetTrack->getName());
            // Track is now in Loaded state and ready to play
            // Reset position to start
            targetTrack->setPosition(0.0);
            // Set sample to start at current playhead position
            double playheadPosition = m_transportBar ? m_transportBar->getPosition() : 0.0;
            targetTrack->setStartPositionInTimeline(playheadPosition);
            Log::info("Sample loaded at timeline position: " + std::to_string(playheadPosition) + "s");
        } else {
            Log::error("Failed to load sample: " + filePath);
        }
    }

    void updateSoundPreview() {
        if (m_previewEngine && m_previewIsPlaying) {
            if (!m_previewEngine->isPlaying()) {
                stopSoundPreview();
            } else {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - m_previewStartTime);
                if (elapsed.count() >= m_previewDuration) {
                    stopSoundPreview();
                }
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
    
    // Getter for file browser to allow direct key event routing
    std::shared_ptr<NomadUI::FileBrowser> getFileBrowser() const { return m_fileBrowser; }
    
private:
    std::shared_ptr<TransportBar> m_transportBar;
    std::shared_ptr<NomadUI::FileBrowser> m_fileBrowser;
    std::shared_ptr<NomadUI::AudioVisualizer> m_audioVisualizer;
    std::shared_ptr<TrackManager> m_trackManager;
    std::shared_ptr<TrackManagerUI> m_trackManagerUI;
    std::unique_ptr<PreviewEngine> m_previewEngine; // Dedicated preview engine (separate from transport)
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
    
    void setPerformanceHUD(std::shared_ptr<PerformanceHUD> perfHUD) {
        m_performanceHUD = perfHUD;
        addChild(m_performanceHUD);
    }
    
    std::shared_ptr<PerformanceHUD> getPerformanceHUD() const {
        return m_performanceHUD;
    }
    
    void onRender(NUIRenderer& renderer) override {
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
    std::shared_ptr<PerformanceHUD> m_performanceHUD;
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
            m_renderer = std::make_unique<NUIRendererGL>();
            if (!m_renderer->initialize(desc.width, desc.height)) {
                Log::error("Failed to initialize UI renderer");
                return false;
            }
            
            Log::info("UI renderer initialized");
            
            // Enable debug logging for FBO cache (temporary for testing)
            #ifdef _DEBUG
            if (m_renderer->getRenderCache()) {
                m_renderer->getRenderCache()->setDebugEnabled(true);
                Log::info("FBO cache debug logging enabled");
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
                        
                        // Store config for later restoration
                        m_mainStreamConfig = config;

                        // Open audio stream with a simple callback
                        if (m_audioManager->openStream(config, audioCallback, this)) {
                            Log::info("Audio stream opened");
                            
                            // Start the audio stream
                            if (m_audioManager->startStream()) {
                                Log::info("Audio stream started");
                                m_audioInitialized = true;
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
        m_content->setAudioStatus(m_audioInitialized);
        m_customWindow->setContent(m_content.get());
        
        // Attempt to load last project state
        auto loadResult = loadProject();
        if (loadResult.ok) {
            if (m_content->getTransportBar()) {
                m_content->getTransportBar()->setTempo(static_cast<float>(loadResult.tempo));
                m_content->getTransportBar()->setPosition(loadResult.playhead);
            }
            if (m_content->getTrackManager()) {
                m_content->getTrackManager()->setPosition(loadResult.playhead);
            }
        }
        
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
                // Stop preview when transport starts
                if (m_content) {
                    m_content->stopSoundPreview();
                }
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
                // Stop preview when transport stops
                if (m_content) {
                    m_content->stopSoundPreview();
                }
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

            // Keep transport time in sync for scrubbing and playback
            if (m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->getTrackManager()) {
                auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
                trackManager->setOnPositionUpdate([transport](double pos) {
                    transport->setPosition(pos);
                });
            }
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
        
        // Create and add Performance HUD (F12 to toggle)
        auto perfHUD = std::make_shared<PerformanceHUD>();
        perfHUD->setVisible(false); // Hidden by default
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
                
                // Sync transport position with track manager during playback
                if (m_content && m_content->getTransportBar() && m_content->getTrackManagerUI()) {
                    auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
                    if (trackManager && trackManager->isPlaying()) {
                        static double lastPosition = 0.0;
                        double currentPosition = trackManager->getPosition();
                        
                        // Detect loop (position jumped backward significantly)
                        if (currentPosition < lastPosition - 0.1) {
                            // Loop occurred - force timer reset
                            m_content->getTransportBar()->setPosition(0.0);
                        } else {
                            m_content->getTransportBar()->setPosition(currentPosition);
                        }
                        
                        lastPosition = currentPosition;
                    }
                }
                
                // Update sound previews
                if (m_content) {
                    m_content->updateSoundPreview();
                }
            }

            {
                NOMAD_ZONE("Render_Prep");
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
        if (!m_running) {
            return;
        }

        // Save project state before tearing down
        saveProject();

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

    ProjectSerializer::LoadResult loadProject() {
        if (!m_content || !m_content->getTrackManager()) {
            return {};
        }
        return ProjectSerializer::load(m_projectPath, m_content->getTrackManager());
    }

    bool saveProject() {
        if (!m_content || !m_content->getTrackManager()) return false;
        double tempo = 120.0;
        double playhead = m_content->getTrackManager()->getPosition();
        if (m_content->getTransportBar()) {
            tempo = m_content->getTransportBar()->getTempo();
        }
        return ProjectSerializer::save(m_projectPath, m_content->getTrackManager(), tempo, playhead);
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
                event.keyCode = convertToNUIKeyCode(key);
                event.pressed = pressed;
                event.released = !pressed;
                
                if (m_audioSettingsDialog->onKeyEvent(event)) {
                    return; // Dialog handled the event
                }
            }
            
            // Handle global key shortcuts
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
                if (m_audioSettingsDialog) {
                    m_audioSettingsDialog->show();
                }
            } else if (key == static_cast<int>(KeyCode::F) && pressed) {
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
                
            } else if (key == static_cast<int>(KeyCode::F12) && pressed) {
                // F12 key to toggle FPS display overlay AND Performance HUD
                if (m_fpsDisplay) {
                    m_fpsDisplay->toggle();
                }
                
                if (m_performanceHUD) {
                    m_performanceHUD->toggle();
                }
            } else if (key == static_cast<int>(KeyCode::L) && pressed) {
                // L key to toggle adaptive FPS logging
                auto config = m_adaptiveFPS->getConfig();
                config.enableLogging = !config.enableLogging;
                m_adaptiveFPS->setConfig(config);
                
            } else if (key == static_cast<int>(KeyCode::B) && pressed) {
                // B key to toggle render batching
                if (m_renderer) {
                    static bool batchingEnabled = true;
                    batchingEnabled = !batchingEnabled;
                    m_renderer->setBatchingEnabled(batchingEnabled);
                }
            } else if (key == static_cast<int>(KeyCode::D) && pressed) {
                // D key to toggle dirty region tracking
                if (m_renderer) {
                    static bool dirtyTrackingEnabled = true;
                    dirtyTrackingEnabled = !dirtyTrackingEnabled;
                    m_renderer->setDirtyRegionTrackingEnabled(dirtyTrackingEnabled);
                }
            } else if (key == static_cast<int>(KeyCode::C) && pressed) {
                // C key to toggle render caching
                if (m_renderer) {
                    static bool cachingEnabled = true;
                    cachingEnabled = !cachingEnabled;
                    m_renderer->setCachingEnabled(cachingEnabled);
                }
            } else if (key == static_cast<int>(KeyCode::O) && pressed) {
                // O key export profiler data to JSON
                Profiler::getInstance().exportToJSON("nomad_profile.json");
            } else {
                // Forward unhandled key events directly to the FileBrowser
                if (m_content && pressed) {
                    auto fileBrowser = m_content->getFileBrowser();
                    if (fileBrowser) {
                        NomadUI::NUIKeyEvent event;
                        event.keyCode = convertToNUIKeyCode(key);
                        event.pressed = pressed;
                        event.released = !pressed;
                        fileBrowser->onKeyEvent(event);
                    }
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

        // Clear output buffer first
        std::fill(outputBuffer, outputBuffer + nFrames * 2, 0.0f);

        // Process audio through track manager
        if (app->m_content && app->m_content->getTrackManagerUI()) {
            auto trackManager = app->m_content->getTrackManagerUI()->getTrackManager();
            PreviewEngine* previewEngine = app->m_content->getPreviewEngine();
            if (trackManager) {
                // Let track manager mix all playing tracks
                double actualRate = 0.0;
                if (app->m_audioManager) {
                    actualRate = static_cast<double>(app->m_audioManager->getStreamSampleRate());
                }
                if (actualRate <= 0.0) {
                    actualRate = static_cast<double>(app->m_mainStreamConfig.sampleRate);
                }
                trackManager->setOutputSampleRate(actualRate);
                trackManager->processAudio(outputBuffer, nFrames, streamTime);
                if (previewEngine) {
                    previewEngine->setOutputSampleRate(actualRate);
                    previewEngine->process(outputBuffer, nFrames);
                }
            }
        }
        
        // Generate test sound if active (directly in callback, no track needed)
        if (app->m_audioSettingsDialog && app->m_audioSettingsDialog->isPlayingTestSound()) {
            // Use actual sample rate (fallback to config)
            double sampleRate = 0.0;
            if (app->m_audioManager) {
                sampleRate = static_cast<double>(app->m_audioManager->getStreamSampleRate());
            }
            if (sampleRate <= 0.0) {
                sampleRate = static_cast<double>(app->m_mainStreamConfig.sampleRate);
            }
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
    std::shared_ptr<PerformanceHUD> m_performanceHUD;
    std::unique_ptr<NomadUI::NUIAdaptiveFPS> m_adaptiveFPS;
    NomadUI::NUIFrameProfiler m_profiler;  // Legacy profiler (can be removed later)
    bool m_running;
    bool m_audioInitialized;
    AudioStreamConfig m_mainStreamConfig;  // Store main audio stream configuration
    std::string m_projectPath{"autosave.nomadproj"};
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
