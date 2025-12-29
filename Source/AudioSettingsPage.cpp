#include "AudioSettingsPage.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadCore/include/NomadLog.h"
#include "../NomadAudio/include/PlaylistMixer.h"
#include "../NomadAudio/include/ClipResampler.h"
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <iomanip>

namespace Nomad {

using namespace Nomad::Audio;

// ============================================================================
// ThreadCountDisplay Implementation
// ============================================================================

ThreadCountDisplay::ThreadCountDisplay() 
    : m_value(1), m_min(1), m_max(64), m_dragStartValue(0)
{
    const char* upArrowSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 14l5-5 5 5z"/></svg>)";
    m_upArrow = std::make_shared<NomadUI::NUIIcon>(upArrowSvg);
    m_upArrow->setIconSize(NomadUI::NUIIconSize::Small);
    
    const char* downArrowSvg = R"(<svg viewBox="0 0 24 24" fill="currentColor"><path d="M7 10l5 5 5-5z"/></svg>)";
    m_downArrow = std::make_shared<NomadUI::NUIIcon>(downArrowSvg);
    m_downArrow->setIconSize(NomadUI::NUIIconSize::Small);
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

NomadUI::NUIRect ThreadCountDisplay::getUpArrowBounds() const {
    NomadUI::NUIRect bounds = getBounds();
    return NomadUI::NUIRect(bounds.x + bounds.width - 24, bounds.y + 2, 20, 12);
}

NomadUI::NUIRect ThreadCountDisplay::getDownArrowBounds() const {
    NomadUI::NUIRect bounds = getBounds();
    return NomadUI::NUIRect(bounds.x + bounds.width - 24, bounds.y + 16, 20, 12);
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

void ThreadCountDisplay::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const float radius = themeManager.getRadius("m");
    
    // Background (Dark Pill)
    NomadUI::NUIColor bgColor = themeManager.getColor("surfaceTertiary").withAlpha(0.5f);
    renderer.fillRoundedRect(bounds, radius, bgColor);
    
    // Border
    NomadUI::NUIColor borderColor = m_isHovered || m_isDragging ? themeManager.getColor("accentPrimary") : themeManager.getColor("glassBorder");
    renderer.strokeRoundedRect(bounds, radius, 1.0f, borderColor);
    
    // Text
    std::string text = std::to_string(m_value) + " Threads";
    float fontSize = themeManager.getFontSize("m");
    NomadUI::NUIColor textColor = themeManager.getColor("textPrimary");
    
    NomadUI::NUISize textSize = renderer.measureText(text, fontSize);
    float textX = bounds.x + 10;
    float textY = std::round(renderer.calculateTextY(bounds, fontSize));
    renderer.drawText(text, NomadUI::NUIPoint(textX, textY), fontSize, textColor);
    
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

bool ThreadCountDisplay::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    
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
    if (event.pressed && event.button == NomadUI::NUIMouseButton::Left) {
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
    , m_isPlayingTestSound(false)
    , m_testSoundPhase(0.0)
    , m_dirty(false)
{
    Log::info("[AudioSettingsPage] Constructor started (with button init)");
    createUI(); // Create UI elements FIRST
    loadCurrentSettings(); // Load initial state
    
    m_testSoundButton = std::make_shared<NomadUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setOnClick([this]() {
        m_isPlayingTestSound = !m_isPlayingTestSound;
        m_testSoundButton->setText(m_isPlayingTestSound ? "Stop Test Sound" : "Test Sound");
        m_testSoundPhase = 0.0;
        
        // If stopping, restore stream logic
        if (!m_isPlayingTestSound) {
            // Restore stream
            if (m_onStreamRestore) m_onStreamRestore();
        }
    });
    addChild(m_testSoundButton);
    
    // Initial Population
    // Trigger update logic ONCE without recursion loop
    updateDriverList();
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
        auto l = std::make_shared<NomadUI::NUILabel>();
        l->setText(text);
        addChild(l);
        return l;
    };
    
    m_driverLabel = createLabel("Audio Driver:");
    m_deviceLabel = createLabel("Device:");
    m_sampleRateLabel = createLabel("Sample Rate:");
    m_bufferSizeLabel = createLabel("Buffer Size:");
    m_latencyLabel = createLabel("Est. Latency: -- ms");
    m_qualityPresetLabel = createLabel("Quality Preset:");
    m_resamplingLabel = createLabel("Resampling:");
    m_ditheringLabel = createLabel("Dithering:");
    m_dcRemovalLabel = createLabel("DC Removal:");
    m_softClippingLabel = createLabel("Soft Clipping:");
    m_multiThreadingLabel = createLabel("Multi-threading:");
    m_threadCountLabel = createLabel("Thread Count:");
    
    // Dropdowns
    auto createDropdown = [&](std::function<void(int)> onChange) {
        auto d = std::make_shared<NomadUI::NUIDropdown>();
        d->setOnSelectionChanged([onChange](int index, int, const std::string&) {
            onChange(index);
        });
        addChild(d);
        return d;
    };

    m_driverDropdown = createDropdown([this](int idx) { 
        m_dirty = true;
        
        // SWITCH DRIVER IMMEDIATELY so device list is correct
        if (m_audioManager) {
             m_audioManager->setPreferredDriverType((AudioDriverType)m_driverDropdown->getSelectedValue());
        }
        
        // When driver changes, we need to update device list (and potentially switch backend)
        updateDeviceList();
    });
    
    m_deviceDropdown = createDropdown([this](int idx) { 
        m_dirty = true; 
        // SWITCH DEVICE IMMEDIATELY
        if (m_audioManager) {
            m_audioManager->switchDevice((uint32_t)m_deviceDropdown->getSelectedValue());
        }
    });
    
    m_sampleRateDropdown = createDropdown([this](int idx) { 
        m_dirty = true; 
        if (m_audioManager) {
            m_audioManager->setSampleRate((uint32_t)m_sampleRateDropdown->getSelectedValue());
        }
        updateLatencyEstimate();
    });
    
    m_bufferSizeDropdown = createDropdown([this](int idx) { 
        m_dirty = true;
        if (m_audioManager) {
             m_audioManager->setBufferSize((uint32_t)m_bufferSizeDropdown->getSelectedValue());
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
        auto t = std::make_shared<NomadUI::NUIToggle>();
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

    m_testSoundButton = std::make_shared<NomadUI::NUIButton>();
    m_testSoundButton->setText("Test Sound");
    m_testSoundButton->setOnClick([this]() {
        m_isPlayingTestSound = !m_isPlayingTestSound;
        m_testSoundButton->setText(m_isPlayingTestSound ? "Stop Test Sound" : "Test Sound");
        m_testSoundPhase = 0.0;
        
        // If stopping, restore stream logic
        if (!m_isPlayingTestSound) {
            // Restore stream
            if (m_onStreamRestore) m_onStreamRestore();
        }
    });
    addChild(m_testSoundButton);
    Log::info("[AudioSettingsPage] Test sound button created");
    
    // Initial Population
    // Trigger update logic ONCE without recursion loop
    Log::info("[AudioSettingsPage] createUI components created. Updating driver list...");
    updateDriverList();
    Log::info("[AudioSettingsPage] Initial updateDriverList complete.");
    
    // Load persisted settings (overrides defaults if exists)
    loadSettings();
}



void AudioSettingsPage::applyChanges() {
    // 1. Resampling Quality
    int qIdx = m_resamplingDropdown->getSelectedIndex();
    ClipResamplingQuality globalQ = ClipResamplingQuality::High;
    Nomad::Audio::Interpolators::InterpolationQuality engineQ = Nomad::Audio::Interpolators::InterpolationQuality::Sinc16; // Default High
    
    if (qIdx == 0) { 
        globalQ = ClipResamplingQuality::Fast; 
        engineQ = Nomad::Audio::Interpolators::InterpolationQuality::Cubic;
    }
    else if (qIdx == 1) { 
        globalQ = ClipResamplingQuality::Draft;
        engineQ = Nomad::Audio::Interpolators::InterpolationQuality::Sinc32; 
    }
    else if (qIdx == 2) { 
        globalQ = ClipResamplingQuality::Standard;
        engineQ = Nomad::Audio::Interpolators::InterpolationQuality::Cubic; 
    }
    else if (qIdx == 3) { 
        globalQ = ClipResamplingQuality::High;
        engineQ = Nomad::Audio::Interpolators::InterpolationQuality::Sinc64;
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
        
        m_audioEngine->setSafetyProcessingEnabled(m_softClippingToggle->isOn());
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
    // Refresh device lists in case hardware changed
    updateDriverList();
}

void AudioSettingsPage::onHide() {
    // Stop test sound if playing
    if (m_isPlayingTestSound) {
        m_isPlayingTestSound = false;
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

void AudioSettingsPage::updateDriverList() {
    m_driverDropdown->clearItems();
    
    // In a real scenario with multiple backends, we'd list them.
    // Nomads AudioDeviceManager might handle WASAPI/ASIO as "Driver Types"
    // For now, let's query available driver types
    auto types = m_audioManager->getAvailableDriverTypes();
    
    // Map types to UI
    for (int i = 0; i < types.size(); ++i) {
        std::string name = "Unknown";
        if (types[i] == AudioDriverType::WASAPI_SHARED) name = "WASAPI Shared";
        else if (types[i] == AudioDriverType::WASAPI_EXCLUSIVE) name = "WASAPI Exclusive";
        else if (types[i] == AudioDriverType::ASIO_EXTERNAL) name = "ASIO (External)";
        else if (types[i] == AudioDriverType::ASIO_NOMAD) name = "ASIO (Nomad)";
        else if (types[i] == AudioDriverType::DIRECTSOUND) name = "DirectSound";
        
        m_driverDropdown->addItem(name, (int)types[i]);
    }

    if (types.empty()) {
        // Fallback
        m_driverDropdown->addItem("DirectSound", (int)AudioDriverType::DIRECTSOUND);
    }
    
    // Select current driver type
    auto currentType = m_audioManager->getActiveDriverType();
    m_driverDropdown->setSelectedByValue((int)currentType);
    
    // Update dependent lists
    updateDeviceList();
}

void AudioSettingsPage::updateDeviceList() {
    m_deviceDropdown->clearItems();
    
    auto devices = m_audioManager->getDevices();
    for (const auto& dev : devices) {
        m_deviceDropdown->addItem(dev.name, (int)dev.id);
    }
    
    if (devices.empty()) {
        m_deviceDropdown->addItem("No Devices Found", -1);
    }
    
    // Select active device
    auto currentConfig = m_audioManager->getCurrentConfig();
    m_deviceDropdown->setSelectedByValue((int)currentConfig.deviceId);
    
    // Update Standard Rates/Buffers if empty
    if (m_sampleRateDropdown->getItemCount() == 0) {
        m_sampleRateDropdown->addItem("44100 Hz", 44100);
        m_sampleRateDropdown->addItem("48000 Hz", 48000);
        m_sampleRateDropdown->addItem("88200 Hz", 88200);
        m_sampleRateDropdown->addItem("96000 Hz", 96000);
        m_sampleRateDropdown->setSelectedByValue((int)currentConfig.sampleRate);
    }
    
    if (m_bufferSizeDropdown->getItemCount() == 0) {
        std::vector<int> sizes = {64, 128, 256, 512, 1024, 2048};
        for (int s : sizes) m_bufferSizeDropdown->addItem(std::to_string(s) + " samples", s);
        m_bufferSizeDropdown->setSelectedByValue((int)currentConfig.bufferSize);
    }
    
    updateLatencyEstimate();
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
        label->setBounds(NomadUI::NUIRect(x, y, 100, rowHeight));
        comp->setBounds(NomadUI::NUIRect(x + 110, y, colWidth - 110, rowHeight));
    };
    
    // Left Column: Device
    layRow(m_driverLabel, m_driverDropdown, x1); y += rowHeight + gap;
    layRow(m_deviceLabel, m_deviceDropdown, x1); y += rowHeight + gap;
    layRow(m_sampleRateLabel, m_sampleRateDropdown, x1); y += rowHeight + gap;
    layRow(m_bufferSizeLabel, m_bufferSizeDropdown, x1); y += rowHeight + gap;
    m_latencyLabel->setBounds(NomadUI::NUIRect(x1 + 110, y, colWidth - 110, 20)); y += 30;
    
    // Test Sound Button (below left column)
    m_testSoundButton->setBounds(NomadUI::NUIRect(x1, y, colWidth, 32));

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

void AudioSettingsPage::onRender(NomadUI::NUIRenderer& renderer) {
    // Render all children normally
    renderChildren(renderer);
    
    // Z-Order Fix: Render open dropdown list ON TOP of everything else
    // Iterate children to find open dropdowns
    for (const auto& child : getChildren()) {
        if (!child->isVisible()) continue;
        
        if (auto dd = std::dynamic_pointer_cast<NomadUI::NUIDropdown>(child)) {
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
    NomadUI::NUIComponent::onResize(width, height);
    layoutComponents();
}

void AudioSettingsPage::onUpdate(double deltaTime) {
    NomadUI::NUIComponent::onUpdate(deltaTime);
}

bool AudioSettingsPage::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    return NomadUI::NUIComponent::onMouseEvent(event);
}


// ============================================================================
// Persistence
// ============================================================================

void AudioSettingsPage::saveSettings() {
    std::ofstream file("audio_settings.conf");
    if (file.is_open()) {
        file << "driver=" << m_driverDropdown->getSelectedValue() << "\n";
        file << "device=" << m_deviceDropdown->getSelectedValue() << "\n";
        file << "samplerate=" << m_sampleRateDropdown->getSelectedValue() << "\n";
        file << "buffersize=" << m_bufferSizeDropdown->getSelectedValue() << "\n";
        file << "quality_preset=" << m_qualityPresetDropdown->getSelectedValue() << "\n";
        file << "resampling=" << m_resamplingDropdown->getSelectedValue() << "\n";
        file << "dithering=" << m_ditheringDropdown->getSelectedValue() << "\n";
        file << "threads=" << m_threadCountInput->getValue() << "\n";
        file << "dc_removal=" << (m_dcRemovalToggle->isOn() ? "1" : "0") << "\n";
        file << "soft_clipping=" << (m_softClippingToggle->isOn() ? "1" : "0") << "\n";
        file << "multi_threading=" << (m_multiThreadingToggle->isOn() ? "1" : "0") << "\n";
        file.close();
        Log::info("[AudioSettingsPage] Settings saved to audio_settings.conf");
    } else {
        Log::error("[AudioSettingsPage] Failed to save settings!");
    }
}

void AudioSettingsPage::loadSettings() {
    std::ifstream file("audio_settings.conf");
    if (!file.is_open()) {
        Log::info("[AudioSettingsPage] No saved settings found. Using defaults.");
        return;
    }

    Log::info("[AudioSettingsPage] Loading settings...");
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = line.substr(0, pos);
        std::string valStr = line.substr(pos + 1);
        int val = 0;
        try {
            val = std::stoi(valStr);
        } catch (...) { continue; }

        if (key == "driver") {
            // Apply driver to engine FIRST to ensure device listing works
            if (m_audioManager) {
                m_audioManager->setPreferredDriverType((AudioDriverType)val);
                // Also trigger updateDeviceList manually just in case
            }
            m_driverDropdown->setSelectedByValue(val);
        }
        else if (key == "device") {
            if (m_audioManager) m_audioManager->switchDevice((uint32_t)val);
            m_deviceDropdown->setSelectedByValue(val);
        }
        else if (key == "samplerate") {
            if (m_audioManager) m_audioManager->setSampleRate((uint32_t)val);
            m_sampleRateDropdown->setSelectedByValue(val);
        }
        else if (key == "buffersize") {
            if (m_audioManager) m_audioManager->setBufferSize((uint32_t)val);
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
        else if (key == "soft_clipping") {
            m_softClippingToggle->setOn(val == 1);
        }
        else if (key == "multi_threading") {
            m_multiThreadingToggle->setOn(val == 1);
        }
    }
    
    applyChanges();
    Log::info("[AudioSettingsPage] Settings loaded successfully.");
}

} // namespace Nomad
