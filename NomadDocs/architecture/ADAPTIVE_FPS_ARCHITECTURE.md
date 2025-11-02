# Adaptive FPS System - Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           NOMAD DAW Application                              │
│                              (Source/Main.cpp)                               │
└─────────────────────────────┬───────────────────────────────────────────────┘
                              │
                              │ Creates & Configures
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        NUIAdaptiveFPS Manager                                │
│                     (NomadUI/Core/NUIAdaptiveFPS)                           │
├─────────────────────────────────────────────────────────────────────────────┤
│  Configuration:                                                             │
│  ├─ fps30: 30.0                  ├─ performanceThreshold: 0.018            │
│  ├─ fps60: 60.0                  ├─ performanceSampleCount: 10             │
│  ├─ idleTimeout: 2.0s            └─ transitionSpeed: 0.05                  │
│                                                                              │
│  State Tracking:                                                            │
│  ├─ currentTargetFPS             ├─ averageFrameTime                       │
│  ├─ userActive (bool)            ├─ frameTimeHistory (deque)               │
│  ├─ idleTimer                    └─ framesSince60FPSChange                 │
│  ├─ animationActive                                                         │
│  └─ audioVisualizationActive                                                │
└───────────┬─────────────────────────────────────────────────────────────────┘
            │
            │ Controls
            │
┌───────────▼─────────────────────────────────────────────────────────────────┐
│                           Main Event Loop                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. frameStart = adaptiveFPS->beginFrame()                                  │
│       │                                                                      │
│       ├─→ Record start timestamp                                            │
│       │                                                                      │
│  2. Process Events (pollEvents)                                             │
│       │                                                                      │
│       ├─→ Mouse Move      ──→ signalActivity(MouseMove)                     │
│       ├─→ Mouse Click     ──→ signalActivity(MouseClick)                    │
│       ├─→ Mouse Wheel     ──→ signalActivity(Scroll)                        │
│       ├─→ Key Press       ──→ signalActivity(KeyPress)                      │
│       └─→ Window Resize   ──→ signalActivity(WindowResize)                  │
│                                                                              │
│  3. Update Components                                                       │
│       │                                                                      │
│       ├─→ Animations      ──→ setAnimationActive(true)                      │
│       └─→ Audio Viz       ──→ setAudioVisualizationActive(true)             │
│                                                                              │
│  4. Render Frame                                                            │
│       │                                                                      │
│       └─→ Draw UI (independent of FPS logic)                                │
│                                                                              │
│  5. sleepTime = adaptiveFPS->endFrame(frameStart, deltaTime)                │
│       │                                                                      │
│       ├─→ Calculate actual frame time                                       │
│       ├─→ Update performance metrics (rolling avg)                          │
│       ├─→ Check activity state                                              │
│       ├─→ Update target FPS:                                                │
│       │    ├─ If active && canSustain60FPS → 60 FPS                         │
│       │    ├─ If idle > 2s → 30 FPS                                         │
│       │    └─ Smooth lerp transition                                        │
│       └─→ Return sleep duration = (targetFrameTime - actualFrameTime)       │
│                                                                              │
│  6. adaptiveFPS->sleep(sleepTime)                                           │
│       │                                                                      │
│       └─→ std::this_thread::sleep_for(duration)                             │
│                                                                              │
│  7. Loop back to step 1                                                     │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────┐
│                       FPS State Machine                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│      ┌──────────┐                                                           │
│      │   IDLE   │  ← Start Here                                             │
│      │  30 FPS  │                                                            │
│      └────┬─────┘                                                            │
│           │                                                                  │
│           │ User Activity Detected                                           │
│           │ (mouse, keyboard, scroll, etc.)                                  │
│           │                                                                  │
│           ▼                                                                  │
│      ┌──────────┐                                                           │
│      │  ACTIVE  │                                                            │
│   ┌─▶│  60 FPS  │◀─┐                                                        │
│   │  └────┬─────┘  │                                                         │
│   │       │        │                                                         │
│   │       │        │ Continued Activity                                      │
│   │       │        │ (reset idle timer)                                      │
│   │       │        │                                                         │
│   │       │        └────────────────────────┐                                │
│   │       │                                 │                                │
│   │       │ No Activity                     │                                │
│   │       │ for 2 seconds                   │                                │
│   │       │                                 │                                │
│   │       ▼                                 │                                │
│   │  ┌──────────┐                           │                                │
│   │  │TRANSITION│                           │                                │
│   │  │  60→30   │                           │                                │
│   │  └────┬─────┘                           │                                │
│   │       │                                 │                                │
│   │       │ Smooth Lerp                     │                                │
│   │       │                                 │                                │
│   │       ▼                                 │                                │
│   │  ┌──────────┐                           │                                │
│   └──│   IDLE   │                           │                                │
│      │  30 FPS  │◀──────────────────────────┘                                │
│      └──────────┘                                                            │
│                                                                              │
│                      Performance Guard:                                      │
│                                                                              │
│      If frameTime > 18ms for multiple frames:                               │
│                                                                              │
│      ┌──────────┐                                                           │
│      │  ACTIVE  │                                                            │
│      │  60 FPS  │                                                            │
│      └────┬─────┘                                                            │
│           │                                                                  │
│           │ Performance degradation detected                                 │
│           │ (averageFrameTime > threshold)                                   │
│           │                                                                  │
│           ▼                                                                  │
│      ┌──────────┐                                                           │
│      │SAFE MODE │                                                            │
│      │  30 FPS  │                                                            │
│      └──────────┘                                                            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────┐
│                    Activity Detection Flow                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  User Input Events                                                          │
│      │                                                                       │
│      ├─→ Mouse Move    ──┐                                                  │
│      ├─→ Mouse Click   ──┤                                                  │
│      ├─→ Mouse Drag    ──┤                                                  │
│      ├─→ Scroll        ──┤                                                  │
│      ├─→ Key Press     ──┤                                                  │
│      └─→ Window Resize ──┤                                                  │
│                          │                                                  │
│                          ▼                                                  │
│                  signalActivity(type)                                       │
│                          │                                                  │
│                          ├─→ Set userActive = true                          │
│                          ├─→ Reset idleTimer = 0                            │
│                          └─→ Log activity (if enabled)                      │
│                                                                              │
│  UI State Changes                                                           │
│      │                                                                       │
│      ├─→ Animation Active   ──→ setAnimationActive(true)                    │
│      └─→ Audio Visualizing  ──→ setAudioVisualizationActive(true)           │
│                                                                              │
│  Frame Update                                                               │
│      │                                                                       │
│      └─→ Check: userActive || animationActive || audioVizActive             │
│           │                                                                  │
│           ├─ YES → Boost to 60 FPS (if performance allows)                  │
│           │                                                                  │
│           └─ NO  → Increment idleTimer                                       │
│                    │                                                         │
│                    └─→ if idleTimer > 2s → Lower to 30 FPS                  │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────┐
│                   Performance Monitoring                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Frame Time Measurement:                                                    │
│                                                                              │
│      frameStart = now()                                                     │
│           │                                                                  │
│           ├─→ Process Events                                                │
│           ├─→ Update Logic                                                  │
│           └─→ Render Frame                                                  │
│           │                                                                  │
│      frameEnd = now()                                                       │
│           │                                                                  │
│      frameTime = frameEnd - frameStart                                      │
│                                                                              │
│  Rolling Window Average:                                                    │
│                                                                              │
│      frameTimeHistory.push_back(frameTime)                                  │
│           │                                                                  │
│           └─→ Keep last 10 samples                                          │
│           │                                                                  │
│      averageFrameTime = sum(history) / count(history)                       │
│                                                                              │
│  Performance Check:                                                         │
│                                                                              │
│      canSustain60FPS = (averageFrameTime < 0.018)  // 18ms threshold        │
│           │                                                                  │
│           ├─ TRUE  → OK to run at 60 FPS                                    │
│           └─ FALSE → Revert to 30 FPS                                       │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘


┌─────────────────────────────────────────────────────────────────────────────┐
│                   Thread Independence                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────┐              ┌────────────────────┐                 │
│  │   UI Thread        │              │  Audio Thread      │                 │
│  │                    │              │  (Real-Time)       │                 │
│  ├────────────────────┤              ├────────────────────┤                 │
│  │                    │              │                    │                 │
│  │ Adaptive FPS       │              │ Audio Callback     │                 │
│  │ ├─ 30 FPS (idle)   │   INDEPENDENT│ ├─ 44.1/48 kHz    │                 │
│  │ └─ 60 FPS (active) │   ═══════════│ └─ Fixed timing   │                 │
│  │                    │              │                    │                 │
│  │ Frame Timing:      │              │ Sample Timing:     │                 │
│  │ ├─ beginFrame()    │              │ ├─ Process buffer  │                 │
│  │ ├─ render()        │              │ ├─ Mix tracks      │                 │
│  │ ├─ endFrame()      │              │ └─ Output samples  │                 │
│  │ └─ sleep()         │              │                    │                 │
│  │                    │              │ ** NEVER BLOCKS ** │                 │
│  │ ** Can change **   │              │ ** Always on time **│                │
│  │                    │              │                    │                 │
│  └────────────────────┘              └────────────────────┘                 │
│                                                                              │
│  Result: UI FPS changes DO NOT affect audio quality or timing!              │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Key Metrics

| Metric | Idle (30 FPS) | Active (60 FPS) |
|--------|---------------|-----------------|
| Target Frame Time | 33.3ms | 16.6ms |
| Actual Frame Time | ~33.2ms | ~16.8ms |
| Sleep Time | ~32ms | ~15ms |
| CPU Usage | ~2-5% | ~5-15% |
| Thermal Impact | Minimal | Moderate |
| Power Consumption | Low | Moderate |

## Transition Characteristics

```
FPS Transition (30 → 60):
  30 ─┐
      │          ╱────────────── 60
      │        ╱
      │      ╱
      │    ╱
      │  ╱
      └╱─────────────────────────► Time
       ↑
    Activity
    detected
    (instant start,
     smooth ramp)

FPS Transition (60 → 30):
  60 ──────────┐
               │╲
               │ ╲
               │  ╲
               │   ╲
               │    ╲
      30 ──────┴─────╲───────────► Time
                      ↑
                   2s idle
                   (smooth ramp)
```

## File Relationships

```
Main.cpp
  │
  ├─→ Creates NUIAdaptiveFPS instance
  │
  ├─→ Configures (fps30, fps60, thresholds)
  │
  └─→ Integrates into main loop
      │
      ├─→ beginFrame()
      ├─→ endFrame()
      └─→ sleep()

NUIPlatformBridge.cpp
  │
  └─→ Event callbacks
      │
      ├─→ Mouse events → Main.cpp → signalActivity()
      ├─→ Keyboard events → Main.cpp → signalActivity()
      └─→ Window events → Main.cpp → signalActivity()

NUIAdaptiveFPS.cpp
  │
  ├─→ Activity tracking
  ├─→ Performance monitoring
  ├─→ FPS calculation
  └─→ Sleep time calculation
```
