# Adaptive FPS System - User Guide

## Overview

The **Adaptive FPS System** for NomadUI intelligently manages frame pacing between 30 and 60 FPS based on user activity and system performance, providing a fluid experience while preserving CPU and thermal efficiency.

## Features

✅ **Dynamic Frame Rate Switching**
- Idle: 30 FPS (~33.3ms/frame) for low CPU/thermal load
- Active: 60 FPS (~16.6ms/frame) for smooth interactions

✅ **Intelligent Activity Detection**
- Mouse movement, clicks, drags
- Keyboard input
- Window resize
- Scrolling
- Active animations
- Audio visualization updates

✅ **Performance Guards**
- Automatically reverts to 30 FPS if system can't sustain 60 FPS
- Monitors frame times over rolling window
- Configurable performance thresholds

✅ **Smooth Transitions**
- Gradual interpolation between frame rates (no snapping)
- Configurable transition speed

✅ **Audio Thread Independence**
- UI frame rate changes do not affect audio callback timing
- Audio remains real-time regardless of visual FPS

## Usage

### Basic Usage (Already Integrated)

The adaptive FPS system is **already integrated** into the NOMAD DAW main application. No additional setup required!

### Configuration

You can customize the adaptive FPS behavior in `Source/Main.cpp`:

```cpp
// In NomadApp constructor
NomadUI::NUIAdaptiveFPS::Config fpsConfig;
fpsConfig.fps30 = 30.0;                    // Target FPS for idle state
fpsConfig.fps60 = 60.0;                    // Target FPS for active state
fpsConfig.idleTimeout = 2.0;               // Seconds of inactivity before lowering to 30 FPS
fpsConfig.performanceThreshold = 0.018;    // Max frame time (18ms) to sustain 60 FPS
fpsConfig.performanceSampleCount = 10;     // Number of frames to average
fpsConfig.transitionSpeed = 0.05;          // Lerp factor for smooth transitions (0-1)
fpsConfig.enableLogging = false;           // Enable debug logging

m_adaptiveFPS = std::make_unique<NomadUI::NUIAdaptiveFPS>(fpsConfig);
```

### FPS Modes

The system supports three modes:

1. **Auto** (Default) - Adaptive behavior
2. **Locked30** - Always 30 FPS
3. **Locked60** - Always 60 FPS

#### Programmatic Mode Change

To change modes at runtime:

```cpp
// Set to auto mode (adaptive)
m_adaptiveFPS->setMode(NomadUI::NUIAdaptiveFPS::Mode::Auto);

// Lock to 30 FPS
m_adaptiveFPS->setMode(NomadUI::NUIAdaptiveFPS::Mode::Locked30);

// Lock to 60 FPS
m_adaptiveFPS->setMode(NomadUI::NUIAdaptiveFPS::Mode::Locked60);
```

#### Keyboard Shortcuts (NOMAD DAW)

- **F Key**: Cycle through FPS modes (Auto → Locked 30 → Locked 60 → Auto)
- **L Key**: Toggle adaptive FPS logging (debug output)

### Enable Logging

To see real-time FPS statistics:

```cpp
auto config = m_adaptiveFPS->getConfig();
config.enableLogging = true;
m_adaptiveFPS->setConfig(config);
```

Output example:
```
[AdaptiveFPS] Target: 60 FPS | Actual: 59 FPS | FrameTime: 16.8 ms | Active: YES | Idle: 0s | Can60: YES
[AdaptiveFPS] Target: 30 FPS | Actual: 30 FPS | FrameTime: 33.2 ms | Active: NO | Idle: 2.1s | Can60: YES
```

### Query Current State

```cpp
// Get current target FPS
double currentFPS = m_adaptiveFPS->getCurrentTargetFPS();

// Get average frame time
double avgFrameTime = m_adaptiveFPS->getAverageFrameTime();

// Check if user is active
bool active = m_adaptiveFPS->isUserActive();

// Get detailed statistics
auto stats = m_adaptiveFPS->getStats();
std::cout << "Target FPS: " << stats.currentTargetFPS << std::endl;
std::cout << "Actual FPS: " << stats.actualFPS << std::endl;
std::cout << "User Active: " << stats.userActive << std::endl;
std::cout << "Can Sustain 60: " << stats.canSustain60 << std::endl;
```

## Integration Details

### Main Event Loop

The adaptive FPS system integrates into the main event loop:

```cpp
while (m_running && m_window->processEvents()) {
    // Begin frame timing
    auto frameStart = m_adaptiveFPS->beginFrame();
    
    // Calculate delta time
    auto currentTime = std::chrono::high_resolution_clock::now();
    double deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
    lastTime = currentTime;
    
    // Update and render
    updateComponents(deltaTime);
    render();
    
    // End frame timing and sleep if needed
    double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);
    if (sleepTime > 0.0) {
        m_adaptiveFPS->sleep(sleepTime);
    }
}
```

### Activity Signaling

User interactions automatically signal activity to the adaptive FPS system:

```cpp
// Mouse events
m_window->setMouseMoveCallback([this](int x, int y) {
    m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseMove);
});

m_window->setMouseButtonCallback([this](int button, bool pressed) {
    if (pressed) {
        m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::MouseClick);
    }
});

// Keyboard events
m_window->setKeyCallback([this](int key, bool pressed) {
    if (pressed) {
        m_adaptiveFPS->signalActivity(NomadUI::NUIAdaptiveFPS::ActivityType::KeyPress);
    }
});

// Audio visualization
m_adaptiveFPS->setAudioVisualizationActive(audioIsPlaying);
```

## Performance Characteristics

### Idle State (30 FPS)
- **CPU Usage**: ~2-5% (depending on system)
- **Frame Time**: ~33.3ms
- **Thermal Impact**: Minimal
- **Power Consumption**: Low

### Active State (60 FPS)
- **CPU Usage**: ~5-15% (depending on system and UI complexity)
- **Frame Time**: ~16.6ms
- **Thermal Impact**: Moderate
- **Power Consumption**: Moderate

### Transition Behavior
- **Boost Time**: Immediate (on first user interaction)
- **Fallback Time**: 2 seconds after last user interaction (configurable)
- **Transition**: Smooth lerp (no visual stuttering)

## Architecture

### Components

1. **NUIAdaptiveFPS** (`NomadUI/Core/NUIAdaptiveFPS.h/.cpp`)
   - Core adaptive FPS logic
   - Frame timing management
   - Performance monitoring

2. **NUIApp Integration** (`NomadUI/Core/NUIApp.h/.cpp`)
   - Optional integration for NomadUI framework apps
   - Automatic activity detection from UI events

3. **Main Application Integration** (`Source/Main.cpp`)
   - NOMAD DAW-specific integration
   - Audio visualization activity detection
   - Global input event handling

### Thread Safety

- ✅ UI frame timing is independent of audio thread
- ✅ Audio callbacks run in real-time thread (not affected by FPS)
- ✅ Activity signaling is lightweight and non-blocking

## Troubleshooting

### Issue: FPS stays at 30 even during interaction

**Solution**: Check that activity is being signaled:
1. Enable logging to verify activity detection
2. Ensure mouse/keyboard callbacks are properly connected
3. Verify `idleTimeout` is not too short

### Issue: FPS constantly fluctuates

**Solution**: Adjust performance threshold:
```cpp
config.performanceThreshold = 0.020; // Increase to 20ms (more lenient)
config.performanceSampleCount = 15;  // Average over more frames
```

### Issue: System can't sustain 60 FPS

**Solution**: The adaptive system will automatically revert to 30 FPS. Consider:
1. Optimizing render code
2. Reducing UI complexity
3. Checking for performance bottlenecks

### Issue: Transitions are too abrupt

**Solution**: Adjust transition speed:
```cpp
config.transitionSpeed = 0.02; // Slower, smoother transitions
```

## Future Enhancements

Potential future improvements:

- **Variable frame rate tiers**: Add 45 FPS intermediate tier
- **Per-component FPS**: Different FPS for different UI regions
- **Machine learning**: Learn user interaction patterns
- **GPU-based detection**: Detect GPU bottlenecks
- **Power profile integration**: Adapt to battery vs. AC power
- **Display sync**: Sync with actual monitor refresh rate

## API Reference

### NUIAdaptiveFPS Class

```cpp
class NUIAdaptiveFPS {
public:
    enum class Mode { Auto, Locked30, Locked60 };
    enum class ActivityType { MouseMove, MouseClick, MouseDrag, Scroll, 
                              KeyPress, WindowResize, Animation, AudioVisualization };
    
    // Configuration
    void setMode(Mode mode);
    void setConfig(const Config& config);
    
    // Activity tracking
    void signalActivity(ActivityType type);
    void setAnimationActive(bool active);
    void setAudioVisualizationActive(bool active);
    
    // Frame timing
    std::chrono::high_resolution_clock::time_point beginFrame();
    double endFrame(const std::chrono::high_resolution_clock::time_point& frameStart, double deltaTime);
    void sleep(double sleepDuration);
    
    // State queries
    double getCurrentTargetFPS() const;
    double getAverageFrameTime() const;
    bool canSustain60FPS() const;
    bool isUserActive() const;
    Stats getStats() const;
};
```

## License

Part of the NOMAD DAW project - Proprietary
