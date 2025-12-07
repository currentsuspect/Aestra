# 🔍 Debugging Guide

This guide covers debugging techniques, tools, and best practices for working with Nomad DAW.

---

## 📋 Table of Contents

- [Development Setup](#-development-setup)
- [Visual Studio Debugging](#-visual-studio-debugging)

- [Logging System](#-logging-system)
- [Common Issues](#-common-issues)
- [Audio Debugging](#-audio-debugging)
- [UI Debugging](#-ui-debugging)
- [Memory Debugging](#-memory-debugging)

---

## 🛠️ Development Setup

### Debug vs Release Builds

**Debug Build** (recommended for development):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNOMAD_CORE_MODE=ON
cmake --build build --config Debug
```

**Features:**
- Optimization disabled (`/Od` on MSVC, `-O0` on GCC)
- Debug symbols included (`/Zi`, `-g`)
- Assertions enabled (`NDEBUG` not defined)
- Easier to step through code

**Release Build** (for performance testing):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNOMAD_CORE_MODE=ON
cmake --build build --config Release
```

**Features:**
- Full optimization (`/O2`, `-O3`)
- Minimal debug symbols
- Assertions disabled
- Better performance profiling

---

## 🐛 Visual Studio Debugging

### Basic Debugging

1. **Start Debugging:**
   - Press `F5` or click "Start Debugging"
   - Application runs under debugger

2. **Set Breakpoints:**
   - Click in left margin next to line number
   - Red dot appears
   - Execution pauses when breakpoint is hit

3. **Step Through Code:**
   - `F10` — Step Over (executes current line)
   - `F11` — Step Into (enters function calls)
   - `Shift+F11` — Step Out (exits current function)
   - `F5` — Continue (runs until next breakpoint)

### Conditional Breakpoints

Right-click a breakpoint → **Conditions...**

**Example:** Break only when a variable has a specific value
```cpp
// Break when track position exceeds 5.0 seconds
m_positionSeconds > 5.0
```

**Example:** Break on specific iteration
```cpp
// Break on 100th loop iteration
i == 100
```

### Data Breakpoints

Break when a variable's value changes:
1. Right-click variable → **Break When Value Changes**
2. Debugger pauses whenever the variable is modified

**Use Case:** Track down unexpected variable changes

### Watch Window

View variable values while debugging:
1. `Debug → Windows → Watch → Watch 1`
2. Add variables to watch
3. Values update automatically while stepping

**Example Watch Expressions:**
```
m_positionSeconds
m_tracks.size()
m_isPlaying.load()
renderer.getFPS()
```

### Call Stack

View function call hierarchy:
1. `Debug → Windows → Call Stack`
2. Double-click to jump to any frame

**Use Case:** Trace how code reached current point

---



## 📝 Logging System

Nomad includes a comprehensive logging system.

### Log Levels

```cpp
namespace Nomad {
    enum class LogLevel {
        Debug,    // Verbose debugging info
        Info,     // General information
        Warning,  // Potential issues
        Error,    // Errors that don't crash app
        Fatal     // Critical errors that may crash
    };
}
```

### Using the Logger

```cpp
#include "NomadCore/Log.h"

// Basic logging
Log::debug("Loading sample from path: " + path);
Log::info("Audio device initialized");
Log::warning("Sample rate mismatch: expected 44100, got 48000");
Log::error("Failed to load sample: " + filename);
Log::fatal("Audio thread crashed!");

// Formatted logging
Log::info("FPS: " + std::to_string(fps) + ", Frame Time: " + std::to_string(frameTime) + "ms");
```

### Log Output

**Console Output:**
```
[INFO]  [2025-01-15 14:32:10] Audio device initialized
[WARN]  [2025-01-15 14:32:11] Sample rate mismatch
[ERROR] [2025-01-15 14:32:12] Failed to load sample: kick.wav
```

**Log File:**
- Windows: `%APPDATA%\Nomad\logs\nomad.log`
- Linux: `~/.nomad/logs/nomad.log`

### Setting Log Level

```cpp
// In Source/Main.cpp - initialize()
Log::setLogLevel(LogLevel::Debug);  // Show all logs
Log::setLogLevel(LogLevel::Info);   // Default level
Log::setLogLevel(LogLevel::Error);  // Only errors
```

### Custom Log Formatting

```cpp
// Add timestamps
Log::info("[" + getCurrentTimeString() + "] MyEvent");

// Add context
Log::info("[Track " + std::to_string(trackId) + "] Position: " + std::to_string(pos));
```

---

## 🔧 Common Issues

### Issue 1: Audio Not Playing

**Symptoms:** No sound output, timer not advancing

**Debug Steps:**
1. Check log for audio initialization errors:
   ```
   [ERROR] AudioDeviceManager: Failed to initialize WASAPI
   ```

2. Verify audio device:
   ```cpp
   Log::info("Active Device: " + m_audioDevice->getName());
   Log::info("Sample Rate: " + std::to_string(m_sampleRate));
   ```

3. Check track state:
   ```cpp
   Log::info("Track State: " + std::to_string((int)track->getState()));
   Log::info("Is Playing: " + std::to_string(m_isPlaying.load()));
   ```

4. Verify audio callback is running:
   ```cpp
   void processAudio(float* buffer, uint32_t frames, double time) {
       static int callCount = 0;
       if (++callCount % 100 == 0) {
           Log::debug("Audio callback called " + std::to_string(callCount) + " times");
       }
   }
   ```

### Issue 2: Crash on Startup

**Symptoms:** Application crashes immediately

**Debug Steps:**
1. Run under debugger (F5)
2. Check call stack when crash occurs
3. Look for null pointer dereferences:
   ```cpp
   if (m_trackManager != nullptr) {  // Add null checks
       m_trackManager->play();
   }
   ```

4. Check initialization order:
   ```cpp
   // Ensure dependencies initialized first
   initAudioEngine();    // Before
   initUI();             // After (depends on audio)
   ```

### Issue 3: Memory Leak

**Symptoms:** Memory usage increases over time

**Debug Steps:**
1. Use Visual Studio Diagnostic Tools:
   - `Debug → Windows → Show Diagnostic Tools`
   - View memory graph

2. Find leaks with CRT Debug Heap:
   ```cpp
   // In Main.cpp
   #ifdef _DEBUG
   #define _CRTDBG_MAP_ALLOC
   #include <crtdbg.h>
   #endif
   
   int main() {
       #ifdef _DEBUG
       _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
       #endif
       
       // ... app code ...
   }
   ```

3. Check for missing destructors:
   ```cpp
   class MyComponent {
   public:
       ~MyComponent() {
           Log::debug("MyComponent destroyed");  // Should see this
       }
   };
   ```

---

## 🎵 Audio Debugging

### Sample Rate Issues

**Problem:** Audio pitch is wrong or plays too fast/slow

**Cause:** Sample rate mismatch between file and device

**Debug:**
```cpp
Log::info("Sample File Rate: " + std::to_string(sampleInfo.sampleRate));
Log::info("Device Output Rate: " + std::to_string(m_deviceSampleRate));

if (sampleInfo.sampleRate != m_deviceSampleRate) {
    Log::warning("Sample rate mismatch detected!");
}
```

**Fix:** Use `m_deviceSampleRate` for position calculations, not `sampleInfo.sampleRate`.

### Buffer Underruns

**Problem:** Audio crackling or stuttering

**Cause:** Audio thread isn't fast enough to fill buffer

**Debug:**
```cpp
void processAudio(float* buffer, uint32_t frames, double time) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // ... process audio ...
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Calculate available time for this buffer
    double availableTime = (frames / (double)m_sampleRate) * 1000000.0;  // microseconds
    
    if (duration.count() > availableTime * 0.8) {
        Log::warning("Audio thread is slow! " + std::to_string(duration.count()) + "us / " + 
                     std::to_string(availableTime) + "us");
    }
}
```

### Latency Measurement

```cpp
// Measure round-trip latency
auto inputTime = std::chrono::high_resolution_clock::now();

// ... audio processing ...

auto outputTime = std::chrono::high_resolution_clock::now();
auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(outputTime - inputTime);

Log::info("Audio Latency: " + std::to_string(latency.count()) + "ms");
```

---

## 🎨 UI Debugging

### Rendering Issues

**Problem:** UI elements not appearing or positioned incorrectly

**Debug:**
```cpp
void MyComponent::onRender(NUIRenderer& renderer) {
    auto bounds = getBounds();
    
    // Log bounds
    Log::debug("Component bounds: x=" + std::to_string(bounds.x) + 
               ", y=" + std::to_string(bounds.y) +
               ", w=" + std::to_string(bounds.width) +
               ", h=" + std::to_string(bounds.height));
    
    // Draw debug outline
    #ifdef DEBUG_RENDERING
    renderer.strokeRect(bounds, 2.0f, NUIColor(1.0f, 0.0f, 0.0f));  // Red outline
    #endif
    
    // ... normal rendering ...
}
```

### Event Handling

**Problem:** Clicks not registering

**Debug:**
```cpp
bool MyComponent::onMouseEvent(const NUIMouseEvent& event) {
    Log::debug("Mouse event: type=" + std::to_string((int)event.type) +
               ", x=" + std::to_string(event.position.x) +
               ", y=" + std::to_string(event.position.y));
    
    auto bounds = getBounds();
    bool contains = bounds.contains(event.position);
    Log::debug("Contains mouse: " + std::to_string(contains));
    
    // ... handle event ...
}
```

### FPS Drops

**Problem:** UI feels sluggish

**Debug:**
```cpp
// In Main.cpp run loop
auto frameStart = std::chrono::high_resolution_clock::now();

// ... render frame ...

auto frameEnd = std::chrono::high_resolution_clock::now();
auto frameTime = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);

if (frameTime.count() > 33) {  // > 30 FPS
    Log::warning("Slow frame: " + std::to_string(frameTime.count()) + "ms");
}
```

**Solution:** Profile with Tracy to find bottleneck

---

## 💾 Memory Debugging

### Detecting Leaks with Valgrind (Linux)

```bash
valgrind --leak-check=full --track-origins=yes ./NOMAD
```

### Smart Pointers

Use smart pointers to prevent leaks:

```cpp
// Bad: Manual memory management
MyClass* obj = new MyClass();
// ... if exception occurs, memory leaks ...
delete obj;

// Good: Smart pointer
std::unique_ptr<MyClass> obj = std::make_unique<MyClass>();
// ... automatically cleaned up ...
```

### RAII Pattern

Wrap resources in RAII classes:

```cpp
class AudioBuffer {
public:
    AudioBuffer(size_t size) {
        m_data = new float[size];
        m_size = size;
    }
    
    ~AudioBuffer() {
        delete[] m_data;  // Automatic cleanup
    }
    
private:
    float* m_data;
    size_t m_size;
};
```

---

## 🧪 Unit Testing (Future)

Nomad will include unit tests for critical components:

```cpp
// Example: Track position calculation test
TEST(TrackTest, PositionCalculation) {
    Track track;
    track.setSampleRate(48000);
    track.setPosition(2.0);  // 2 seconds
    
    EXPECT_EQ(track.getPosition(), 2.0);
    EXPECT_EQ(track.getSamplePosition(), 96000);  // 2 * 48000
}
```

---

## 📚 Additional Resources

- **[Bug Reports Guide](bug-reports.md)** — How to report bugs effectively
- **[Performance Tuning](performance-tuning.md)** — Optimization techniques
- **[Coding Style Guide](../CODING_STYLE.md)** — Code conventions

---

**Happy Debugging!** 🐛🔨

*Last updated: January 2025*
