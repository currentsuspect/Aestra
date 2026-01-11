# Profiler Integration and Migration Guide

## Overview

This document describes how to migrate from the old duplicate profiler systems to the new unified profiler.

## Old System Issues

### Duplicate Profiler Systems
1. **NomadProfiler** (NomadCore) - Zone timing, frame stats, Chrome trace export
2. **NUIFrameProfiler** (NomadUI) - UI frame timing, console output
3. **PerformanceHUD** - Visual overlay using NomadProfiler data

### Problems Identified
- ❌ Duplicate FPS calculations
- ❌ Similar timing mechanisms in multiple places
- ❌ Fragmented performance data
- ❌ No unified export format
- ❌ Limited integration between profilers
- ❌ Unused legacy profiler in Main.cpp (line 2145)

## New Unified Profiler Benefits

### ✅ Consolidated Architecture
- Single profiler handles all timing needs
- Thread-aware zone profiling
- Enhanced Chrome trace export with metadata
- Performance regression detection
- Automated performance alerts
- Memory and GPU profiling hooks
- HTML report generation

### ✅ Enhanced Features
- **Memory Profiling**: Track allocations, deallocations, peak usage
- **Thread Profiling**: Per-thread CPU time and zone statistics  
- **Performance Alerts**: Real-time detection of frame spikes, audio overload
- **Regression Detection**: Automatic baseline comparison and alerts
- **Enhanced Export**: JSON with metadata + HTML reports

## Migration Steps

### Step 1: Replace Include Files

**Old:**
```cpp
#include "NomadProfiler.h"        // NomadCore version
#include "NUIFrameProfiler.h"     // NomadUI version
```

**New:**
```cpp
#include "NomadUnifiedProfiler.h" // Single unified profiler
```

### Step 2: Update Main Application Loop

**Old Main.cpp (around line 1291):**
```cpp
// Begin profiler frame
Profiler::getInstance().beginFrame();

// Begin frame timing BEFORE any work
auto frameStart = m_adaptiveFPS->beginFrame();

{
    NOMAD_ZONE("Input_Poll");
    // ... input handling ...
}

{
    NOMAD_ZONE("UI_Update");
    // ... UI updates ...
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

// End profiler frame
Profiler::getInstance().endFrame();
```

**New Main.cpp:**
```cpp
// Begin unified profiler frame
Nomad::UnifiedProfiler::getInstance().beginFrame();

// Begin frame timing BEFORE any work
auto frameStart = m_adaptiveFPS->beginFrame();

{
    NOMAD_ZONE("Input_Poll");
    // ... input handling ...
}

{
    NOMAD_ZONE("UI_Update");
    // ... UI updates ...
}

{
    NOMAD_ZONE("Render_Prep");
    render();
}

// Mark render end for breakdown timing
Nomad::UnifiedProfiler::getInstance().markRenderEnd();

// End frame timing BEFORE swapBuffers (to exclude VSync wait)
double sleepTime = m_adaptiveFPS->endFrame(frameStart, deltaTime);

{
    NOMAD_ZONE("GPU_Submit");
    // SwapBuffers (may block on VSync)
    m_window->swapBuffers();
}

// Mark swap end for breakdown timing
Nomad::UnifiedProfiler::getInstance().markSwapEnd();

// Sync audio telemetry
Nomad::UnifiedProfiler::getInstance().syncAudioTelemetry();

// End unified profiler frame
Nomad::UnifiedProfiler::getInstance().endFrame();
```

### Step 3: Update PerformanceHUD Integration

**Old PerformanceHUD.cpp:**
```cpp
void PerformanceHUD::update() {
    if (!isVisible()) return;
    
    // Update graph
    const auto& stats = m_profiler.getCurrentFrame();
    m_frameTimeGraph[m_graphIndex] = static_cast<float>(stats.totalTimeMs);
    m_graphIndex = (m_graphIndex + 1) % GRAPH_SAMPLES;
}
```

**New PerformanceHUD.cpp:**
```cpp
void PerformanceHUD::update() {
    if (!isVisible()) return;
    
    // Update graph with enhanced frame data
    const auto& stats = m_profiler.getCurrentFrame();
    m_frameTimeGraph[m_graphIndex] = static_cast<float>(stats.totalTimeMs);
    m_graphIndex = (m_graphIndex + 1) % GRAPH_SAMPLES;
    
    // Check for active performance alerts
    if (m_profiler.hasActiveAlerts()) {
        auto alerts = m_profiler.getActiveAlerts();
        for (const auto& alert : alerts) {
            Log::warning("Performance Alert: " + alert.message);
        }
    }
}
```

### Step 4: Remove Legacy Profiler

**Remove from Main.cpp (around line 2145):**
```cpp
// REMOVE THIS LINE:
NomadUI::NUIFrameProfiler m_profiler;  // Legacy profiler (can be removed later)
```

### Step 5: Add Memory Profiling Hooks

**In allocation-heavy code:**
```cpp
// Add memory tracking for major allocations
void* ptr = malloc(size);
NOMAD_MEMORY_ALLOC(size);

// Later when freeing:
free(ptr);
NOMAD_MEMORY_FREE(size);
```

### Step 6: Configure Performance Baselines

**In application initialization:**
```cpp
// Set performance baselines for regression detection
auto& profiler = Nomad::UnifiedProfiler::getInstance();
profiler.setPerformanceBaseline("frameTimeMs", 16.7);  // Target 60fps
profiler.setPerformanceBaseline("audioLoadPercent", 50.0);  // Target audio load

// Configure alert thresholds
profiler.setAlertThresholds(20.0, 85.0);  // 20ms frame time, 85% audio load
```

### Step 7: Update Export Functionality

**Replace old export calls:**
```cpp
// Old:
Profiler::getInstance().exportToJSON("nomad_profile.json");

// New:
auto& profiler = Nomad::UnifiedProfiler::getInstance();
profiler.exportPerformanceReport("nomad_performance_report");  // Exports both JSON and HTML
```

## Audio Integration Enhancements

### Current Audio Profiling (Already Good)
The existing AudioTelemetry system is comprehensive and already provides:
- ✅ Real-time audio callback timing
- ✅ Xrun/underrun detection
- ✅ Buffer size and sample rate tracking
- ✅ CPU load percentage calculation
- ✅ Sample rate conversion (SRC) activity

### Enhanced Audio Integration
The unified profiler integrates seamlessly with AudioTelemetry:

```cpp
// In main loop - already implemented:
if (trackManager) {
    Profiler::getInstance().setAudioLoad(trackManager->getAudioLoadPercent());
}

// New unified profiler version:
auto& profiler = Nomad::UnifiedProfiler::getInstance();
profiler.setAudioLoad(trackManager->getAudioLoadPercent());
profiler.syncAudioTelemetry();  // Additional audio metrics from AudioTelemetry
```

## Performance Monitoring Features

### Real-time Alerts
The unified profiler automatically detects:
- **Frame Time Spikes**: > threshold (default 16.7ms)
- **High Audio Load**: > threshold (default 80%)
- **Audio Xruns**: Any underruns detected
- **Memory Pressure**: Rapid allocation growth

### Performance Regression Detection
- Monitors performance trends over time
- Compares against established baselines
- Alerts on significant regressions (>20% frame time, >30% audio load)
- Provides historical performance comparison

### Enhanced Export Formats

#### JSON (Chrome Trace Compatible)
```json
{
  "traceEvents": [
    {
      "name": "Metadata",
      "cat": "metadata", 
      "ph": "i",
      "args": {
        "buildInfo": "NOMAD-2025-Core",
        "totalFrames": 15000,
        "systemInfo": "Windows 10, Intel i7, RTX 3080"
      }
    },
    {
      "name": "Frame",
      "cat": "frame",
      "ph": "X", 
      "dur": 16670,
      "args": {
        "cpu": 8.5,
        "gpu": 4.2,
        "render": 6.1,
        "swap": 2.4,
        "audioLoad": 45.2,
        "drawCalls": 1250,
        "memoryMB": 256.7
      }
    }
  ]
}
```

#### HTML Reports
- Interactive performance dashboards
- Performance alert summaries
- Regression analysis charts
- Memory usage trends

## Testing and Validation

### Verification Steps
1. **Compile**: Ensure new profiler compiles without errors
2. **Runtime**: Verify no crashes or performance regressions
3. **Export**: Test JSON and HTML export functionality
4. **Alerts**: Verify performance alerts trigger correctly
5. **Integration**: Confirm audio telemetry integration works

### Performance Validation
```cpp
// Test script - add to development builds
void validateProfilerPerformance() {
    auto& profiler = Nomad::UnifiedProfiler::getInstance();
    
    // Run for 1000 frames
    for (int i = 0; i < 1000; ++i) {
        profiler.beginFrame();
        // Simulate work
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
        profiler.endFrame();
    }
    
    profiler.printPerformanceSummary();
    
    // Validate memory usage doesn't grow
    assert(profiler.getCurrentFrame().memory.currentBytes < 10 * 1024 * 1024); // < 10MB
}
```

## Rollback Plan

If issues arise during migration:

1. **Keep Old Profiler**: Don't delete old files immediately
2. **Conditional Compilation**: Use build flags to switch between profilers
3. **Gradual Migration**: Migrate components one at a time
4. **Performance Testing**: Compare before/after performance metrics

## Benefits Summary

After migration, you'll have:

✅ **Single profiler system** - eliminates confusion and duplication
✅ **Enhanced performance monitoring** - memory, GPU, threading
✅ **Automated performance alerts** - proactive issue detection  
✅ **Performance regression detection** - prevents performance degradation
✅ **Better export formats** - JSON + HTML reports
✅ **Thread-aware profiling** - better multi-threaded analysis
✅ **Audio integration** - seamless with existing AudioTelemetry
✅ **Zero overhead when disabled** - compile-time optimization

The unified profiler provides a robust, feature-complete performance monitoring solution that consolidates the best aspects of the existing profilers while adding significant new capabilities.