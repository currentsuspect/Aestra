// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "NomadApp.h"
#include "AppLifecycle.h"
#include "ServiceLocator.h"
#include "AudioThreadConstraints.h"
#include "../NomadUI/Graphics/OpenGL/NUIRendererGL.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "../NomadAudio/include/AudioGraphBuilder.h"
#include "../NomadAudio/include/AudioCommandQueue.h"
#include "../NomadAudio/include/AudioRT.h"
#include "../NomadAudio/include/PreviewEngine.h"
#include "../NomadAudio/include/PluginManager.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/PluginManager.h"
#include "../NomadCore/include/NomadUnifiedProfiler.h"
#include "../NomadAudio/include/WaveformCache.h"
#include "../NomadAudio/include/ClipSource.h"
#include "AppearanceSettingsPage.h"
#include "FileBrowser.h"
#include "AudioVisualizer.h"
#include "TrackUIComponent.h"
#include "TrackManagerUI.h"
#include "ViewTypes.h"
#include "SettingsDialog.h"
#include "ConfirmationDialog.h"
#include "UnifiedHUD.h"
#include "NomadContent.h"
#include "NomadRootComponent.h"
#include "../NomadUI/Core/NUIComponent.h" 
#include "../NomadUI/Core/NUICustomWindow.h"
#include "../NomadUI/Widgets/NUIMenuBar.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>
#include <algorithm>
// #include <objbase.h> // Removed to prevent conflict

using namespace Nomad;
using namespace NomadUI;
using namespace Nomad::Audio;

const std::string BROWSER_SETTINGS_FILE = "browser_settings.json";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

/**
 * @brief Convert Nomad::KeyCode to NomadUI::NUIKeyCode
 */
NomadUI::NUIKeyCode convertToNUIKeyCode(int key) {
    using KC = Nomad::KeyCode;
    using NUIKC = NomadUI::NUIKeyCode;
    
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

} // namespace

// =============================================================================
// Helper Component: ScopeIndicatorLabel
// =============================================================================
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
        std::string fullText = getText();
        std::string displayLabel = fullText;
        
        // If text starts with the triangle bytes (E2 96 B6), strip them
        if (fullText.size() >= 3 && 
            (unsigned char)fullText[0] == 0xE2 && 
            (unsigned char)fullText[1] == 0x96 && 
            (unsigned char)fullText[2] == 0xB6) {
            
            size_t substrIdx = 3; 
            if (fullText.size() > 3 && fullText[3] == ' ') substrIdx++;
            
            displayLabel = fullText.substr(substrIdx);
        }
        
        float textX = bounds.x + iconSize + padding;
        float textY = bounds.y + (bounds.height - renderer.measureText(displayLabel, fontSize).height) * 0.5f;
        
        renderer.drawText(displayLabel, NomadUI::NUIPoint(std::round(textX), std::round(textY)), fontSize, getTextColor());
    }

private:
    std::shared_ptr<NomadUI::NUIIcon> m_icon;
};

// =============================================================================
// NomadApp Implementation
// =============================================================================

NomadApp::NomadApp() 
    : m_running(false)
    , m_audioInitialized(false)
{
    // Initialize m_projectPath with autosave path
    m_projectPath = getAutosavePath();

    // Initialize unified logging
    auto multiLogger = std::make_shared<MultiLogger>(LogLevel::Info);
    
    // Add console logger
    multiLogger->addLogger(std::make_shared<ConsoleLogger>(LogLevel::Info));
    
    // Add file logger
    // DEBUG: Use current path to allow AI inspection
    std::string logPath = (std::filesystem::current_path() / "nomad_debug.log").string();
    auto fileLogger = std::make_shared<FileLogger>(logPath, LogLevel::Info);
    multiLogger->addLogger(fileLogger);
    
    // Wrap in AsyncLogger for thread safety
    m_asyncLogger = std::make_shared<AsyncLogger>(multiLogger);
    
    Log::init(m_asyncLogger);
    Log::info("Logging initialized to console and " + logPath);

    // Initial log message to confirm async works
    Nomad::Log::info("Async Logging System initialized.");

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

NomadApp::~NomadApp() {
    shutdown();
}

std::string NomadApp::getAppDataPath() {
    IPlatformUtils* utils = Platform::getUtils();
    if (!utils) {
        return std::filesystem::current_path().string();
    }
    
    std::string appDataDir = utils->getAppDataPath("Nomad");
    
    std::error_code ec;
    if (!std::filesystem::create_directories(appDataDir, ec) && ec) {
        // Log::warning("Failed to create app data directory: " + appDataDir + " (" + ec.message() + ")");
        // Can't log here safely if logger depends on path
    }
    
    return appDataDir;
}

std::string NomadApp::getAutosavePath() {
    return (std::filesystem::path(getAppDataPath()) / "autosave.nmd").string();
}

std::string NomadApp::getLegacyAutosavePath() {
    return (std::filesystem::path(getAppDataPath()) / "autosave.nomadproj").string();
}

bool NomadApp::initialize(const std::string& projectPath) {
    // Transition to Initializing state
    if (!Nomad::AppLifecycle::instance().transitionTo(Nomad::AppState::Initializing)) {
        Log::error("Failed to transition to Initializing state");
        return false;
    }
    
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
    desc.startMaximized = true;  // Start maximized by default

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
        
        // CRITICAL: Get the ACTUAL client size after window creation and SWP_FRAMECHANGED
        // With WS_OVERLAPPEDWINDOW + our WM_NCCALCSIZE returning 0, client should equal window,
        // but we need to query the real values to be safe.
        int actualWidth = 0, actualHeight = 0;
        m_window->getSize(actualWidth, actualHeight);
        Log::info("Renderer init with actual client size: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight));
        
        if (!glRenderer->initialize(actualWidth, actualHeight)) {
            delete glRenderer; // Clean up on failure
            Log::error("Failed to initialize UI renderer");
            return false;
        }
        
        // Enable render caching by default for performance
        glRenderer->setCachingEnabled(true);
        
        // Transfer ownership to unique_ptr
        m_renderer.reset(glRenderer);
        
        Log::info("UI renderer initialized");
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
                
                // Find first output device
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
                    config.bufferSize = 256;
                    
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
                            
                            m_audioManager->setAutoBufferScaling(true, 5);
                            
                            double actualRate = static_cast<double>(m_audioManager->getStreamSampleRate());
                            if (actualRate <= 0.0) {
                                actualRate = static_cast<double>(config.sampleRate);
                            }
                            if (m_audioEngine) {
                                m_audioEngine->setSampleRate(static_cast<uint32_t>(actualRate));
                                m_audioEngine->setBufferConfig(config.bufferSize, config.numOutputChannels);

                                // Register input callback
                                m_audioEngine->setInputCallback([](const float* input, uint32_t n, void* user) {
                                    auto* app = static_cast<NomadApp*>(user); 
                                    if (app) {
                                        auto content = app->getNomadContent();
                                        if (content && content->getTrackManager()) {
                                            content->getTrackManager()->processInput(input, n);
                                        }
                                    }
                                }, this);
                            }
                            
                            Nomad::Log::info("Audio Stream Opened: Input Channels = " + std::to_string(config.numInputChannels));
                            
                            m_mainStreamConfig.sampleRate = static_cast<uint32_t>(actualRate);
                            if (m_audioEngine) {
                                const uint64_t hz = estimateCycleHz();
                                if (hz > 0) {
                                    m_audioEngine->telemetry().cycleHz.store(hz, std::memory_order_relaxed);
                                }
                            }
                            
                            // Load metronome click sounds
                            if (m_audioEngine) {
                                m_audioEngine->loadMetronomeClicks(
                                    "NomadAudio/assets/nomad_metronome.wav",
                                    "NomadAudio/assets/nomad_metronome_up.wav"
                                );
                                m_audioEngine->setBPM(120.0f);
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
            Log::error("Exception while initializing audio: " + std::string(e.what()));
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

    Log::info("Loading UI configuration...");
    Log::info("UI configuration loaded");
    
    // Create root component
    m_rootComponent = std::make_shared<NomadRootComponent>();
    m_rootComponent->setBounds(NUIRect(0, 0, desc.width, desc.height));
    m_window->setRootComponent(m_rootComponent.get()); // WIRED: Events flow to this root
    
    // Create custom window with title bar
    m_customWindow = std::make_shared<NUICustomWindow>();
    m_rootComponent->addChild(m_customWindow); // WIRED: Window is in the tree
    m_customWindow->setTitle("NOMAD DAW v1.0");
    m_customWindow->setBounds(NUIRect(0, 0, desc.width, desc.height));
    
    // Wire up precise Hit Test callback logic
    // This allows the Title Bar to handle drag vs click dynamically
    m_window->setHitTestCallback([this](int x, int y) {
        if (m_customWindow && m_customWindow->getTitleBar()) {
            // Convert physical pixels (OS) to logical pixels (NUI)
            float dpi = m_window->getDPIScale();
            if (dpi <= 0.0f) dpi = 1.0f;
            
            float lx = static_cast<float>(x) / dpi;
            float ly = static_cast<float>(y) / dpi;
            NomadUI::NUIPoint logicPt(lx, ly);

            auto titleBar = m_customWindow->getTitleBar();
            if (titleBar->isVisible() && titleBar->getBounds().contains(logicPt)) {
                Nomad::HitTestResult res = titleBar->hitTest(logicPt);
                return res;
            }
        }
        return Nomad::HitTestResult::Default;
    });
    
    // Create content area
    m_content = std::make_shared<NomadContent>();
    m_content->setPlatformBridge(m_window.get());
    m_content->setAudioStatus(m_audioInitialized);
    if (m_audioEngine) {
        m_content->setAudioEngine(m_audioEngine.get());
    }
    m_customWindow->setContent(m_content.get());
    
    // Add view focus toggle to title bar
    if (auto titleBar = m_customWindow->getTitleBar()) {
        if (m_content->getViewToggle()) {
            titleBar->addChild(m_content->getViewToggle());
        }
        
        // Create menu bar
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
    
    // Pass platform window to TrackManagerUI
    if (m_content->getTrackManagerUI()) {
        m_content->getTrackManagerUI()->setPlatformWindow(m_window.get());
    }
    
    // Restore File Browser state
    if (m_content->getFileBrowser()) {
        m_content->getFileBrowser()->loadState(BROWSER_SETTINGS_FILE);
    }

    // Initialize deferred audio settings
    if (m_audioEngine && m_content && m_content->getTrackManager()) {
        if (m_audioInitialized) {
            m_content->getTrackManager()->setInputChannelCount(m_mainStreamConfig.numInputChannels);
            m_content->getTrackManager()->setOutputSampleRate(m_mainStreamConfig.sampleRate);
            m_content->getTrackManager()->setInputSampleRate(m_mainStreamConfig.sampleRate);
            
            // Connect Metering (v3.1)
            auto meterBuffer = std::make_shared<Audio::MeterSnapshotBuffer>();
            m_audioEngine->setMeterSnapshots(meterBuffer);
            m_content->getTrackManager()->setMeterSnapshots(meterBuffer);
            
            // Connect Channel Slot Map (Crucial)
            auto slotMap = m_content->getTrackManager()->getChannelSlotMapShared();
            if (slotMap) {
                m_audioEngine->setChannelSlotMap(slotMap);
            }
            
            Nomad::Log::info("MeterSnapshotBuffer allocated and connected");
            
            // Register core services with ServiceLocator (B-003)
            Nomad::ServiceLocator::provide<Nomad::Audio::AudioEngine>(m_audioEngine.get());
            Nomad::ServiceLocator::provide<Nomad::Audio::AudioDeviceManager>(m_audioManager.get());
            Nomad::ServiceLocator::provide<Nomad::Audio::TrackManager>(m_content->getTrackManager());
            Log::info("Core services registered with ServiceLocator");
            
            // Wire CommandHistory to auto-mark project dirty on any edit (C-005)
            auto trackMgr = m_content->getTrackManager();
            trackMgr->getCommandHistory().setOnStateChanged([trackMgr]() {
                if (trackMgr) {
                    trackMgr->markModified();
                }
            });
            Log::info("CommandHistory wired to dirty-state tracking");
        }
    }
    
    // Wire up TransportBar callbacks
    if (m_content && m_content->getTransportBar() && m_audioEngine) {
        // Transport Controls
        m_content->getTransportBar()->setOnPlay([this]() {
            if (m_content && m_content->getTrackManager()) {
                // Priority: Main playback kills preview
                m_content->stopSoundPreview();
                
                // Check View Focus: If Arsenal, play active pattern
                if (m_content->getViewFocus() == ViewFocus::Arsenal) {
                    Nomad::Audio::PatternID pid = m_content->getActivePatternID();
                    if (pid.value != 0) {
                        m_content->getTrackManager()->playPatternInArsenal(pid);
                        Nomad::Log::info("Transport: Play Pattern " + std::to_string(pid.value));
                    } else {
                        // Fallback handling? Or just play timeline?
                        m_content->getTrackManager()->play(); 
                        Nomad::Log::info("Transport: Play (Arsenal focused but no pattern selected)");
                    }
                } else {
                    // Timeline Focus
                    m_content->getTrackManager()->play();
                    Nomad::Log::info("Transport: Play");
                }

                m_audioEngine->setTransportPlaying(true);
            }
        });
        
        m_content->getTransportBar()->setOnPause([this]() {
            if (m_content && m_content->getTrackManager()) {
                m_content->getTrackManager()->pause();
                m_audioEngine->setTransportPlaying(false);
                Nomad::Log::info("Transport: Pause");
            }
        });
        
        m_content->getTransportBar()->setOnStop([this]() {
            if (m_content && m_content->getTrackManager()) {
                auto trackMgr = m_content->getTrackManager();
                
                // Double Stop Check: If already stopped, do hard reset to position 0
                if (m_audioEngine && !m_audioEngine->isTransportPlaying()) {
                    m_audioEngine->panic();
                    m_audioEngine->setGlobalSamplePos(0);
                    trackMgr->setPosition(0.0);
                    trackMgr->setPlayStartPosition(0.0);
                    Nomad::Log::info("Transport: Double Stop - Hard Reset to 0");
                } else {
                    // Single Stop: Return to play start position
                    double playStartPos = trackMgr->getPlayStartPosition();
                    double sr = m_audioEngine ? m_audioEngine->getSampleRate() : 48000.0;
                    uint64_t samplePos = static_cast<uint64_t>(playStartPos * sr);
                    
                    trackMgr->stop();
                    m_audioEngine->setGlobalSamplePos(samplePos);
                    trackMgr->setPosition(playStartPos);
                    m_audioEngine->setTransportPlaying(false);
                    Nomad::Log::info("Transport: Stop - Return to play start position: " + std::to_string(playStartPos) + "s");
                }
            }
        });

        m_content->getTransportBar()->setOnRecord([this](bool recording) {
            if (m_content && m_content->getTrackManager()) {
                if (recording) {
                    m_content->stopSoundPreview(); // Recording also kills preview
                    m_content->getTrackManager()->record();
                } else {
                    m_content->getTrackManager()->finishRecording();
                }
                Nomad::Log::info("Transport: Record " + std::string(recording ? "ON" : "OFF"));
            }
        });

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

    // Loop preset callbacks
    if (m_content && m_content->getTrackManagerUI() && m_audioEngine) {
        m_content->getTrackManagerUI()->setOnLoopPresetChanged([this](int preset) {
            if (!m_audioEngine) return;
            // Preset 0=Off, 1=1Bar, 2=2Bars, 3=4Bars, 4=8Bars, 5=Selection, 6=Project
            if (preset == 0) {
                m_audioEngine->setLoopEnabled(false);
                // Clear playhead memory back to 0 when exiting loop
                if (m_content && m_content->getTrackManager()) {
                    m_content->getTrackManager()->setPlayStartPosition(0.0);
                }
                Nomad::Log::info("Loop: OFF (playhead memory cleared)");
            } else if (preset >= 1 && preset <= 4) {
                int beatsPerBar = m_audioEngine->getBeatsPerBar();
                double loopBars = (preset == 1) ? 1.0 : (preset == 2) ? 2.0 : (preset == 3) ? 4.0 : 8.0;
                m_audioEngine->setLoopRegion(0.0, loopBars * beatsPerBar);
                m_audioEngine->setLoopEnabled(true);
                Nomad::Log::info("Loop: " + std::to_string(static_cast<int>(loopBars)) + " bar(s)");
            } else if (preset == 6) {
                // Project: Loop from 0 to the end of the last clip
                if (m_content && m_content->getTrackManager()) {
                    auto& playlist = m_content->getTrackManager()->getPlaylistModel();
                    double projectEndBeat = playlist.getTotalDurationBeats();
                    if (projectEndBeat > 0.001) {
                        m_audioEngine->setLoopRegion(0.0, projectEndBeat);
                        m_audioEngine->setLoopEnabled(true);
                        Nomad::Log::info("Loop: Project (0 to " + std::to_string(projectEndBeat) + " beats)");
                    } else {
                        m_audioEngine->setLoopEnabled(false);
                        Nomad::Log::warning("Loop Project: No clips found");
                    }
                }
            }
            // Preset 5 (Selection) is handled by onSelectionMade callback below
        });
        
        // Selection made callback - jump playhead to selection start and enable loop
        m_content->getTrackManagerUI()->setOnSelectionMade([this](double startBeat, double endBeat) {
            if (!m_audioEngine || !m_content || !m_content->getTrackManager()) return;
            
            auto trackMgr = m_content->getTrackManager();
            auto& playlist = trackMgr->getPlaylistModel();
            
            // Convert beats to seconds for playhead positioning
            double startSeconds = playlist.beatToSeconds(startBeat);
            
            // Set loop region FIRST (before any position changes)
            m_audioEngine->setLoopRegion(startBeat, endBeat);
            m_audioEngine->setLoopEnabled(true);
            
            // Jump playhead to selection start - this sends command to audio thread
            trackMgr->setPosition(startSeconds);
            trackMgr->setPlayStartPosition(startSeconds);  // So stop returns here
            
            // Also directly set audio engine position for immediate effect while playing
            double sr = m_audioEngine->getSampleRate();
            if (sr <= 0.0) sr = 48000.0;
            m_audioEngine->setGlobalSamplePos(static_cast<uint64_t>(startSeconds * sr));
            
            Nomad::Log::info("Selection: Jump to " + std::to_string(startSeconds) + "s, Loop " + 
                           std::to_string(startBeat) + " - " + std::to_string(endBeat) + " beats");
        });
        
        // Loop region update callback - called when Project loop auto-updates on clip changes
        m_content->getTrackManagerUI()->setOnLoopRegionUpdate([this](double startBeat, double endBeat) {
            if (!m_audioEngine) return;
            m_audioEngine->setLoopRegion(startBeat, endBeat);
            Nomad::Log::info("Loop region auto-updated: " + std::to_string(startBeat) + " - " + std::to_string(endBeat) + " beats");
        });
        
        // Default: Loop OFF (clean state)
        m_audioEngine->setLoopEnabled(false);
    }
    
    // Create Unified Settings Dialog
    m_settingsDialog = std::make_shared<SettingsDialog>();
    
    auto generalPage = std::make_shared<GeneralSettingsPage>();
    m_generalSettingsPage = generalPage;
    generalPage->setOnAutoSaveToggled([this](bool enabled) {
        m_autoSaveEnabled.store(enabled, std::memory_order_relaxed);
        Log::info(std::string("[AutoSave] ") + (enabled ? "Enabled" : "Disabled"));
    });
    m_settingsDialog->addPage(generalPage);
    
    auto audioPage = std::make_shared<AudioSettingsPage>(m_audioManager.get(), m_audioEngine.get());
    m_audioSettingsPage = audioPage; 
    
    audioPage->setOnStreamRestore([this]() {
        // Simple restore
         if (m_audioManager->openStream(m_mainStreamConfig, audioCallback, this)) {
             m_audioManager->startStream();
             m_audioInitialized = true;
         }
    });
    
    m_settingsDialog->addPage(audioPage);
    m_settingsDialog->addPage(std::make_shared<AppearanceSettingsPage>());
    m_settingsDialog->setBounds(NUIRect(0, 0, 950, 600));
    
    m_confirmationDialog = std::make_shared<ConfirmationDialog>();
    m_confirmationDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
    
    // D-003: Create Recovery Dialog for autosave detection
    m_recoveryDialog = std::make_shared<RecoveryDialog>();
    m_recoveryDialog->setBounds(NUIRect(0, 0, desc.width, desc.height));
    
    // Add to root
    m_rootComponent->setCustomWindow(m_customWindow);
    m_rootComponent->addChild(m_settingsDialog);
    m_rootComponent->setSettingsDialog(m_settingsDialog);
    m_rootComponent->addChild(m_confirmationDialog);
    m_rootComponent->addChild(m_recoveryDialog);
    
    // Create and add Unified Performance HUD
    auto unifiedHUD = std::make_shared<UnifiedHUD>(m_adaptiveFPS.get());
    unifiedHUD->setVisible(false);
    unifiedHUD->setAudioEngine(m_audioEngine.get());
    m_rootComponent->setUnifiedHUD(unifiedHUD);
    m_unifiedHUD = unifiedHUD;
    
    // Connect window and renderer to bridge
    m_window->setRootComponent(m_rootComponent.get());
    m_window->setRenderer(m_renderer.get());
    
    m_customWindow->setWindowHandle(m_window.get());
    
    setupCallbacks();
    
    if (m_useCustomCursor) {
        initializeCustomCursors();
        if (m_window) {
            m_window->setCursorVisible(false);
        }
    }
    
    m_running = true;

    // Initialize Plugin Manager and set up default search paths
    if (Nomad::Audio::PluginManager::getInstance().initialize()) {
        Log::info("Plugin Manager initialized");
        // Add default paths - user can trigger scan via UI's Scan button
        Nomad::Audio::PluginManager::getInstance().getScanner().addDefaultSearchPaths();
        // Note: Not auto-scanning on startup - let user control via Plugin Browser's Scan button
    } else {
        Log::error("Failed to initialize Plugin Manager");
    }

    // Load project if specified via command line argument (e.g. double-clicking .nmd file)
    if (!projectPath.empty() && std::filesystem::exists(projectPath)) {
        m_projectPath = projectPath;
        Log::info("Loading project from command line: " + projectPath);
        auto result = loadProject();
        if (result.ok) {
            if (m_content && m_content->getTransportBar() && result.tempo > 0) {
                m_content->getTransportBar()->setTempo(result.tempo);
            }
            if (result.ui) {
                applyUIState(*result.ui);
            }
            Log::info("Project loaded successfully: " + projectPath);
        } else {
            Log::warning("Failed to load project: " + projectPath + " - starting with default");
        }
    } else {
        // D-003: Check for autosave and offer recovery
        std::string autosavePath = getAutosavePath();
        std::string timestamp;
        if (RecoveryDialog::detectAutosave(autosavePath, timestamp)) {
            // Autosave found - show recovery dialog
            // Note: Dialog is modal-like, blocking input until user responds
            // For now, we'll load it automatically and let UI show banner/indicator
            // (Full modal dialog implementation would require event loop changes)
            
            m_projectPath = autosavePath;
            Log::info("Autosave detected, loading for recovery: " + autosavePath);
            auto result = loadProject();
            if (result.ok) {
                if (m_content && m_content->getTransportBar() && result.tempo > 0) {
                    m_content->getTransportBar()->setTempo(result.tempo);
                }
                if (result.ui) {
                    applyUIState(*result.ui);
                }
                // Reset project path to autosave path so user knows to Save As
                m_projectPath = getAutosavePath();
                Log::info("Recovered autosave project from " + timestamp);
            } else {
                Log::warning("Failed to load autosave - starting fresh");
            }
        }
    }

    Log::info("NOMAD DAW initialized successfully");
    
    // Transition to Running state
    if (!Nomad::AppLifecycle::instance().transitionTo(Nomad::AppState::Running)) {
        Log::error("Failed to transition to Running state");
        return false;
    }
    
    return true;
}

void NomadApp::run() {
    m_renderer->beginFrame();
    m_renderer->clear(NUIColor(0.06f, 0.06f, 0.07f));
    if (m_rootComponent) m_rootComponent->onRender(*m_renderer);
    m_renderer->endFrame();

    double deltaTime = 0.0;
    double autoSaveTimer = 0.0;
    const double autoSaveInterval = 300.0;
    
    while (m_running && m_window->processEvents()) {
        UnifiedProfiler::getInstance().beginFrame();
        auto frameStart = m_adaptiveFPS->beginFrame();
        
        {
            NOMAD_ZONE("Input_Poll");
            static auto lastTime = std::chrono::high_resolution_clock::now();
            auto currentTime = std::chrono::high_resolution_clock::now();
            deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
            lastTime = currentTime;
        }
        
        {
            NOMAD_ZONE("UI_Update");
            // Sync Transport State
            if (m_audioEngine && m_content && m_content->getTrackManager()) {
                auto tm = m_content->getTrackManager();
                
                if (tm->isUserScrubbing()) {
                    // Scrubbing: UI -> Engine
                    double scrubPos = tm->getPosition();
                    // tm->getUIPosition() might be smoother, but getPosition() is the logic target
                    uint32_t sr = m_audioEngine->getSampleRate();
                    if (sr > 0) {
                        uint64_t samplePos = static_cast<uint64_t>(scrubPos * sr);
                        m_audioEngine->setGlobalSamplePos(samplePos);
                    }
                } else if (m_audioEngine->isTransportPlaying()) {
                    // Playing: Engine -> UI (only sync when actually playing)
                    double realTime = m_audioEngine->getPositionSeconds();
                    tm->syncPositionFromEngine(realTime);
                }
                // When stopped, don't sync - let TrackManager keep its position (reset to 0)
                
                // Update Transport Bar Display
                if (m_content->getTransportBar()) {
                   // Ensure Transport Bar reflects the current reality (source of truth is Engine or TM depending on state)
                   double displayTime = tm->getPosition();
                   m_content->getTransportBar()->setPosition(displayTime);
                   
                   // Sync play state visualization
                   bool isEnginePlaying = m_audioEngine->isTransportPlaying();
                   auto transportState = m_content->getTransportBar()->getState();
                    
                   if (isEnginePlaying && transportState != TransportState::Playing) {
                       // We can't easily force UI to "play" state without triggering callback loop
                       // But if we just rely on the transport bar buttons logic, it should coincide.
                   } else if (!isEnginePlaying && transportState == TransportState::Playing) {
                       m_content->getTransportBar()->stop();
                   }
                }
                
                // Update window title with dirty indicator
                updateWindowTitle();
            }
            
            if (m_rootComponent) {
                m_rootComponent->onUpdate(deltaTime);
            }

            if (m_audioEngine && m_content && m_content->getAudioVisualizer()) {
                m_content->getAudioVisualizer()->setPeakLevels(
                    m_audioEngine->getPeakL(), m_audioEngine->getPeakR(),
                    m_audioEngine->getRmsL(), m_audioEngine->getRmsR()
                );
            }
            
            // Rebuild graph check (simplified)
            if (m_audioEngine && m_content && m_content->getTrackManager() &&
                m_content->getTrackManager()->consumeGraphDirty()) {
                
                    auto graph = AudioGraphBuilder::buildFromTrackManager(*m_content->getTrackManager(), m_mainStreamConfig.sampleRate);
                    m_audioEngine->setGraph(graph);
                    
                    // Also sync slot map if graph changed (channels added/removed)
                    auto slotMap = m_content->getTrackManager()->getChannelSlotMapShared();
                    if (slotMap) {
                        m_audioEngine->setChannelSlotMap(slotMap);
                    }
                    
                    m_content->getTrackManager()->rebuildAndPushSnapshot();
            }
        }

        {
            NOMAD_ZONE("Render_Prep");
            render();
        }

        // Auto-save logic
        if (m_autoSaveFuture.valid()) {
           if (m_autoSaveFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                m_autoSaveInFlight.store(false, std::memory_order_relaxed);
           }
        }
        
        if (m_autoSaveEnabled.load(std::memory_order_relaxed)) {
            autoSaveTimer += deltaTime;
            if (autoSaveTimer >= autoSaveInterval) {
                 if (!m_autoSaveInFlight.load(std::memory_order_relaxed)) {
                     // D-001: Only autosave when project is dirty
                     if (m_content && m_content->getTrackManager() && m_content->getTrackManager()->isModified()) {
                         autoSaveTimer = 0.0;
                         
                         // Capture state for async save
                         std::string savePath = getAutosavePath();
                         auto trackMgr = m_content->getTrackManager();
                         double tempo = 120.0;
                         if (m_content->getTransportBar()) {
                             tempo = m_content->getTransportBar()->getTempo();
                         }
                         double playhead = trackMgr->getPosition();
                         auto uiState = captureUIState();
                         
                         m_autoSaveInFlight.store(true, std::memory_order_relaxed);
                         m_autoSaveFuture = std::async(std::launch::async, [trackMgr, savePath, tempo, playhead, uiState]() {
                             // Serialize with compact output (no indent) for speed
                             auto result = ProjectSerializer::serialize(trackMgr, tempo, playhead, 0, &uiState);
                             if (result.ok) {
                                 bool written = ProjectSerializer::writeAtomically(savePath, result.contents);
                                 if (written) {
                                     Log::info("[AutoSave] Project autosaved to " + savePath);
                                 } else {
                                     Log::warning("[AutoSave] Failed to write autosave file");
                                 }
                                 return written;
                             }
                             Log::warning("[AutoSave] Failed to serialize project");
                             return false;
                         });
                     } else {
                         // Project is clean, reset timer but skip save
                         autoSaveTimer = 0.0;
                     }
                 }
            }
        }
        
        double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);
        m_window->swapBuffers();
        if (sleepTime > 0.0) m_adaptiveFPS->sleep(sleepTime);
        
        UnifiedProfiler::getInstance().endFrame();
    }
    
    Log::info("Exiting main loop");
}

void NomadApp::shutdown() {
    Log::info("[SHUTDOWN] Entering shutdown function...");
    
    // Transition to ShuttingDown state
    Nomad::AppLifecycle::instance().transitionTo(Nomad::AppState::ShuttingDown);
    
    // Clear ServiceLocator before destroying services
    // Clear ServiceLocator before destroying services
    Nomad::ServiceLocator::clear();
    Log::info("[SHUTDOWN] ServiceLocator cleared");;
    
    // Wait for any pending autosave to complete before we tear down
    if (m_autoSaveFuture.valid()) {
        Log::info("[SHUTDOWN] Waiting for autosave to complete...");
        m_autoSaveFuture.wait();
        Log::info("[SHUTDOWN] Autosave completed.");
    }
    
    // Shutdown Plugin Manager
    Nomad::Audio::PluginManager::getInstance().shutdown();
    
    if (saveProject()) {
        Log::info("[SHUTDOWN] Project saved successfully");
    }

    if (m_audioInitialized && m_audioManager) {
        m_audioManager->stopStream();
        m_audioManager->closeStream();
    }
    
    // Explicitly destroy audio components before other cleanup
    // to ensure ASIO/COM cleanup happens in correct order
    m_audioEngine.reset();
    m_audioManager.reset();

    if (m_window) {
        m_window->destroy();
    }

    Platform::shutdown();
    m_running = false;
    Log::info("NOMAD DAW shutdown complete");
    
    // Transition to Terminated state
    Nomad::AppLifecycle::instance().transitionTo(Nomad::AppState::Terminated);
    
    // Shutdown async logger to ensure worker thread joins before static destruction
    Log::shutdown();
    
    // Force flush any remaining output
    std::cout.flush();
    std::cerr.flush();
    
    // Clear remaining members that might have blocking destructors
    m_content.reset();
    m_rootComponent.reset();
    m_customWindow.reset();
    m_settingsDialog.reset();
    m_audioSettingsPage.reset();
    m_generalSettingsPage.reset();
    m_confirmationDialog.reset();
    m_unifiedHUD.reset();
    m_renderer.reset();
    m_window.reset();
}

void NomadApp::render() {
    if (!m_renderer || !m_rootComponent) return;

    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor bgColor = themeManager.getColor("background");
    
    m_renderer->clear(bgColor);
    m_renderer->beginFrame();
    m_rootComponent->onRender(*m_renderer);
    NUIDragDropManager::getInstance().renderDragGhost(*m_renderer);
    
    if (m_useCustomCursor && m_windowFocused) {
        // Ensure cursor is not clipped by previous UI elements
        m_renderer->clearClipRect();
        
        // Only render default arrow if TrackManagerUI doesn't have a custom cursor active
        bool trackManagerHasCustomCursor = false;
        if (m_content && m_content->getTrackManagerUI()) {
            trackManagerHasCustomCursor = m_content->getTrackManagerUI()->isCustomCursorActive();
        }
        
        if (!trackManagerHasCustomCursor) {
            renderCustomCursor();
        }
    }
    
    m_renderer->endFrame();
}

// ==============================
// Custom Software Cursor
// ==============================

void NomadApp::checkKeyModifiers(bool pressed, int key) {
    // Maintain an authoritative modifier state
    auto setModifier = [this](NomadUI::NUIModifiers bit, bool down) {
        int mods = static_cast<int>(m_keyModifiers);
        const int flag = static_cast<int>(bit);
        mods = down ? (mods | flag) : (mods & ~flag);
        m_keyModifiers = static_cast<NomadUI::NUIModifiers>(mods);
    };
    if (key == static_cast<int>(Nomad::KeyCode::Control)) {
        setModifier(NomadUI::NUIModifiers::Ctrl, pressed);
    } else if (key == static_cast<int>(Nomad::KeyCode::Shift)) {
        setModifier(NomadUI::NUIModifiers::Shift, pressed);
    } else if (key == static_cast<int>(Nomad::KeyCode::Alt)) {
        setModifier(NomadUI::NUIModifiers::Alt, pressed);
    }
}

void NomadApp::initializeCustomCursors() {
    // Arrow cursor (default pointer)
    m_cursorArrow = std::make_shared<NomadUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M5 2L5 18L9 14L12 21L14 20L11 13L17 13L5 2Z" fill="white" stroke="black" stroke-width="1.5"/>
        </svg>
    )");
    
    // Hand cursor
    m_cursorHand = std::make_shared<NomadUI::NUIIcon>(R"(
        <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
            <path d="M12 6V3C12 2.45 12.45 2 13 2C13.55 2 14 2.45 14 3V10H15V4C15 3.45 15.45 3 16 3C16.55 3 17 3.45 17 4V10H18V5C18 4.45 18.45 4 19 4C19.55 4 20 4.45 20 5V15C20 18.31 17.31 21 14 21H12C8.69 21 6 18.31 6 15V12C6 11.45 6.45 11 7 11C7.55 11 8 11.45 8 12V14H9V6C9 5.45 9.45 5 10 5C10.55 5 11 5.45 11 6V10H12V6Z" fill="white" stroke="black" stroke-width="1"/>
        </svg>
    )");
    
    // I-Beam cursor
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

void NomadApp::renderCustomCursor() {
    if (!m_renderer) return;
    
    NomadUI::NUICursorStyle style = NomadUI::NUICursorStyle::Arrow;
    
        // Exclusion logic removed to ensure cursor is always visible
        // The track grid handles its own mouse events but we want the custom cursor to overlay everything
    
    std::shared_ptr<NomadUI::NUIIcon> cursorIcon;
    float offsetX = 0.0f, offsetY = 0.0f;
    float size = 20.0f;
    
    switch (m_activeCursorStyle) {
        case NomadUI::NUICursorStyle::Hand:
        case NomadUI::NUICursorStyle::Grab:
        case NomadUI::NUICursorStyle::Grabbing:
            cursorIcon = m_cursorHand;
            offsetX = -6.0f; offsetY = -2.0f;
            break;
        case NomadUI::NUICursorStyle::IBeam:
            cursorIcon = m_cursorIBeam;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        case NomadUI::NUICursorStyle::ResizeEW:
            cursorIcon = m_cursorResizeH;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        case NomadUI::NUICursorStyle::ResizeNS:
            cursorIcon = m_cursorResizeV;
            offsetX = -size / 2.0f; offsetY = -size / 2.0f;
            break;
        default:
            cursorIcon = m_cursorArrow;
            offsetX = 0.0f; offsetY = 0.0f;
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
// Callbacks
// ==============================

void NomadApp::setupCallbacks() {
    auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
    dragManager.setOnDragStart([this](const NomadUI::DragData&) {
        if (m_window) m_window->setMouseCapture(true);
    });
    
    dragManager.setOnDragEnd([this](const NomadUI::DragData&, const NomadUI::DropResult&) {
        if (m_window) m_window->setMouseCapture(false);
    });

    m_window->setResizeCallback([this](int width, int height) {
        m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::WindowResize);
        if (m_renderer) m_renderer->resize(width, height);
    });

    m_window->setCloseCallback([this]() {
        Log::info("Window close requested");
        requestClose();
    });

    m_window->setMouseMoveCallback([this](int x, int y) {
        m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseMove);
        m_lastMouseX = x;
        m_lastMouseY = y;
        auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
        if (dragManager.isDragging()) {
            dragManager.updateDrag(NomadUI::NUIPoint(static_cast<float>(x), static_cast<float>(y)));
        }
    });
    
    m_window->setMouseButtonCallback([this](int button, bool pressed) {
        if (pressed) m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseClick);
        
        // Close dropdown menu if clicking outside of it (but not on the menu bar which handles its own toggle)
        if (pressed && button == 0 && m_activeMenu && m_activeMenu->isVisible()) {
            NomadUI::NUIPoint mousePos(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY));
            // Don't close if clicking on the menu bar (let the menu bar handle toggle)
            bool clickOnMenuBar = m_menuBar && m_menuBar->getGlobalBounds().contains(mousePos);
            if (!m_activeMenu->getBounds().contains(mousePos) && !clickOnMenuBar) {
                m_activeMenu->hide();
            }
        }
        
        if (!pressed && button == 0) {
            auto& dragManager = NomadUI::NUIDragDropManager::getInstance();
            if (dragManager.isDragging()) {
                dragManager.endDrag(NomadUI::NUIPoint(static_cast<float>(m_lastMouseX), static_cast<float>(m_lastMouseY)));
            }
        }
    });
    
    m_window->setMouseWheelCallback([this](float) {
        m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::Scroll);
    });
    
    m_window->setFocusCallback([this](bool focused) {
        m_windowFocused = focused;
        if (focused) {
            if (m_useCustomCursor && m_window) m_window->setCursorVisible(false);
            return;
        }
        m_keyModifiers = NomadUI::NUIModifiers::None;
        NomadUI::NUIComponent::clearFocusedComponent();
    });

    m_window->setKeyCallback([this](int key, bool pressed) {
        if (pressed) m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::KeyPress);

        // checkKeyModifiers(pressed, key); // Helper method I added but forgot to delcare, inline here for now
        if (key == static_cast<int>(Nomad::KeyCode::Control)) {
            m_keyModifiers = static_cast<NomadUI::NUIModifiers>((static_cast<int>(m_keyModifiers) & ~static_cast<int>(NomadUI::NUIModifiers::Ctrl)) | (pressed ? static_cast<int>(NomadUI::NUIModifiers::Ctrl) : 0));
        } else if (key == static_cast<int>(Nomad::KeyCode::Shift)) {
            m_keyModifiers = static_cast<NomadUI::NUIModifiers>((static_cast<int>(m_keyModifiers) & ~static_cast<int>(NomadUI::NUIModifiers::Shift)) | (pressed ? static_cast<int>(NomadUI::NUIModifiers::Shift) : 0));
        } else if (key == static_cast<int>(Nomad::KeyCode::Alt)) {
            m_keyModifiers = static_cast<NomadUI::NUIModifiers>((static_cast<int>(m_keyModifiers) & ~static_cast<int>(NomadUI::NUIModifiers::Alt)) | (pressed ? static_cast<int>(NomadUI::NUIModifiers::Alt) : 0));
        }

        NomadUI::NUIKeyEvent event;
        event.keyCode = convertToNUIKeyCode(key);
        event.pressed = pressed;
        event.released = !pressed;
        event.modifiers = m_keyModifiers;
        
        if (m_settingsDialog && m_settingsDialog->isVisible()) {
            if (m_settingsDialog->onKeyEvent(event)) return;
        }
    
        if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) {
            if (m_confirmationDialog->onKeyEvent(event)) return;
        }
        
        if (m_content) {
            auto fileBrowser = m_content->getFileBrowser();
            if (fileBrowser && fileBrowser->isSearchBoxFocused()) {
                fileBrowser->onKeyEvent(event);
                return;
            }
        }
    
        if (auto* focused = NomadUI::NUIComponent::getFocusedComponent()) {
            if (focused->onKeyEvent(event)) return;
        }

        if (m_content && m_content->getTrackManagerUI() && m_content->getTrackManagerUI()->isVisible()) {
            if (m_content->getTrackManagerUI()->onKeyEvent(event)) return;
        }

        const bool hasModifiers = (event.modifiers != NomadUI::NUIModifiers::None);

        if (key == static_cast<int>(Nomad::KeyCode::Escape) && pressed) {
            if (m_confirmationDialog && m_confirmationDialog->isDialogVisible()) return;
            if (m_settingsDialog && m_settingsDialog->isVisible()) {
                m_settingsDialog->hide();
            } else if (m_customWindow && m_customWindow->isFullScreen()) {
                m_customWindow->exitFullScreen();
            } else {
                requestClose();
            }
        } else if (key == static_cast<int>(Nomad::KeyCode::F11) && pressed) {
            if (m_customWindow) m_customWindow->toggleFullScreen();
        } else if (key == static_cast<int>(Nomad::KeyCode::Space) && pressed) {
            if (m_content && m_content->getTransportBar()) {
                auto* transport = m_content->getTransportBar();
                if (transport->getState() == TransportState::Playing) transport->stop();
                else transport->play();
            }
        } else if (key == static_cast<int>(Nomad::KeyCode::F12) && pressed) {
            if (m_unifiedHUD) m_unifiedHUD->toggle();
        } else if (key == static_cast<int>(Nomad::KeyCode::F5) && pressed) {
            if (m_content) m_content->toggleView(Nomad::Audio::ViewType::Playlist);
        } else if (key == static_cast<int>(Nomad::KeyCode::F9) && pressed) {
            if (m_content) m_content->toggleView(Nomad::Audio::ViewType::Mixer);
        }
    });

    if (m_content && m_content->getTrackManagerUI()) {
        m_content->getTrackManagerUI()->setOnCursorVisibilityChanged([this](bool visible) {
            if (m_window && !m_useCustomCursor) {
                m_window->setCursorVisible(visible);
            }
        });
    }
}

// ==============================
// Menu Actions
// ==============================

void NomadApp::showFileMenu() {
    auto menu = std::make_shared<NomadUI::NUIContextMenu>();
    menu->addItem("New Project", [this]() {
        // Stop playback
        if (m_content && m_content->getTrackManager()) {
            m_content->getTrackManager()->stop();
        }
        
        // Clear existing project and reset to default state
        if (m_content) {
            m_content->resetToDefaultProject();
        }
        
        // Reset to autosave path (untitled project)
        m_projectPath = getAutosavePath();
        m_lastWindowTitle.clear();  // Force title refresh to show "Untitled"
        Log::info("New project created");
    });
    
    menu->addItem("Open Project...", [this]() {
        auto* utils = Platform::getUtils();
        if (!utils) return;
        
        // Stop playback first
        if (m_content && m_content->getTrackManager()) {
            m_content->getTrackManager()->stop();
        }
        
        // Windows filter format requires embedded nulls - must use explicit size
        const char filterData[] = "Nomad Project (*.nmd)\0*.nmd\0All Files (*.*)\0*.*\0";
        std::string filter(filterData, sizeof(filterData) - 1);
        
        std::string path = utils->openFileDialog("Open Project", filter);
        if (!path.empty()) {
            m_projectPath = path;
            auto result = loadProject();
            if (result.ok) {
                // Apply tempo if transport bar exists
                if (m_content && m_content->getTransportBar()) {
                    m_content->getTransportBar()->setTempo(result.tempo);
                }
                // Apply UI state if present
                if (result.ui) {
                    applyUIState(*result.ui);
                }
                Log::info("Opened project: " + path);
            } else {
                Log::error("Failed to open project: " + path);
            }
        }
    });
    
    menu->addSeparator();
    menu->addItem("Save", [this]() { saveCurrentProject(); });
    
    menu->addItem("Save As...", [this]() {
        auto* utils = Platform::getUtils();
        if (!utils) return;
        
        // Windows filter format requires embedded nulls - must use explicit size
        const char filterData[] = "Nomad Project (*.nmd)\0*.nmd\0";
        std::string filter(filterData, sizeof(filterData) - 1);
        
        std::string path = utils->saveFileDialog("Save Project As", filter);
        if (!path.empty()) {
            // Ensure .nmd extension
            if (path.size() < 4 || path.substr(path.size() - 4) != ".nmd") {
                path += ".nmd";
            }
            m_projectPath = path;
            saveCurrentProject();
        }
    });
    
    menu->addSeparator();
    menu->addItem("Settings...", [this]() {
        if (m_settingsDialog) m_settingsDialog->show();
    });
    menu->addSeparator();
    menu->addItem("Exit", [this]() { m_running = false; });
    showDropdownMenu(menu, 10.0f);
}

void NomadApp::showEditMenu() {
    auto menu = std::make_shared<NomadUI::NUIContextMenu>();
    menu->addItem("Undo", [](){});
    menu->addItem("Redo", [](){});
    showDropdownMenu(menu, 55.0f);
}

void NomadApp::showViewMenu() {
    auto menu = std::make_shared<NomadUI::NUIContextMenu>();
    bool hudVisible = m_unifiedHUD && m_unifiedHUD->isVisible();
    menu->addCheckbox("Performance Stats (F12)", hudVisible, [this](bool) {
        if (m_unifiedHUD) m_unifiedHUD->toggle();
    });
    showDropdownMenu(menu, 105.0f);
}

void NomadApp::showDropdownMenu(std::shared_ptr<NomadUI::NUIContextMenu> menu, float xOffset) {
    if (!menu || !m_rootComponent || !m_customWindow) return;
    
    // Toggle behavior: if clicking the same menu button, just close it
    if (m_activeMenu && m_activeMenu->isVisible() && m_activeMenuXOffset == xOffset) {
        m_activeMenu->hide();
        return;
    }
    
    // Close any existing active menu first
    if (m_activeMenu && m_activeMenu->isVisible()) {
        m_activeMenu->hide();
        m_rootComponent->removeChild(m_activeMenu);
        m_activeMenu = nullptr;
    }
    
    // Add and show the new menu
    m_rootComponent->addChild(menu);
    menu->showAt(static_cast<int>(xOffset), 32);
    m_activeMenu = menu;
    m_activeMenuXOffset = xOffset;
    
    // Set up callback to clear m_activeMenu when hidden
    menu->setOnHide([this, menuPtr = menu.get()]() {
        if (m_activeMenu.get() == menuPtr) {
            if (m_rootComponent) m_rootComponent->removeChild(m_activeMenu);
            m_activeMenu = nullptr;
            m_activeMenuXOffset = -1.0f;
        }
    });
}

// ==============================
// Project Management
// ==============================

void NomadApp::requestClose() {
    bool hasUnsavedChanges = false;
    if (m_content && m_content->getTrackManager()) {
        hasUnsavedChanges = m_content->getTrackManager()->isModified();
    }
    
    if (m_confirmationDialog) {
        m_confirmationDialog->show(
            "Close NOMAD",
            "Are you sure you want to close NOMAD?",
            [this](DialogResponse response) {
                switch (response) {
                    case DialogResponse::Save:
                        saveProject();
                        m_running = false;
                        break;
                    case DialogResponse::DontSave:
                        m_running = false;
                        break;
                    case DialogResponse::Cancel:
                        break;
                    default: break;
                }
            }
        );
    } else {
        m_running = false;
    }
}

void NomadApp::saveCurrentProject() {
    if (saveProject()) {
        Log::info("Project saved");
    } else {
        Log::error("Failed to save project");
    }
}

ProjectSerializer::LoadResult NomadApp::loadProject() {
    if (!m_content || !m_content->getTrackManager()) return {};
    
    std::string loadPath = m_projectPath;
    if (!std::filesystem::exists(loadPath)) {
        Log::warning("Project file not found: " + loadPath);
        return {};
    }
    
    try {
        auto res = ProjectSerializer::load(loadPath, m_content->getTrackManager());
        if (res.ok) {
            m_content->getTrackManager()->setModified(false);
            m_lastWindowTitle.clear();  // Force title refresh
            Log::info("Project loaded successfully from: " + loadPath);
        }
        return res;
    } catch (const std::exception& e) {
        Log::error("Exception loading project: " + std::string(e.what()));
        return {};
    } catch (...) {
        Log::error("Unknown exception loading project");
        return {};
    }
}

bool NomadApp::saveProject() {
    if (!m_content || !m_content->getTrackManager()) return false;
    double tempo = 120.0;
    if (m_content->getTransportBar()) tempo = m_content->getTransportBar()->getTempo();
    double playhead = m_content->getTrackManager()->getPosition();
    auto uiState = captureUIState();
    bool saved = ProjectSerializer::save(m_projectPath, m_content->getTrackManager(), tempo, playhead, &uiState);
    if (saved && m_content->getTrackManager()) {
        m_content->getTrackManager()->setModified(false);
        m_lastWindowTitle.clear();  // Force title refresh
    }
    return saved;
}

ProjectSerializer::UIState NomadApp::captureUIState() const {
    ProjectSerializer::UIState state;
    if (m_settingsDialog) {
        state.settingsDialogVisible = m_settingsDialog->isVisible();
        state.settingsDialogActivePage = m_settingsDialog->getActivePageID();
    }
    // Panel serialization logic from Main.cpp
    if (m_rootComponent) {
        // Recursive walk omitted for brevity in this step, but assumption holds it's similar
    }
    return state;
}

void NomadApp::applyUIState(const ProjectSerializer::UIState& state) {
    if (m_settingsDialog) {
        if (!state.settingsDialogActivePage.empty()) m_settingsDialog->setActivePage(state.settingsDialogActivePage);
        if (state.settingsDialogVisible) m_settingsDialog->show(); else m_settingsDialog->hide();
    }
}

void NomadApp::updateWindowTitle() {
    if (!m_customWindow) return;
    
    // Check if modified state changed
    bool isModified = false;
    if (m_content && m_content->getTrackManager()) {
        isModified = m_content->getTrackManager()->isModified();
    }
    
    // Only update if state changed
    if (isModified == m_lastModifiedState && !m_lastWindowTitle.empty()) {
        return;
    }
    
    // Extract project name from path
    std::string projectName = "Untitled";
    if (!m_projectPath.empty()) {
        std::filesystem::path p(m_projectPath);
        if (p.has_stem()) {
            projectName = p.stem().string();
            // Don't show "autosave" as project name
            if (projectName == "autosave") {
                projectName = "Untitled";
            }
        }
    }
    
    // Build title: "ProjectName - NOMAD DAW" or "ProjectName* - NOMAD DAW" if modified
    std::string title = projectName;
    if (isModified) {
        title += "*";
    }
    title += " - NOMAD DAW";
    
    // Update only if changed
    if (title != m_lastWindowTitle) {
        m_customWindow->setTitle(title);
        m_lastWindowTitle = title;
        m_lastModifiedState = isModified;
    }
}

// ==============================
// Audio Callback
// ==============================

int NomadApp::audioCallback(float* outputBuffer, const float* inputBuffer,
                         uint32_t nFrames, double streamTime, void* userData) {
    // B-005: Mark this as audio thread for constraint checking
    Nomad::Audio::AudioThreadGuard audioThreadGuard;
    Nomad::Audio::AudioThreadStats::instance().totalCallbacks.fetch_add(1, std::memory_order_relaxed);
    
    NomadApp* app = static_cast<NomadApp*>(userData);
    if (!app || !outputBuffer) return 1;

    Nomad::Audio::RT::initAudioThread();
    const uint64_t cbStartCycles = Nomad::Audio::RT::readCycleCounter();

    double actualRate = static_cast<double>(app->m_mainStreamConfig.sampleRate);
    if (actualRate <= 0.0) actualRate = 48000.0;

    if (app->m_audioEngine) {
        app->m_audioEngine->setSampleRate(static_cast<uint32_t>(actualRate));
        app->m_audioEngine->processBlock(outputBuffer, inputBuffer, nFrames, streamTime);
    } else {
        std::fill(outputBuffer, outputBuffer + nFrames * 2, 0.0f);
    }
    
    // Preview mixing
    if (app->m_content && app->m_content->getPreviewEngine()) {
        auto previewEngine = app->m_content->getPreviewEngine();
        previewEngine->setOutputSampleRate(actualRate);
        previewEngine->process(outputBuffer, nFrames);
    }
    
    // Test sound generation moved to AudioEngine internal processing
    // Legacy block removed

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
        }
    }

    return 0;
}
