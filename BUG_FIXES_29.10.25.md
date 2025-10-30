# NOMAD DAW – Bug Fixes Implementation Guide (29.10.25)

**Version:** v1.0  
**Platform:** Windows  
**Mode:** Development  
**FPS Mode:** Adaptive (Target 30 FPS)  
**Build:** Stable Debug

---

## 📋 Bug Summary

This document provides implementation details for all 11 reported bugs. Each section includes:
- Root cause analysis
- Code locations
- Implementation steps
- Testing criteria

---

## 🐛 Bug #1: Timer Loop Reset Failure

### **Issue**
Timer does not reset to `0.0.0` when looping samples (sample audio loops actually, just UI), resulting in the playhead failing to reset visually and functionally.

### **Root Cause**
The `TransportBar` and `TrackManager` don't implement loop boundary detection. When audio playback reaches the end of a sample with looping enabled, there's no logic to reset the position back to 0.

### **Files to Modify**
1. `NomadAudio/include/TrackManager.h` - Add loop support
2. `NomadAudio/src/TrackManager.cpp` - Implement loop logic
3. `Source/TransportBar.h` - Add loop state management
4. `Source/TransportBar.cpp` - Handle loop UI feedback

### **Implementation Steps**

#### Step 1: Add Loop Detection in TrackManager
```cpp
// In NomadAudio/src/TrackManager.cpp - processAudio() method

void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    // ... existing code ...
    
    // After processing, check for loop boundaries
    if (m_isPlaying.load()) {
        double currentPos = m_positionSeconds.load();
        double maxDuration = getTotalDuration();
        
        // Check if we've exceeded the duration
        if (currentPos >= maxDuration && maxDuration > 0.0) {
            // Loop back to start
            m_positionSeconds.store(0.0);
            
            // Reset all tracks to start position
            for (auto& track : m_tracks) {
                if (!track->isSystemTrack()) {
                    track->setPosition(0.0);
                }
            }
            
            // Notify UI callback if set
            if (m_onPositionUpdate) {
                m_onPositionUpdate(0.0);
            }
        }
    }
}
```

#### Step 2: Sync Timer Display on Loop
```cpp
// In Source/Main.cpp - run() method, update sync logic

// Sync transport position with track manager during playback
if (m_content && m_content->getTransportBar() && m_content->getTrackManagerUI()) {
    auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
    if (trackManager && trackManager->isPlaying()) {
        double currentPosition = trackManager->getPosition();
        m_content->getTransportBar()->setPosition(currentPosition);
        
        // CRITICAL: If position jumped backward (loop occurred), force UI update
        static double lastPosition = 0.0;
        if (currentPosition < lastPosition - 0.1) { // Threshold to detect loop
            m_content->getTransportBar()->setPosition(0.0); // Force reset
        }
        lastPosition = currentPosition;
    }
}
```

### **Testing Criteria**
- [ ] Load a sample into a track
- [ ] Enable loop mode (if available) or play sample repeatedly
- [ ] Verify timer resets to `00:00.00` when playback loops
- [ ] Verify playhead visual resets to left edge
- [ ] Verify audio continues seamlessly

---

## 🐛 Bug #2: VU Meter Inactive

### **Issue**
VU meter currently not linked to the active audio output bus.

### **Root Cause**
The `AudioVisualizer` component (CompactMeter mode) is created but never receives audio data from the `TrackManager`'s master output buffer.

### **Files to Modify**
1. `Source/Main.cpp` - Wire up VU meter to audio callback
2. `NomadAudio/src/TrackManager.cpp` - Expose master output buffer

### **Implementation Steps**

#### Step 1: Add Master Output Monitoring
```cpp
// In NomadAudio/include/TrackManager.h

class TrackManager {
public:
    // ... existing methods ...
    
    // Add callback for master output monitoring (for VU meters, etc.)
    void setOnMasterOutput(std::function<void(const float*, uint32_t)> callback) {
        m_onMasterOutput = callback;
    }

private:
    std::function<void(const float*, uint32_t)> m_onMasterOutput;
};
```

```cpp
// In NomadAudio/src/TrackManager.cpp - processAudio()

void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    // ... existing mixing code ...
    
    // After mixing is complete, notify master output callback
    if (m_onMasterOutput && m_isPlaying.load()) {
        m_onMasterOutput(outputBuffer, numFrames);
    }
}
```

#### Step 2: Connect VU Meter in Main
```cpp
// In Source/Main.cpp - initialize() method, after creating audio visualizer

// Wire up VU meter to master output
if (m_content && m_content->getTrackManagerUI()) {
    auto trackManager = m_content->getTrackManagerUI()->getTrackManager();
    auto audioViz = m_content->getAudioVisualizer();
    
    if (trackManager && audioViz) {
        trackManager->setOnMasterOutput([audioViz](const float* buffer, uint32_t numFrames) {
            // Assuming stereo output (2 channels)
            // Extract left and right channels
            std::vector<float> leftChannel;
            std::vector<float> rightChannel;
            leftChannel.reserve(numFrames);
            rightChannel.reserve(numFrames);
            
            for (uint32_t i = 0; i < numFrames; ++i) {
                leftChannel.push_back(buffer[i * 2]);      // Left channel
                rightChannel.push_back(buffer[i * 2 + 1]); // Right channel
            }
            
            // Update visualizer with stereo data
            audioViz->setAudioData(leftChannel.data(), rightChannel.data(), numFrames, 48000.0);
        });
    }
}
```

### **Testing Criteria**
- [ ] Play audio through any track
- [ ] Verify VU meter shows left/right channel levels
- [ ] Verify VU meter responds to volume changes
- [ ] Verify RMS/peak values are accurate
- [ ] Verify VU meter updates smoothly during playback

---

## 🐛 Bug #3: FPS Drop in Settings Panel

### **Issue**
Opening the settings panel causes FPS drop from 24–29 → 12–17.

### **Root Cause**
`AudioSettingsDialog` renders all dropdowns, error messages, and background overlay every frame without dirty checking or caching. The semi-transparent overlay and multiple dropdown rendering paths are expensive.

### **Files to Modify**
1. `Source/AudioSettingsDialog.cpp` - Optimize rendering

### **Implementation Steps**

#### Step 1: Add Dirty Flag System
```cpp
// In Source/AudioSettingsDialog.h

class AudioSettingsDialog : public NomadUI::NUIComponent {
private:
    bool m_needsFullRedraw = true; // Dirty flag
};
```

```cpp
// In Source/AudioSettingsDialog.cpp - onRender()

void AudioSettingsDialog::onRender(NomadUI::NUIRenderer& renderer) {
    if (!m_visible) return;
    
    // Only render background and dialog frame if dirty
    if (m_needsFullRedraw) {
        renderBackground(renderer);
        renderDialog(renderer);
        m_needsFullRedraw = false; // Clear flag after render
    }
    
    // Render all children (buttons, dropdowns)
    NomadUI::NUIComponent::onRender(renderer);
    
    // ... existing code ...
}

// Add method to mark dirty when settings change
void AudioSettingsDialog::markDirty() {
    m_needsFullRedraw = true;
}
```

#### Step 2: Cache Dropdown State
```cpp
// Call markDirty() whenever dialog state changes:

void AudioSettingsDialog::show() {
    m_visible = true;
    setVisible(true);
    loadCurrentSettings();
    layoutComponents();
    markDirty(); // Force redraw on show
}

void AudioSettingsDialog::updateDeviceList() {
    // ... existing code ...
    markDirty(); // Redraw when device list changes
}
```

#### Step 3: Reduce Transparency Overhead
```cpp
// In renderBackground() - reduce alpha for less expensive blending

void AudioSettingsDialog::renderBackground(NomadUI::NUIRenderer& renderer) {
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor overlayColor = themeManager.getColor("backgroundPrimary");
    overlayColor = overlayColor.withAlpha(0.6f); // Reduced from 0.8f
    
    NomadUI::NUIRect overlay(0, 0, 2000, 2000);
    renderer.fillRect(overlay, overlayColor);
}
```

### **Testing Criteria**
- [ ] Open settings panel
- [ ] Verify FPS stays above 24 FPS
- [ ] Change driver/device settings
- [ ] Verify UI remains responsive
- [ ] Close and reopen panel multiple times

---

## 🐛 Bug #4: Debug Area Title Alignment & Keybinding

### **Issue**
- Title text in debug overlay misaligned (too high)
- Debug toggle uses 'F' + 'L' instead of F12 only
- Need lightweight mode (stats-only)

### **Files to Modify**
1. `Source/FPSDisplay.h` - Fix text alignment
2. `Source/Main.cpp` - Change keybinding to F12

### **Implementation Steps**

#### Step 1: Fix Title Alignment
```cpp
// In Source/FPSDisplay.h - onRender() method

void onRender(NomadUI::NUIRenderer& renderer) override {
    if (!m_visible || !m_adaptiveFPS) return;

    auto stats = m_adaptiveFPS->getStats();
    auto bounds = getBounds();

    // Background panel
    NomadUI::NUIColor bgColor(0.0f, 0.0f, 0.0f, 0.75f);
    renderer.fillRect(bounds, bgColor);
    
    NomadUI::NUIColor borderColor(0.3f, 0.3f, 0.3f, 0.9f);
    renderer.strokeRect(bounds, 1.0f, borderColor);

    float textX = bounds.x + 10;
    float textY = bounds.y + 10;
    float lineHeight = 18.0f;

    // Title with proper baseline alignment
    NomadUI::NUIColor titleColor(0.4f, 0.8f, 1.0f, 1.0f);
    float titleFontSize = 14.0f;
    float titleY = textY + titleFontSize * 0.75f; // FIXED: Add baseline offset
    renderer.drawText("ADAPTIVE FPS MONITOR", NomadUI::NUIPoint(textX, titleY), titleFontSize, titleColor);
    textY += lineHeight + 5;
    
    // ... rest of rendering code ...
}
```

#### Step 2: Change Keybinding to F12
```cpp
// In Source/Main.cpp - setupCallbacks() keyboard handler

// Remove old 'F' + 'L' logic
// Replace with:

if (key == static_cast<int>(KeyCode::F12) && pressed) {
    // F12 to toggle FPS display overlay
    if (m_fpsDisplay) {
        m_fpsDisplay->toggle();
        std::stringstream ss;
        ss << "FPS Display: " << (m_fpsDisplay->isVisible() ? "ON" : "OFF");
        Log::info(ss.str());
        std::cout << ss.str() << std::endl;
    }
}
```

#### Step 3: Add Lightweight Mode
```cpp
// In Source/FPSDisplay.h

class FPSDisplay : public NomadUI::NUIComponent {
public:
    enum class DisplayMode {
        Full,       // All stats
        Lightweight // FPS only
    };
    
    void setDisplayMode(DisplayMode mode) { m_displayMode = mode; }
    
private:
    DisplayMode m_displayMode = DisplayMode::Full;
};

// In onRender(), add conditional rendering:
void onRender(NomadUI::NUIRenderer& renderer) override {
    // ... existing code ...
    
    if (m_displayMode == DisplayMode::Lightweight) {
        // Only show FPS
        std::stringstream ss;
        ss << "FPS: " << std::fixed << std::setprecision(1) << stats.currentFPS;
        renderer.drawText(ss.str(), NomadUI::NUIPoint(textX, textY), 14.0f, valueColor);
    } else {
        // Show all stats (existing code)
    }
}
```

### **Testing Criteria**
- [ ] Press F12 to toggle debug overlay
- [ ] Verify title text is vertically centered with other text
- [ ] Verify 'F' + 'L' no longer toggles debug overlay
- [ ] Toggle lightweight mode and verify only FPS shows

---

## 🐛 Bug #5: Settings Panel Error Message Overlap

### **Issue**
Error messages in settings overlap audio settings section.

### **Root Cause**
The error message is rendered at a fixed Y position (`titleY + 50`) without adjusting the layout of components below it.

### **Files to Modify**
1. `Source/AudioSettingsDialog.cpp` - Adjust layout dynamically

### **Implementation Steps**

#### Step 1: Add Dynamic Layout Adjustment
```cpp
// In Source/AudioSettingsDialog.cpp - layoutComponents()

void AudioSettingsDialog::layoutComponents() {
    if (!m_visible) return;
    
    float padding = 24.0f;
    float labelWidth = 120.0f;
    float dropdownWidth = 320.0f;
    float dropdownHeight = 36.0f;
    float buttonWidth = 110.0f;
    float buttonHeight = 38.0f;
    float buttonSpacing = 12.0f;
    float verticalSpacing = 20.0f;
    float sectionSpacing = 28.0f;
    
    // Calculate error message height
    float errorHeight = 0.0f;
    if (!m_errorMessage.empty() && m_errorMessageAlpha > 0.0f) {
        errorHeight = 20.0f; // Reserve space for error message
    }
    
    // Start position for components (below title bar + error space)
    float startY = m_dialogBounds.y + 70.0f + errorHeight; // DYNAMIC
    float labelX = m_dialogBounds.x + padding;
    float dropdownX = labelX + labelWidth + 16.0f;
    
    // Driver selector
    m_driverLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_driverDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    startY += dropdownHeight + verticalSpacing;
    
    // Device selector
    m_deviceLabel->setBounds(NomadUI::NUIRect(labelX, startY, labelWidth, dropdownHeight));
    m_deviceDropdown->setBounds(NomadUI::NUIRect(dropdownX, startY, dropdownWidth, dropdownHeight));
    startY += dropdownHeight + sectionSpacing;
    
    // ... rest of layout code ...
}
```

#### Step 2: Trigger Relayout on Error
```cpp
// Call layoutComponents() whenever error message changes

void AudioSettingsDialog::applySettings() {
    // ... existing code ...
    
    if (errorOccurred) {
        m_errorMessage = "Failed to apply settings";
        m_errorMessageAlpha = 1.0f;
        layoutComponents(); // REFLOW LAYOUT
    }
}
```

### **Testing Criteria**
- [ ] Open settings panel
- [ ] Trigger an error (e.g., select invalid device)
- [ ] Verify error message appears in reserved space
- [ ] Verify audio settings dropdowns shift down
- [ ] Verify no overlap occurs

---

## 🐛 Bug #6: Sample Culling Behavior

### **Issue**
Right edge of sample waveform is culled on-screen. Desired: culling off-screen for infinite-scroll continuity.

### **Root Cause**
The current culling padding (`200px`) is insufficient for certain zoom levels or scroll speeds.

### **Files to Modify**
1. `Source/TrackUIComponent.cpp` - Increase culling padding

### **Implementation Steps**

#### Step 1: Increase Culling Padding
```cpp
// In Source/TrackUIComponent.cpp - onRender()

// CRITICAL: Add generous padding for off-screen culling
float cullPaddingLeft = 400.0f;   // Increased from 200px
float cullPaddingRight = 400.0f;  // Increased from 200px

// Check if waveform intersects with visible area
if (waveformStartX + waveformWidthInPixels > gridStartX - cullPaddingLeft && 
    waveformStartX < gridEndX + cullPaddingRight) {
    // ... render waveform ...
}
```

### **Testing Criteria**
- [ ] Load sample into track
- [ ] Scroll timeline horizontally
- [ ] Verify waveform edge is never culled while visible
- [ ] Verify smooth scrolling without pop-in

---

## 🐛 Bug #7: Grid Area Mouse Scrolling

### **Issue**
Mouse wheel does not scroll vertically through tracks in the grid.

### **Root Cause**
`TrackManagerUI::onMouseEvent()` only handles zoom (mouse wheel on ruler) but not vertical scrolling.

### **Files to Modify**
1. `Source/TrackManagerUI.cpp` - Add vertical scroll handler

### **Implementation Steps**

#### Step 1: Add Vertical Scroll Logic
```cpp
// In Source/TrackManagerUI.cpp - onMouseEvent()

bool TrackManagerUI::onMouseEvent(const NomadUI::NUIMouseEvent& event) {
    NomadUI::NUIRect bounds = getBounds();
    NomadUI::NUIPoint localPos(event.position.x - bounds.x, event.position.y - bounds.y);
    
    float headerHeight = 30.0f;
    float rulerHeight = 20.0f;
    float horizontalScrollbarHeight = 15.0f;
    NomadUI::NUIRect rulerRect(0, headerHeight + horizontalScrollbarHeight, bounds.width, rulerHeight);
    
    // Mouse wheel zoom on ruler (existing code)
    if (event.wheelDelta != 0.0f && rulerRect.contains(localPos)) {
        // ... existing zoom code ...
        return true;
    }
    
    // NEW: Mouse wheel vertical scroll on track grid area
    float trackAreaY = headerHeight + horizontalScrollbarHeight + rulerHeight;
    NomadUI::NUIRect trackAreaRect(0, trackAreaY, bounds.width, bounds.height - trackAreaY);
    
    if (event.wheelDelta != 0.0f && trackAreaRect.contains(localPos)) {
        // Vertical scrolling
        float scrollSpeed = 60.0f; // pixels per wheel notch
        float scrollDelta = -event.wheelDelta * scrollSpeed;
        
        m_scrollOffset += scrollDelta;
        
        // Clamp scroll offset
        float maxScroll = std::max(0.0f, m_trackUIComponents.size() * (m_trackHeight + m_trackSpacing) - trackAreaRect.height);
        m_scrollOffset = std::max(0.0f, std::min(m_scrollOffset, maxScroll));
        
        // Update scrollbar
        if (m_scrollbar) {
            m_scrollbar->setCurrentRange(m_scrollOffset, trackAreaRect.height);
        }
        
        layoutTracks(); // Re-layout tracks
        return true;
    }
    
    // ... rest of existing code ...
}
```

### **Testing Criteria**
- [ ] Add multiple tracks (>5)
- [ ] Scroll mouse wheel in track grid area
- [ ] Verify tracks scroll vertically
- [ ] Verify scrollbar updates
- [ ] Verify scroll bounds are respected

---

## 🐛 Bug #8: Mute/Solo Visual Feedback

### **Issue**
- Mute (`M`) only blacks out track controls; grid and waveform remain visible
- Need greyscale waveform/grid for muted tracks
- Solo (`S`) should grey out non-soloed tracks

### **Files to Modify**
1. `Source/TrackUIComponent.cpp` - Add visual desaturation

### **Implementation Steps**

#### Step 1: Add Greyscale Rendering for Muted Tracks
```cpp
// In Source/TrackUIComponent.cpp - onRender()

void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    
    // ... existing code ...
    
    // Determine if track should be greyed out
    bool shouldGreyOut = false;
    bool anySoloed = false;
    
    // Check if any track is soloed (would need parent access or global state)
    // For now, grey out if muted
    if (m_track && m_track->isMuted()) {
        shouldGreyOut = true;
    }
    
    // Apply greyscale overlay for muted/non-soloed tracks
    if (shouldGreyOut) {
        // Render a semi-transparent grey overlay over entire track
        NomadUI::NUIColor greyOverlay(0.5f, 0.5f, 0.5f, 0.6f);
        renderer.fillRect(bounds, greyOverlay.withAlpha(0.4f));
        
        // Desaturate waveform rendering (modify waveform color)
        // This would require passing a desaturation flag to drawWaveform()
    }
    
    // ... rest of rendering code ...
}
```

#### Step 2: Implement Desaturated Waveform
```cpp
// In drawWaveform() - add greyscale mode

void TrackUIComponent::drawWaveform(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& bounds,
                                     float offsetRatio, float visibleRatio) {
    // ... existing code ...
    
    // Determine waveform color based on mute/solo state
    NomadUI::NUIColor waveformColor = NomadUI::NUIColor(0.0f, 0.737f, 0.831f); // Cyan
    
    if (m_track && m_track->isMuted()) {
        // Greyscale for muted tracks
        waveformColor = NomadUI::NUIColor(0.5f, 0.5f, 0.5f, 0.6f);
    }
    
    // ... use waveformColor in rendering ...
}
```

#### Step 3: Implement Solo Inverse Effect
```cpp
// In TrackManagerUI, track solo state globally

void TrackManagerUI::onUpdate(double deltaTime) {
    // Check if any track is soloed
    bool anySoloed = false;
    for (auto& trackUI : m_trackUIComponents) {
        if (trackUI->getTrack() && trackUI->getTrack()->isSoloed()) {
            anySoloed = true;
            break;
        }
    }
    
    // Notify all tracks of solo state
    for (auto& trackUI : m_trackUIComponents) {
        trackUI->setAnySoloed(anySoloed);
    }
}

// In TrackUIComponent.cpp - render with solo awareness

void TrackUIComponent::onRender(NomadUI::NUIRenderer& renderer) {
    // ... existing code ...
    
    bool shouldGreyOut = false;
    if (m_track) {
        if (m_track->isMuted()) {
            shouldGreyOut = true;
        } else if (m_anySoloed && !m_track->isSoloed()) {
            // Grey out non-soloed tracks when solo is active
            shouldGreyOut = true;
        }
    }
    
    // ... apply greyscale as before ...
}
```

### **Testing Criteria**
- [ ] Mute a track
- [ ] Verify waveform and grid are greyscale
- [ ] Solo a track
- [ ] Verify other tracks are greyscale
- [ ] Verify soloed track remains colored

---

## 🐛 Bug #9: Pause Playback Resumption

### **Issue**
Pausing playback halts audio permanently; resumes only after full stop/reset.

### **Root Cause**
`Track::pause()` sets state to `Paused`, but `Track::play()` only plays from `Loaded` or `Stopped` states, not `Paused`.

### **Files to Modify**
1. `NomadAudio/src/Track.cpp` - Allow resume from pause

### **Implementation Steps**

#### Step 1: Fix Track Play Logic
```cpp
// In NomadAudio/src/Track.cpp - play()

void Track::play() {
    TrackState currentState = getState();
    
    // Can play from Loaded, Stopped, OR Paused states
    if (currentState == TrackState::Loaded || 
        currentState == TrackState::Stopped || 
        currentState == TrackState::Paused) { // ADDED
        
        Log::info("Playing track: " + m_name);
        
        // Reset position if stopped (start from beginning)
        // DON'T reset if paused (resume from current position)
        if (currentState == TrackState::Stopped) {
            m_positionSeconds.store(0.0);
            m_playbackPhase.store(0.0);
        }
        
        setState(TrackState::Playing);
    }
}
```

#### Step 2: Fix TrackManager Pause Logic
```cpp
// In NomadAudio/src/TrackManager.cpp - pause()

void TrackManager::pause() {
    m_isPlaying.store(false);
    // DON'T reset position here - keep it for resume

    for (auto& track : m_tracks) {
        if (!track->isSystemTrack()) {
            track->pause(); // This sets state to Paused
        }
    }

    Log::info("TrackManager: Paused");
}

// Modify play() to resume from pause
void TrackManager::play() {
    m_isPlaying.store(true);
    m_isRecording.store(false);
    // DON'T reset position if resuming from pause

    for (auto& track : m_tracks) {
        if (!track->isSystemTrack()) {
            track->play(); // Will now resume from Paused state
        }
    }

    Log::info("TrackManager: Play started");
}
```

### **Testing Criteria**
- [ ] Play audio
- [ ] Pause playback
- [ ] Resume playback
- [ ] Verify audio continues from paused position
- [ ] Verify timer displays correct position

---

## 🐛 Bug #10: Restricted Folder Handling

### **Issue**
Unreadable folders load into file browser; opening them clears the entire view (blank state).

### **Root Cause**
`FileBrowser::loadDirectoryContents()` doesn't catch permission errors before iterating directory contents.

### **Files to Modify**
1. `Source/FileBrowser.cpp` - Add permission check

### **Implementation Steps**

#### Step 1: Add Permission Check
```cpp
// In Source/FileBrowser.cpp - loadDirectoryContents()

void FileBrowser::loadDirectoryContents() {
    files_.clear();
    selectedFile_ = nullptr;
    selectedIndex_ = -1;
    
    try {
        std::filesystem::path currentDir(currentPath_);
        
        // NEW: Check if directory is readable
        std::error_code ec;
        auto dirStatus = std::filesystem::status(currentDir, ec);
        
        if (ec) {
            // Permission denied or other error
            Log::error("Cannot access directory: " + currentPath_ + " - " + ec.message());
            
            // Add error entry at top of file list
            files_.emplace_back(
                "⚠️ Access Denied",
                "",
                FileType::Unknown,
                false
            );
            files_.emplace_back(
                "Cannot read this directory. Permission denied.",
                "",
                FileType::Unknown,
                false
            );
            files_.emplace_back(
                "Use '..' to navigate back.",
                "",
                FileType::Unknown,
                false
            );
            
            // Still add parent directory for navigation
            if (currentDir.has_parent_path() && currentDir.parent_path() != currentDir) {
                files_.emplace_back("..", currentDir.parent_path().string(), FileType::Folder, true);
            }
            
            updateScrollbarVisibility();
            return; // Exit early without clearing view
        }
        
        // Add parent directory entry if not at root
        if (currentDir.has_parent_path() && currentDir.parent_path() != currentDir) {
            files_.emplace_back("..", currentDir.parent_path().string(), FileType::Folder, true);
        }
        
        // Iterate through directory contents with error handling
        for (const auto& entry : std::filesystem::directory_iterator(currentDir, ec)) {
            if (ec) {
                Log::error("Error reading directory entry: " + ec.message());
                continue; // Skip this entry
            }
            
            // ... rest of existing code ...
        }
        
        sortFiles();
    }
    catch (const std::exception& e) {
        // Handle directory access errors gracefully
        Log::error("Error loading directory: " + std::string(e.what()));
        
        // Add error message to file list
        files_.emplace_back(
            "⚠️ Error Loading Directory",
            "",
            FileType::Unknown,
            false
        );
        files_.emplace_back(
            e.what(),
            "",
            FileType::Unknown,
            false
        );
    }
    
    updateScrollbarVisibility();
}
```

#### Step 2: Prevent Navigation to Unreadable Folders
```cpp
// In navigateTo() - check permissions before navigating

void FileBrowser::navigateTo(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        Log::error("Path does not exist: " + path);
        return;
    }
    
    if (!std::filesystem::is_directory(path)) {
        Log::error("Path is not a directory: " + path);
        return;
    }
    
    // NEW: Check if directory is readable
    std::error_code ec;
    auto dirStatus = std::filesystem::status(path, ec);
    
    if (ec) {
        Log::error("Cannot access directory: " + path + " - " + ec.message());
        // Don't navigate, stay in current directory
        return;
    }
    
    setCurrentPath(path);
}
```

### **Testing Criteria**
- [ ] Navigate to `C:\Windows\System32\config` (restricted)
- [ ] Verify error message appears in file list
- [ ] Verify ".." entry is still present for navigation
- [ ] Verify file browser remains functional
- [ ] Navigate back using ".." entry

---

## 🐛 Bug #11: Track Scrollbar Icons

### **Issue**
Track scrollbars use placeholder icons (pixelated/low quality).

### **Root Cause**
Scrollbar rendering in `NomadUI/Core/NUIScrollbar.cpp` uses basic geometric shapes instead of SVG icons.

### **Files to Modify**
1. `NomadUI/Core/NUIScrollbar.cpp` - Use SVG icons for arrows
2. `Source/TrackManagerUI.cpp` - Apply icon-based scrollbars

### **Implementation Steps**

#### Step 1: Add SVG Icons to Scrollbar
```cpp
// In NomadUI/Core/NUIScrollbar.h

#include "../Core/NUIIcon.h"

class NUIScrollbar : public NUIComponent {
private:
    std::shared_ptr<NUIIcon> m_upArrowIcon;
    std::shared_ptr<NUIIcon> m_downArrowIcon;
    std::shared_ptr<NUIIcon> m_leftArrowIcon;
    std::shared_ptr<NUIIcon> m_rightArrowIcon;
};
```

```cpp
// In NomadUI/Core/NUIScrollbar.cpp - constructor

NUIScrollbar::NUIScrollbar(Orientation orientation)
    : NUIComponent()
    , orientation_(orientation)
{
    setSize(orientation == Orientation::Vertical ? 16 : 200, 
            orientation == Orientation::Vertical ? 200 : 16);
    
    // Load SVG icons from NomadAssets
    if (orientation == Orientation::Vertical) {
        m_upArrowIcon = std::make_shared<NUIIcon>();
        m_upArrowIcon->loadSVGFromFile("NomadAssets/icons/arrow_up.svg");
        m_upArrowIcon->setIconSize(12, 12);
        
        m_downArrowIcon = std::make_shared<NUIIcon>();
        m_downArrowIcon->loadSVGFromFile("NomadAssets/icons/arrow_down.svg");
        m_downArrowIcon->setIconSize(12, 12);
    } else {
        m_leftArrowIcon = std::make_shared<NUIIcon>();
        m_leftArrowIcon->loadSVGFromFile("NomadAssets/icons/arrow_left.svg");
        m_leftArrowIcon->setIconSize(12, 12);
        
        m_rightArrowIcon = std::make_shared<NUIIcon>();
        m_rightArrowIcon->loadSVGFromFile("NomadAssets/icons/arrow_right.svg");
        m_rightArrowIcon->setIconSize(12, 12);
    }
    
    updateThumbSize();
}
```

#### Step 2: Render SVG Icons Instead of Triangles
```cpp
// In NUIScrollbar.cpp - drawArrows()

void NUIScrollbar::drawArrows(NUIRenderer& renderer) const
{
    auto& themeManager = NUIThemeManager::getInstance();
    NUIColor arrowColor = themeManager.getColor("textSecondary");
    
    NUIRect leftArrowRect = getLeftArrowRect();
    NUIRect rightArrowRect = getRightArrowRect();
    
    if (orientation_ == Orientation::Vertical) {
        // Use SVG icons instead of triangles
        if (m_upArrowIcon) {
            float iconX = leftArrowRect.x + (leftArrowRect.width - 12) / 2;
            float iconY = leftArrowRect.y + (leftArrowRect.height - 12) / 2;
            m_upArrowIcon->setBounds(NUIRect(iconX, iconY, 12, 12));
            m_upArrowIcon->setColor(arrowColor);
            m_upArrowIcon->onRender(renderer);
        }
        
        if (m_downArrowIcon) {
            float iconX = rightArrowRect.x + (rightArrowRect.width - 12) / 2;
            float iconY = rightArrowRect.y + (rightArrowRect.height - 12) / 2;
            m_downArrowIcon->setBounds(NUIRect(iconX, iconY, 12, 12));
            m_downArrowIcon->setColor(arrowColor);
            m_downArrowIcon->onRender(renderer);
        }
    } else {
        // Horizontal scrollbar icons
        if (m_leftArrowIcon) {
            // ... similar logic ...
        }
        if (m_rightArrowIcon) {
            // ... similar logic ...
        }
    }
}
```

#### Step 3: Create Arrow SVG Icons (if not exists)
```xml
<!-- Save as NomadAssets/icons/arrow_up.svg -->
<svg viewBox="0 0 24 24" fill="currentColor">
    <path d="M7 14l5-5 5 5z"/>
</svg>

<!-- Save as NomadAssets/icons/arrow_down.svg -->
<svg viewBox="0 0 24 24" fill="currentColor">
    <path d="M7 10l5 5 5-5z"/>
</svg>

<!-- Save as NomadAssets/icons/arrow_left.svg -->
<svg viewBox="0 0 24 24" fill="currentColor">
    <path d="M14 7l-5 5 5 5z"/>
</svg>

<!-- Save as NomadAssets/icons/arrow_right.svg -->
<svg viewBox="0 0 24 24" fill="currentColor">
    <path d="M10 7l5 5-5 5z"/>
</svg>
```

### **Testing Criteria**
- [ ] Open track manager with scrollbars
- [ ] Verify scrollbar arrows are crisp SVG icons
- [ ] Verify icons scale properly
- [ ] Verify icon colors match theme

---

## 📊 Testing Checklist

After implementing all fixes:

### Functional Tests
- [ ] All 11 bugs are resolved
- [ ] No regressions in existing features
- [ ] Audio playback works correctly
- [ ] UI remains responsive

### Performance Tests
- [ ] FPS remains above 24 FPS with settings panel open
- [ ] No memory leaks
- [ ] Audio thread remains real-time safe

### Compatibility Tests
- [ ] Windows 10/11 compatibility
- [ ] Different audio drivers (WASAPI, ASIO)
- [ ] Various screen resolutions

---

## 🚀 Build & Deploy

1. **Build Project**
   ```powershell
   cd build
   cmake --build . --config Debug
   ```

2. **Run Tests**
   ```powershell
   cd bin/Debug
   ./NOMAD.exe
   ```

3. **Validate Fixes**
   - Work through each bug in order
   - Document any issues
   - Commit fixes incrementally

---

## 📝 Notes

- Some fixes may require additional refactoring for cleaner architecture
- Consider adding automated tests for critical paths
- Monitor performance metrics post-deployment
- Update user documentation with F12 keybinding change

---

**End of Bug Fixes Implementation Guide**
