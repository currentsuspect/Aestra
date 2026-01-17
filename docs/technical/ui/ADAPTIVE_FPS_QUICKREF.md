# Adaptive FPS System - Quick Reference

## 🎯 At a Glance

**Adaptive FPS** automatically switches between 30 FPS (idle) and 60 FPS (active) for optimal performance and efficiency.

## 🚀 Quick Start

Already integrated! Just build and run Aestra.

## ⚙️ Configuration

```cpp
// In Source/Main.cpp - AestraApp constructor
AestraUI::NUIAdaptiveFPS::Config fpsConfig;
fpsConfig.fps30 = 30.0;                    // Idle FPS
fpsConfig.fps60 = 60.0;                    // Active FPS
fpsConfig.idleTimeout = 2.0;               // Seconds before lowering FPS
fpsConfig.performanceThreshold = 0.018;    // Max 18ms frame time for 60 FPS
fpsConfig.performanceSampleCount = 10;     // Average over N frames
fpsConfig.transitionSpeed = 0.05;          // Smooth transition factor
fpsConfig.enableLogging = false;           // Debug logging
```

## 🎮 FPS Modes

```cpp
// Auto mode (adaptive - default)
m_adaptiveFPS->setMode(NUIAdaptiveFPS::Mode::Auto);

// Lock to 30 FPS
m_adaptiveFPS->setMode(NUIAdaptiveFPS::Mode::Locked30);

// Lock to 60 FPS
m_adaptiveFPS->setMode(NUIAdaptiveFPS::Mode::Locked60);
```

### Keyboard Shortcuts (Aestra)
- **F**: Cycle FPS modes (Auto → 30 → 60 → Auto)
- **L**: Toggle adaptive FPS logging

## 📊 Enable Debug Logging

```cpp
auto config = m_adaptiveFPS->getConfig();
config.enableLogging = true;
m_adaptiveFPS->setConfig(config);
```

## 📈 Query Stats

```cpp
auto stats = m_adaptiveFPS->getStats();
// stats.currentTargetFPS
// stats.actualFPS
// stats.averageFrameTime
// stats.userActive
// stats.canSustain60
```

## 🎨 Activity Types

Auto-detected from:
- `MouseMove` - Mouse movement
- `MouseClick` - Mouse button press
- `MouseDrag` - Dragging
- `Scroll` - Mouse wheel
- `KeyPress` - Keyboard input
- `WindowResize` - Window resize
- `Animation` - Active animations
- `AudioVisualization` - VU meters, waveforms

## 🔧 Integration Pattern

```cpp
// Main loop
auto frameStart = m_adaptiveFPS->beginFrame();

// ... update and render ...

double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);
if (sleepTime > 0.0) {
    m_adaptiveFPS->sleep(sleepTime);
}
```

## 📋 Key Files

| File | Purpose |
|------|---------|
| `AestraUI/Core/NUIAdaptiveFPS.h` | Class definition |
| `AestraUI/Core/NUIAdaptiveFPS.cpp` | Implementation |
| `AestraUI/Core/NUIApp.h/.cpp` | Framework integration |
| `Source/Main.cpp` | DAW integration |
| `AestraDocs/ADAPTIVE_FPS_GUIDE.md` | Full documentation |

## ⚡ Performance

| State | FPS | Frame Time | CPU Usage |
|-------|-----|------------|-----------|
| Idle | 30 | 33.3ms | ~2-5% |
| Active | 60 | 16.6ms | ~5-15% |
| Transition | Smooth | Lerp | Gradual |

## 🎯 Default Behavior

1. **Idle**: 30 FPS (low CPU, low thermal, low power)
2. **User interacts**: Instantly boost to 60 FPS
3. **2s of inactivity**: Gradually return to 30 FPS
4. **Can't sustain 60 FPS**: Auto-revert to 30 FPS

## ✅ Features

- ✅ Smooth transitions (no snapping)
- ✅ Performance guards (auto-adjust)
- ✅ Audio thread independence
- ✅ Configurable thresholds
- ✅ Debug logging
- ✅ Manual FPS locking

## 🔍 Troubleshooting

**Stuck at 30 FPS?**
→ Enable logging, check activity detection

**Constant fluctuations?**
→ Increase `performanceThreshold` and `performanceSampleCount`

**Too slow transitions?**
→ Decrease `transitionSpeed` (e.g., 0.02)

---

**See also**: `ADAPTIVE_FPS_GUIDE.md` for detailed documentation
