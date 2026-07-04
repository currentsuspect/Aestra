#include "AudioSettingsPage.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraCore/include/AestraLog.h"
#include "PlaylistMixer.h"
#include "ClipResampler.h"
#include "AestraPlatform.h"
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <iomanip>
#include <filesystem>

namespace Aestra {

using namespace Aestra::Audio;

namespace {
std::filesystem::path getAudioSettingsConfigPath(bool* outIsLegacy = nullptr) {
    if (auto* utils = Aestra::Platform::getUtils()) {
        std::error_code ec;
        std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
        if (!appDataDir.empty()) {
            std::filesystem::create_directories(appDataDir, ec);
            if (!ec) {
                std::filesystem::path appDataPath = appDataDir / "audio_settings.conf";
                if (std::filesystem::exists(appDataPath, ec)) {
                    if (outIsLegacy) *outIsLegacy = false;
                    return appDataPath;
                }
            }
        }
    }
    std::filesystem::path legacyPath = std::filesystem::current_path() / "audio_settings.conf";
    std::error_code ec;
    if (std::filesystem::exists(legacyPath, ec)) {
        if (outIsLegacy) *outIsLegacy = true;
        return legacyPath;
    }
    if (outIsLegacy) *outIsLegacy = false;
    if (auto* utils = Aestra::Platform::getUtils()) {
        std::error_code ec;
        std::filesystem::path appDataDir(utils->getAppDataPath("Aestra"));
        if (!appDataDir.empty()) {
            return appDataDir / "audio_settings.conf";
        }
    }
    return std::filesystem::current_path() / "audio_settings.conf";
}
}

// ============================================================================
// ThreadCountDisplay Implementation
// ============================================================================

ThreadCountDisplay::ThreadCountDisplay() 
    : m_value(1), m_min(1), m_max(64), m_dragStartValue(0)
{
    const char* upArrowSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 14l5-5 5 5z"/></svg>)";
    m_upArrow = std::make_shared<AestraUI::NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(AestraUI::NUIIconSize::Small);
    
    const char* downArrowSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 10l5 5 5-5z"/></svg>)";
    m_downArrow = std::make_shared<AestraUI::NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(AestraUI::NUIIconSize::Small);
}

void ThreadCountDisplay::setValue(int val) {
    int old = m_value;
    m_value = std::max(m_min, std::min(m_max, val));
    if (m_value != old && m_onChange) m_onChange(m_value);
}

void ThreadCountDisplay::setRange(int minVal, int maxVal) {
    m_min = minVal; m_max = maxVal;
    setValue(m_value);
}

AestraUI::NUIRect ThreadCountDisplay::getUpArrowBounds() const {
    AestraUI::NUIRect bounds = getBounds();
    return AestraUI::NUIRect(bounds.x + bounds.width - 24, bounds.y + 2, 20, 12);
}

AestraUI::NUIRect ThreadCountDisplay::getDownArrowBounds() const {
    AestraUI::NUIRect bounds = getBounds();
    return AestraUI::NUIRect(bounds.x + bounds.width - 24, bounds.y + 16, 20, 12);
}

void ThreadCountDisplay::onUpdate(double deltaTime) {
    if (m_upArrowPressed || m_downArrowPressed) {
        m_holdTimer += (float)deltaTime;
        if (m_holdTimer > 0.4f) { 
             if (m_upArrowPressed) setValue(m_value + 1);
             else setValue(m_value - 1);
             m_holdTimer = 0.35f; 
        }
    }
}

void ThreadCountDisplay::onRender(AestraUI::NUIRenderer& renderer) {
    AestraUI::NUIRect bounds = getBounds();
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    const float radius = themeManager.getRadius("m");
    
    // Background (Dark Pill)
    AestraUI::NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f);
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border
    AestraUI::NUIColor borderColor = m_isHovered || m_isDragging ? themeManager.getColor("accentPrimary") : themeManager.getColor("glassBorder");
    renderer.strokeRoundedRect(bounds, radius, 1.0f, borderColor);
    
    // Text
    std::string text = std::to_string(m_value) + " Threads";
    float fontSize = themeManager.getFontSize("m");
    AestraUI::NUIColor textColor = themeManager.getColor("textPrimary");
    
    AestraUI::NUISize textSize = renderer.measureText(text, fontSize);
    float textX = bounds.x + 10;
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    renderer.drawText(text, AestraUI::NUIPoint(textX, textY), fontSize, textColor);
    
    // Arrows
    if (m_upArrow) {
        m_upArrow->setBounds(getUpArrowBounds());
        m_upArrow->setColor(m_upArrowHovered || m_upArrowPressed ? themeManager.getColor("accentPrimary") : themeManager.getColor("textSecondary"));
        m_upArrow->onRender(renderer);
    }
    if (m_downArrow) {
        m_downArrow->setBounds(getDownArrowBounds());
        m_downArrow->setColor(m_downArrowHovered || m_downArrowPressed ? themeManager.getColor("accentPrimary") : themeManager.getColor("textSecondary"));
        m_downArrow->onRender(renderer);
    }
}

bool ThreadCountDisplay::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    AestraUI::NUIRect bounds = getBounds();
    
    // Update hover states
    if (!m_isDragging) {
        if (!bounds.contains(event.position)) {
            m_isHovered = false;
            m_upArrowHovered = false; 
            m_downArrowHovered = false;
            return false;
        }
        m_isHovered = true;
        m_upArrowHovered = getUpArrowBounds().contains(event.position);
        m_downArrowHovered = getDownArrowBounds().contains(event.position);
    }
    
    // Handle Press
    if (event.pressed && event.button == AestraUI::NUIMouseButton::Left) {
        // Double click to reset
        if (event.doubleClick) {
             int def = (int)std::thread::hardware_concurrency();
             if (def < 1) def = 4;
             setValue(def);
             return true;
        }
        
        if (m_upArrowHovered) {
             m_upArrowPressed = true; m_holdTimer = 0.0f;
             setValue(m_value + 1);
             return true;
        } else if (m_downArrowHovered) {
             m_downArrowPressed = true; m_holdTimer = 0.0f;
             setValue(m_value - 1);
             return true;
        } else if (bounds.contains(event.position)) {
            // Drag Start on the main body
            m_isDragging = true;
            m_dragStartPos = event.position;
            m_dragStartValue = m_value;
            // Hide cursor could be here if supported
            return true;
        }
    }
    
    // Handle Dragging (Move while dragging)
    if (m_isDragging && !event.pressed && !event.released) {
        float deltaY = m_dragStartPos.y - event.position.y;
        int change = (int)(deltaY / 5.0f); // Revert to 5 pixels per count
        setValue(m_dragStartValue + change);
        return true;
    }
    
    // Handle Release
    if (event.released) {
        m_isDragging = false;
        m_upArrowPressed = false;
        m_downArrowPressed = false;
        return true;
    }
    
    return true; 
}

AudioSettingsPage::AudioSettingsPage(AudioDeviceManager* audioManager, AudioEngine* engine)
    : m_audioManager(audioManager)
    , m_audioEngine(engine)
    , m_dirty(false)
    , m_isInitializing(true)
{
    Log::info("[AudioSettingsPage] Constructor started (with button init)");
    createUI(); // Create UI elements FIRST
    loadCurrentSettings(); // Load initial state
    
    // Create loading indicator
    m_loadingLabel = std::make_shared<AestraUI::NUILabel>();
    m_loadingLabel->setText("Loading audio devices...");
    m_loadingLabel->setVisible(false);
    addChild(m_loadingLabel);
    
    m_testSoundButton = std::make_shared<AestraUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setOnClick([this]() {
        bool playing = m_audioEngine->isTestToneEnabled();
        playing = !playing;
        m_audioEngine->setTestToneEnabled(playing);
        
        m_testSoundButton->setText(playing ? "Stop Test Sound" : "Test Sound");
        
        // NOTE: No stream restore needed - test tone uses existing audio callback
    });
    addChild(m_testSoundButton);
    
    // Start async device enumeration (non-blocking)
    startAsyncDeviceLoad();
}

AudioSettingsPage::~AudioSettingsPage() {
    // Ensure background device enumeration thread is stopped
    if (m_deviceLoadThread.joinable()) {
        m_deviceLoadThread.join();
    }
}

void AudioSettingsPage::loadCurrentSettings() {
    // Mock loading from manager
    // In reality: m_originalState.sampleRate = m_audioManager->getSampleRate(); etc.
    
    // Set UI to match state
    m_dirty = false;
}

void AudioSettingsPage::createUI() {
    Log::info("[AudioSettingsPage] createUI started");
    // Labels
    auto createLabel = [&](const std::string& text) {
        auto l = std::make_shared<AestraUI::NUILabel>();
        l->setText(text);
        addChild(l);
        return l;
    };
    
    m_driverLabel = createLabel("Audio Driver:");
    m_deviceLabel = createLabel("Output Device:");
    m_inputDeviceLabel = createLabel("Input Device:");
    m_sampleRateLabel = createLabel("Sample Rate:");
    m_bufferSizeLabel = createLabel("Buffer Size:");
    m_latencyLabel = createLabel("Est. Latency: -- ms");
    m_qualityPresetLabel = createLabel("Quality Preset:");
    m_resamplingLabel = createLabel("Resampling:");
    m_ditheringLabel = createLabel("Dithering:");
    m_dcRemovalLabel = createLabel("DC Removal:");
    m_softClippingLabel = createLabel("Master Limiter:");
    m_multiThreadingLabel = createLabel("Multi-threading:");
    m_threadCountLabel = createLabel("Thread Count:");
    
    // Dropdowns
    auto createDropdown = [&](std::function<void(int)> onChange) {
        auto d = std::make_shared<AestraUI::NUIDropdown>();
        d->setOnSelectionChanged([onChange](int index, int, const std::string&) {
            onChange(index);
        });
        addChild(d);
        return d;
    };

    m_driverDropdown = createDropdown([this](int idx) {
        m_dirty = true;

        // Guard: skip audio calls during initialization and async UI population.
        // The stream was opened once by AudioEngineController. UI must not mutate it.
        if (m_isInitializing || m_isPopulatingDeviceUI) return;

        // SWITCH DRIVER IMMEDIATELY so device list is correct
        if (m_audioManager) {
             m_audioManager->setPreferredDriverType((AudioDriverType)m_driverDropdown->getSelectedValue());
        }

        // When driver changes, re-enumerate devices off the UI thread (#256)
        startAsyncDeviceLoad();
    });

    m_deviceDropdown = createDropdown([this](int idx) {
        m_dirty = true;

        if (m_isInitializing || m_isPopulatingDeviceUI) return;

        // SWITCH DEVICE IMMEDIATELY
        if (m_audioManager) {
            m_audioManager->switchDevice((uint32_t)m_deviceDropdown->getSelectedValue());
        }
    });

    m_inputDeviceDropdown = createDropdown([this](int idx) {
        m_dirty = true;

        if (m_isInitializing || m_isPopulatingDeviceUI) return;

        if (m_audioManager) {
            const int selected = m_inputDeviceDropdown->getSelectedValue();
            if (selected >= 0) {
                m_audioManager->switchInputDevice(static_cast<uint32_t>(selected));
            }
        }
    });

    m_sampleRateDropdown = createDropdown([this](int idx) {
        m_dirty = true;
        if (m_isInitializing || m_isPopulatingDeviceUI) return;
        if (!m_audioManager) return;

        uint32_t requested = (uint32_t)m_sampleRateDropdown->getSelectedValue();
        bool ok = m_audioManager->setSampleRate(requested);

        if (!ok) {
            uint32_t current = m_audioEngine->getSampleRate();
            m_isPopulatingDeviceUI = true;
            m_sampleRateDropdown->setSelectedByValue((int)current);
            m_isPopulatingDeviceUI = false;
            m_dirty = false;
            AESTRA_LOG_STREAM_WARNING << "[AudioSettingsPage] setSampleRate("
                << requested << ") failed -- restored to " << current;
        } else {
            updateLatencyEstimate();
        }
    });

    m_bufferSizeDropdown = createDropdown([this](int idx) {
        m_dirty = true;

        if (m_isInitializing || m_isPopulatingDeviceUI) return;

        uint32_t newBufferSize = (uint32_t)m_bufferSizeDropdown->getSelectedValue();
        if (m_audioManager) {
             bool success = m_audioManager->setBufferSize(newBufferSize);
             if (success && m_audioEngine) {
                 m_audioEngine->setBufferConfig(newBufferSize, 2);
             }
        }
        updateLatencyEstimate();
    });
    
    m_qualityPresetDropdown = createDropdown([this](int idx) { 
        m_dirty = true; 
        if (idx == 0) { // Eco
             m_resamplingDropdown->setSelectedByValue(0); // Fast
             m_ditheringDropdown->setSelectedByValue((int)DitheringMode::None);
        } else if (idx == 1) { // Normal
             m_resamplingDropdown->setSelectedByValue(2); // Cubic
             m_ditheringDropdown->setSelectedByValue((int)DitheringMode::Triangular);
        } else if (idx == 2) { // High
             m_resamplingDropdown->setSelectedByValue(3); // Sinc64
             m_ditheringDropdown->setSelectedByValue((int)DitheringMode::Triangular);
        }
    });
    m_qualityPresetDropdown->addItem("Eco", 0);
    m_qualityPresetDropdown->addItem("High Quality", 2);
    // Move selection to AFTER all dependent dropdowns are created to avoid crash
    
    m_resamplingDropdown = createDropdown([this](int idx) { 
        m_dirty = true; 
    });
    m_resamplingDropdown->addItem("Fast (Linear)", 0);
    m_resamplingDropdown->addItem("Draft (Sinc32)", 1);
    m_resamplingDropdown->addItem("Standard (Cubic)", 2);
    m_resamplingDropdown->addItem("High (Sinc64)", 3);
    
    m_ditheringDropdown = createDropdown([this](int idx) { m_dirty = true; });
    m_ditheringDropdown->addItem("None", (int)DitheringMode::None);
    m_ditheringDropdown->addItem("Triangular (TPDF)", (int)DitheringMode::Triangular);
    m_ditheringDropdown->addItem("High Pass", (int)DitheringMode::HighPass);
    m_ditheringDropdown->addItem("Noise Shaped", (int)DitheringMode::NoiseShaped);
    
    // Now safe to set quality preset which triggers callback
    m_qualityPresetDropdown->setSelectedIndex(1);
    
    m_threadCountInput = std::make_shared<ThreadCountDisplay>();
    int maxThreads = (int)std::thread::hardware_concurrency();
    if (maxThreads < 1) maxThreads = 4;
    m_threadCountInput->setRange(1, maxThreads);
    m_threadCountInput->setValue(maxThreads);
    m_threadCountInput->setOnChange([this](int val) { m_dirty = true; });
    addChild(m_threadCountInput);

    // Toggles
    auto createCheck = [&](std::function<void(bool)> onChange) {
        auto t = std::make_shared<AestraUI::NUIToggle>();
        t->setOnToggle(onChange);
        addChild(t);
        return t;
    };
    
    m_dcRemovalToggle = createCheck([this](bool v) { m_dirty = true; });
    m_softClippingToggle = createCheck([this](bool v) { m_dirty = true; });
    m_multiThreadingToggle = createCheck([this](bool v) { 
        m_dirty = true; 
        layoutComponents(); // Update layout to show/hide thread count
    });

    m_testSoundButton = std::make_shared<AestraUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setOnClick([this]() {
        bool playing = m_audioEngine->isTestToneEnabled();
        playing = !playing;
        m_audioEngine->setTestToneEnabled(playing);
        
        m_testSoundButton->setText(playing ? "Stop Test Sound" : "Test Sound");
        
        // NOTE: No stream restore needed - test tone uses existing audio callback
    });
    addChild(m_testSoundButton);
    Log::info("[AudioSettingsPage] Test sound button created");

    // Initial Population
    // Sample-rate and buffer-size lists are static; populate them now so
    // loadSettings() below can restore the saved selections. Driver and device
    // lists require hardware enumeration, which blocks — they are filled in by
    // startAsyncDeviceLoad() (called from the constructor) off the UI thread (#256).
    const auto& currentConfig = m_audioManager->getCurrentConfig();
    m_sampleRateDropdown->addItem("44100 Hz", 44100);
    m_sampleRateDropdown->addItem("48000 Hz", 48000);
    m_sampleRateDropdown->addItem("88200 Hz", 88200);
    m_sampleRateDropdown->addItem("96000 Hz", 96000);
    m_sampleRateDropdown->setSelectedByValue((int)currentConfig.sampleRate);

    for (int s : {64, 128, 256, 512, 1024, 2048}) {
        m_bufferSizeDropdown->addItem(std::to_string(s) + " samples", s);
    }
    m_bufferSizeDropdown->setSelectedByValue((int)currentConfig.bufferSize);

    updateLatencyEstimate();

    // Load persisted settings (overrides defaults if exists)
    // NOTE: loadSettings() is called BEFORE m_isInitializing is set to false.
    // This prevents loadSettings() from triggering openStream calls during startup.
    // The audio device was already opened once by AudioEngineController.
    // Settings from disk are applied only if they differ from the active config,
    // and this is handled by the equality-check guards in AudioDeviceManager setters.
    loadSettings();

    // Done with initialization — now allow interactive changes to propagate to audio system
    m_isInitializing = false;
}



void AudioSettingsPage::applyChanges() {
    // 1. Resampling Quality
    int qIdx = m_resamplingDropdown->getSelectedIndex();
    ClipResamplingQuality globalQ = ClipResamplingQuality::High;
    Aestra::Audio::Interpolators::InterpolationQuality engineQ = Aestra::Audio::Interpolators::InterpolationQuality::Sinc16; // Default High
    
    if (qIdx == 0) { 
        globalQ = ClipResamplingQuality::Fast; 
        engineQ = Aestra::Audio::Interpolators::InterpolationQuality::Cubic;
    }
    else if (qIdx == 1) { 
        globalQ = ClipResamplingQuality::Draft;
        engineQ = Aestra::Audio::Interpolators::InterpolationQuality::Sinc32; 
    }
    else if (qIdx == 2) { 
        globalQ = ClipResamplingQuality::Standard;
        engineQ = Aestra::Audio::Interpolators::InterpolationQuality::Cubic; 
    }
    else if (qIdx == 3) { 
        globalQ = ClipResamplingQuality::High;
        engineQ = Aestra::Audio::Interpolators::InterpolationQuality::Sinc64;
    }
    
    PlaylistMixer::setResamplingQuality(globalQ);
    if (m_audioEngine) {
        m_audioEngine->setInterpolationQuality(engineQ);
    }

    // 2. Dithering
    int ditherIdx = m_ditheringDropdown->getSelectedIndex();
    if (m_audioEngine && ditherIdx >= 0) {
        DitheringMode mode = (DitheringMode)m_ditheringDropdown->getSelectedValue();
        m_audioEngine->setDitheringMode(mode);
    }
    
    // Toggles
    if (m_audioEngine) {
        // DC Removal (Mock - Engine doesn't have explicit param exposed yet, assume enabled by default or add later)
        // m_audioEngine->setDCRemovalEnabled(m_dcRemovalToggle->isOn());
        
        m_audioEngine->setSafetyLimiterEnabled(m_softClippingToggle->isOn());
    }
    
    // Multi-threading
    if (m_audioEngine) {
        bool mtEnabled = m_multiThreadingToggle->isOn();
        m_audioEngine->setMultiThreadingEnabled(mtEnabled);
        
        if (mtEnabled) {
            int threads = m_threadCountInput->getValue();
            m_audioEngine->setThreadCount(threads);
        }
    }
    
    // 3. Driver/Device changes (Mock)
    // In real impl, we'd call m_audioManager->closeStream() and open new one.
    // For now, we assume user just changes quality mostly.
    
    m_dirty = false;
    
    // Save state to disk
    saveSettings();
}

void AudioSettingsPage::cancelChanges() {
    loadCurrentSettings();
}

bool AudioSettingsPage::hasUnsavedChanges() const {
    return m_dirty;
}

void AudioSettingsPage::onShow() {
    // Refresh device lists in case hardware changed — off the UI thread (#256)
    startAsyncDeviceLoad();
}

void AudioSettingsPage::onHide() {
    // Stop test sound if playing
    if (m_audioEngine->isTestToneEnabled()) {
        m_audioEngine->setTestToneEnabled(false);
        m_testSoundButton->setText("Test Sound");
        if (m_onStreamRestore) m_onStreamRestore();
    }
}

void AudioSettingsPage::updateLatencyEstimate() {
    // Calculate and set label text based on current selection
    int srVal = m_sampleRateDropdown->getSelectedValue();
    int bufVal = m_bufferSizeDropdown->getSelectedValue();
    
    if (srVal > 0 && bufVal > 0) {
        double latMs = (double)bufVal / (double)srVal * 1000.0;
        // Add safety buffer estimate (usually 2x or 3x depending on buffering)
        // For UI estimate, simple buffer latency is fine, maybe * 2 for roundtrip visual
        std::stringstream ss;
        ss << "Est. Latency: " << (int)latMs << " ms (" << bufVal << " spl)";
        m_latencyLabel->setText(ss.str());
    } else {
        m_latencyLabel->setText("Est. Latency: -- ms");
    }
}

// Layout
void AudioSettingsPage::layoutComponents() {
    auto b = getBounds();
    float padding = 20.0f;
    float colWidth = (b.width - padding * 3) / 2.0f;
    float rowHeight = 30.0f;
    float gap = 15.0f;
    
    float x1 = b.x + padding;
    float x2 = b.x + padding * 2 + colWidth;
    float y = b.y + padding;
    
    auto layRow = [&](auto& label, auto& comp, float x) {
        label->setBounds(AestraUI::NUIRect(x, y, 100, rowHeight));
        comp->setBounds(AestraUI::NUIRect(x + 110, y, colWidth - 110, rowHeight));
    };
    
    // Left Column: Device
    layRow(m_driverLabel, m_driverDropdown, x1); y += rowHeight + gap;
    layRow(m_deviceLabel, m_deviceDropdown, x1); y += rowHeight + gap;
    layRow(m_inputDeviceLabel, m_inputDeviceDropdown, x1); y += rowHeight + gap;
    layRow(m_sampleRateLabel, m_sampleRateDropdown, x1); y += rowHeight + gap;
    layRow(m_bufferSizeLabel, m_bufferSizeDropdown, x1); y += rowHeight + gap;
    m_latencyLabel->setBounds(AestraUI::NUIRect(x1 + 110, y, colWidth - 110, 20)); y += 30;
    
    // Test Sound Button (below left column)
    m_testSoundButton->setBounds(AestraUI::NUIRect(x1, y, colWidth, 32));
    
    // Loading indicator (centered in left column)
    if (m_loadingLabel) {
        m_loadingLabel->setBounds(AestraUI::NUIRect(x1, y + 45, colWidth, 24));
    }

    // Right Column: Processing (reset Y)
    y = b.y + padding;
    layRow(m_qualityPresetLabel, m_qualityPresetDropdown, x2); y += rowHeight + gap;
    layRow(m_resamplingLabel, m_resamplingDropdown, x2); y += rowHeight + gap;
    layRow(m_ditheringLabel, m_ditheringDropdown, x2); y += rowHeight + gap;
    
    // Toggles need smaller height to look good centered in row, but bounds handled by component
    // Assuming NUIToggle sizes itself or centers content. 
    // We'll give it the row height.
    layRow(m_dcRemovalLabel, m_dcRemovalToggle, x2); y += rowHeight + gap;
    layRow(m_softClippingLabel, m_softClippingToggle, x2); y += rowHeight + gap;
    layRow(m_multiThreadingLabel, m_multiThreadingToggle, x2); y += rowHeight + gap;
    
    // Conditionally show thread count if multithreading is enabled
    if (m_multiThreadingToggle->isOn()) {
         layRow(m_threadCountLabel, m_threadCountInput, x2);
         y += rowHeight + gap; // Fix: Increment Y so subsequent elements (if any) don't overlap
    }
}

void AudioSettingsPage::onRender(AestraUI::NUIRenderer& renderer) {
    // Render all children normally
    renderChildren(renderer);
    
    // Z-Order Fix: Render open dropdown list ON TOP of everything else
    // Iterate children to find open dropdowns
    for (const auto& child : getChildren()) {
        if (!child->isVisible()) continue;
        
        if (auto dd = std::dynamic_pointer_cast<AestraUI::NUIDropdown>(child)) {
            // Because getChildren returns const shared_ptrs, and renderDropdownList is non-const,
            // we might need to cast away constness or ensure renderDropdownList is const.
            // Actually getChildren returns vector of shared_ptrs. The pointers themselves are not const to the object.
            // But iteration 'const auto& child' means child is 'const shared_ptr<NUIComponent>&'.
            // Accessing the object child-> is fine (returns NUIComponent* / NUIDropdown*).
            // dynamic_pointer_cast returns shared_ptr<NUIDropdown>.
            
            if (dd->isOpen()) {
                dd->renderDropdownList(renderer);
            }
        }
    }
}

void AudioSettingsPage::onResize(int width, int height) {
    AestraUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

void AudioSettingsPage::onUpdate(double deltaTime) {
    AestraUI::NUIComponent::onUpdate(deltaTime);
    
    // Check for async device load completion
    if (m_deviceDataReady && !m_isLoadingDevices.load()) {
        m_deviceDataReady = false; // Consume the flag
        if (m_deviceReloadPending) {
            // A refresh was requested while this load was in flight (e.g. the
            // driver changed) — the results are stale. Discard them and
            // re-enumerate; controls stay disabled with the loading indicator.
            m_deviceReloadPending = false;
            startAsyncDeviceLoad();
        } else {
            onDeviceLoadComplete();
        }
    }
    
    // Animate loading indicator
    if (m_isLoadingDevices.load() && m_loadingLabel) {
        m_loadingAnimTimer += static_cast<float>(deltaTime);
        int dots = static_cast<int>(m_loadingAnimTimer * 2.0f) % 4;
        std::string text = "Loading audio devices";
        for (int i = 0; i < dots; ++i) text += ".";
        m_loadingLabel->setText(text);
    }
}

bool AudioSettingsPage::onMouseEvent(const AestraUI::NUIMouseEvent& event) {
    return AestraUI::NUIComponent::onMouseEvent(event);
}


// ============================================================================
// Persistence
// ============================================================================

void AudioSettingsPage::saveSettings() {
    const auto configPath = getAudioSettingsConfigPath();
    std::ofstream file(configPath);
    if (file.is_open()) {
        file << "driver=" << m_driverDropdown->getSelectedValue() << "\n";
        file << "device=" << m_deviceDropdown->getSelectedValue() << "\n";
        file << "input_device=" << m_inputDeviceDropdown->getSelectedValue() << "\n";
        file << "samplerate=" << m_sampleRateDropdown->getSelectedValue() << "\n";
        file << "buffersize=" << m_bufferSizeDropdown->getSelectedValue() << "\n";
        file << "quality_preset=" << m_qualityPresetDropdown->getSelectedValue() << "\n";
        file << "resampling=" << m_resamplingDropdown->getSelectedValue() << "\n";
        file << "dithering=" << m_ditheringDropdown->getSelectedValue() << "\n";
        file << "threads=" << m_threadCountInput->getValue() << "\n";
        file << "dc_removal=" << (m_dcRemovalToggle->isOn() ? "1" : "0") << "\n";
        file << "master_limiter=" << (m_softClippingToggle->isOn() ? "1" : "0") << "\n";
        file << "multi_threading=" << (m_multiThreadingToggle->isOn() ? "1" : "0") << "\n";
        file.close();
        Log::info("[AudioSettingsPage] Settings saved to " + configPath.string());
    } else {
        Log::error("[AudioSettingsPage] Failed to save settings!");
    }
}

void AudioSettingsPage::loadSettings() {
    bool isLegacy = false;
    const auto configPath = getAudioSettingsConfigPath(&isLegacy);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        Log::info("[AudioSettingsPage] No saved settings found. Using defaults.");
        return;
    }

    Log::info("[AudioSettingsPage] Loading settings...");

    // During initialization, we skip the audio manager calls.
    // The stream was opened once by AudioEngineController.
    // AudioDeviceManager's equality checks guard against unnecessary reopens on direct calls.
    // We only need to populate the UI dropdowns to reflect saved values.
    // After init, user interactions via dropdown callbacks will handle audio changes.
    const bool skipAudioCalls = m_isInitializing;
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string valStr = line.substr(pos + 1);
        int val = 0;
        try {
            val = std::stoi(valStr);
        } catch (const std::invalid_argument&) {
            continue;
        } catch (const std::out_of_range&) {
            continue;
        }

        if (key == "driver") {
            // Apply driver to engine FIRST to ensure device listing works
            if (m_audioManager && !skipAudioCalls) {
                m_audioManager->setPreferredDriverType((AudioDriverType)val);
            }
            m_driverDropdown->setSelectedByValue(val);
        }
        else if (key == "device") {
            if (m_audioManager && !skipAudioCalls) m_audioManager->switchDevice((uint32_t)val);
            m_deviceDropdown->setSelectedByValue(val);
        }
        else if (key == "input_device") {
            if (m_audioManager && !skipAudioCalls) m_audioManager->switchInputDevice((uint32_t)val);
            m_inputDeviceDropdown->setSelectedByValue(val);
        }
        else if (key == "samplerate") {
            if (m_audioManager && !skipAudioCalls) m_audioManager->setSampleRate((uint32_t)val);
            m_sampleRateDropdown->setSelectedByValue(val);
        }
        else if (key == "buffersize") {
            if (m_audioManager && !skipAudioCalls) m_audioManager->setBufferSize((uint32_t)val);
            m_bufferSizeDropdown->setSelectedByValue(val);
        }
        else if (key == "quality_preset") {
            m_qualityPresetDropdown->setSelectedByValue(val);
        }
        else if (key == "resampling") {
            m_resamplingDropdown->setSelectedByValue(val);
        }
        else if (key == "dithering") {
            m_ditheringDropdown->setSelectedByValue(val);
        }
        else if (key == "threads") {
            m_threadCountInput->setValue(val);
        }
        else if (key == "dc_removal") {
            m_dcRemovalToggle->setOn(val == 1);
        }
        else if (key == "master_limiter") {
            m_softClippingToggle->setOn(val == 1);
            if (m_audioEngine)
                m_audioEngine->setSafetyLimiterEnabled(val == 1);
        }
        else if (key == "multi_threading") {
            m_multiThreadingToggle->setOn(val == 1);
        }
    }
    
    applyChanges();
    Log::info("[AudioSettingsPage] Settings loaded successfully.");

    if (isLegacy) {
        Log::info("[AudioSettingsPage] Loaded from legacy path, migrating to app-data.");
        saveSettings();
    }
}



bool AudioSettingsPage::isPlayingTestSound() const {
    if (m_audioEngine) return m_audioEngine->isTestToneEnabled();
    return false;
}

void AudioSettingsPage::setPlayingTestSound(bool playing) {
    if (m_audioEngine) {
        m_audioEngine->setTestToneEnabled(playing);
        if (m_testSoundButton) {
            m_testSoundButton->setText(playing ? "Stop Test Sound" : "Test Sound");
        }
    }
}

//==============================================================================
// Async Device Loading
//==============================================================================

void AudioSettingsPage::startAsyncDeviceLoad() {
    // This is the only path that may call getAvailableDriverTypes()/getDevices()
    // (blocking hardware enumeration) — keep those calls on the worker thread (#256).

    // If a load is already in flight, don't drop this request — the in-flight
    // results may be for a stale driver selection. Remember it and re-enumerate
    // once the current load finishes (consumed in onUpdate()).
    if (m_isLoadingDevices.load()) {
        m_deviceReloadPending = true;
        return;
    }
    
    m_isLoadingDevices.store(true);
    m_deviceDataReady = false;
    
    // Show loading indicator
    if (m_loadingLabel) {
        m_loadingLabel->setVisible(true);
    }
    
    // Disable dropdowns while loading
    if (m_driverDropdown) m_driverDropdown->setEnabled(false);
    if (m_deviceDropdown) m_deviceDropdown->setEnabled(false);
    if (m_inputDeviceDropdown) m_inputDeviceDropdown->setEnabled(false);
    
    Log::info("[AudioSettingsPage] Starting async device enumeration...");
    
    // Start background thread
    if (m_deviceLoadThread.joinable()) {
        m_deviceLoadThread.join();
    }
    
    m_deviceLoadThread = std::thread([this]() {
        CachedDeviceData data;

        // Backend enumeration can throw (e.g. RtAudio errors). An uncaught
        // exception in a std::thread calls std::terminate, and the page would
        // stay stuck in the loading state — catch, log, and publish fallback
        // data so the UI always recovers.
        try {
            // === Enumerate driver types ===
            auto types = m_audioManager->getAvailableDriverTypes();
            for (int i = 0; i < static_cast<int>(types.size()); ++i) {
                std::string name = "Unknown";
                if (types[i] == AudioDriverType::WASAPI_SHARED)
                    name = "WASAPI Shared";
                else if (types[i] == AudioDriverType::WASAPI_EXCLUSIVE)
                    name = "WASAPI Exclusive";
                else if (types[i] == AudioDriverType::ASIO_EXTERNAL)
                    name = "ASIO (External)";
                else if (types[i] == AudioDriverType::ASIO_Aestra)
                    name = "ASIO (Aestra)";
                else if (types[i] == AudioDriverType::DIRECTSOUND)
                    name = "DirectSound";
                else if (types[i] == AudioDriverType::RTAUDIO)
                    name = "RtAudio (Auto)";
                else if (types[i] == AudioDriverType::PULSEAUDIO)
                    name = "PulseAudio";
                else if (types[i] == AudioDriverType::ALSA)
                    name = "ALSA";
                else if (types[i] == AudioDriverType::JACK)
                    name = "JACK";

                data.driverTypes.push_back({name, static_cast<int>(types[i])});
            }

            data.currentDriverType = static_cast<int>(m_audioManager->getActiveDriverType());

            // === Enumerate devices ===
            auto devices = m_audioManager->getDevices();
            for (const auto& dev : devices) {
                if (dev.maxOutputChannels > 0) {
                    data.outputDevices.push_back({dev.name, static_cast<int>(dev.id)});
                }
                if (dev.maxInputChannels > 0) {
                    data.inputDevices.push_back({dev.name, static_cast<int>(dev.id)});
                }
            }

            auto currentConfig = m_audioManager->getCurrentConfig();
            data.currentDeviceId = static_cast<int>(currentConfig.deviceId);
            data.currentInputDeviceId = static_cast<int>(currentConfig.inputDeviceId);
            data.currentSampleRate = static_cast<int>(currentConfig.sampleRate);
        } catch (const std::exception& e) {
            Log::error(std::string("[AudioSettingsPage] Device enumeration failed: ") + e.what());
        } catch (...) {
            Log::error("[AudioSettingsPage] Device enumeration failed with unknown error");
        }

        // Fallbacks apply to both the empty-hardware and the failure case
        if (data.driverTypes.empty()) {
            data.driverTypes.push_back({"DirectSound", static_cast<int>(AudioDriverType::DIRECTSOUND)});
        }
        if (data.outputDevices.empty()) {
            data.outputDevices.push_back({ "No Output Devices Found", -1 });
        }
        if (data.inputDevices.empty()) {
            data.inputDevices.push_back({ "No Input Devices Found", -1 });
        }

        // Store results thread-safely
        {
            std::lock_guard<std::mutex> lock(m_deviceDataMutex);
            m_cachedDevices = std::move(data);
            m_deviceDataReady = true;
        }
        
        m_isLoadingDevices.store(false);
        Log::info("[AudioSettingsPage] Async device enumeration complete!");
    });
}

void AudioSettingsPage::onDeviceLoadComplete() {
    // Apply cached data to UI (called from main thread)
    std::lock_guard<std::mutex> lock(m_deviceDataMutex);

    // Guard: during UI population, dropdown selection changes must NOT
    // call AudioDeviceManager setters. The stream is already open — UI
    // hydration must not mutate audio device state.
    m_isPopulatingDeviceUI = true;

    // Populate driver dropdown
    m_driverDropdown->clearItems();
    for (const auto& [name, value] : m_cachedDevices.driverTypes) {
        m_driverDropdown->addItem(name, value);
    }
    m_driverDropdown->setSelectedByValue(m_cachedDevices.currentDriverType);
    
    // Populate device dropdown
    m_deviceDropdown->clearItems();
    for (const auto& [name, id] : m_cachedDevices.outputDevices) {
        m_deviceDropdown->addItem(name, id);
    }
    m_deviceDropdown->setSelectedByValue(m_cachedDevices.currentDeviceId);

    m_inputDeviceDropdown->clearItems();
    for (const auto& [name, id] : m_cachedDevices.inputDevices) {
        m_inputDeviceDropdown->addItem(name, id);
    }
    m_inputDeviceDropdown->setSelectedByValue(m_cachedDevices.currentInputDeviceId);
    
    // Populate sample rates
    if (m_sampleRateDropdown->getItemCount() == 0) {
        m_sampleRateDropdown->addItem("44100 Hz", 44100);
        m_sampleRateDropdown->addItem("48000 Hz", 48000);
        m_sampleRateDropdown->addItem("88200 Hz", 88200);
        m_sampleRateDropdown->addItem("96000 Hz", 96000);
        m_sampleRateDropdown->setSelectedByValue(m_cachedDevices.currentSampleRate);
    }
    
    // Populate buffer sizes
    if (m_bufferSizeDropdown->getItemCount() == 0) {
        m_bufferSizeDropdown->addItem("64 samples", 64);
        m_bufferSizeDropdown->addItem("128 samples", 128);
        m_bufferSizeDropdown->addItem("256 samples", 256);
        m_bufferSizeDropdown->addItem("512 samples", 512);
        m_bufferSizeDropdown->addItem("1024 samples", 1024);
        m_bufferSizeDropdown->addItem("2048 samples", 2048);
        m_bufferSizeDropdown->setSelectedByValue(256);
    }
    
    // Hide loading indicator
    if (m_loadingLabel) {
        m_loadingLabel->setVisible(false);
    }
    
    // Re-enable dropdowns
    if (m_driverDropdown) m_driverDropdown->setEnabled(true);
    if (m_deviceDropdown) m_deviceDropdown->setEnabled(true);
    if (m_inputDeviceDropdown) m_inputDeviceDropdown->setEnabled(true);
    
    updateLatencyEstimate();

    m_isPopulatingDeviceUI = false;

    Log::info("[AudioSettingsPage] UI populated with device data");
}

} // namespace Aestra
