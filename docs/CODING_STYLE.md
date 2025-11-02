# 💅 Nomad DAW Coding Style Guide# 💅 Nomad DAW Coding Style Guide



> **📍 This file has been moved!**![Code Style](https://img.shields.io/badge/Code%20Style-clang--format-blue)

>![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-orange)

> The canonical Coding Style Guide is now maintained at:  

> **[`developer/coding-style.md`](developer/coding-style.md)**This guide defines the coding standards and conventions for Nomad DAW. Following these guidelines ensures consistency and maintainability across the codebase.

>

> Please update your bookmarks and use the link above.## 📋 Table of Contents



---- [Code Formatting](#-code-formatting)

- [Naming Conventions](#-naming-conventions)

## Quick Links- [File Organization](#-file-organization)

- [Comments and Documentation](#-comments-and-documentation)

- **Canonical Guide**: [developer/coding-style.md](developer/coding-style.md)- [Best Practices](#-best-practices)

- **GitHub**: [View on GitHub](https://github.com/currentsuspect/NOMAD/blob/develop/docs/developer/coding-style.md)- [C++ Guidelines](#-c-guidelines)

- **Contributing**: [developer/contributing.md](developer/contributing.md)

## 🎨 Code Formatting

---

### Automatic Formatting with clang-format

**Why the move?** This consolidates our documentation structure, placing developer-focused guides under `docs/developer/` to prevent maintenance drift from duplicate files.

Nomad uses **clang-format** for automatic code formatting. The configuration is in `.clang-format` at the project root.

**Format your code before committing:**

```bash
# Format a single file
clang-format -i Source/MyFile.cpp

# Format all C++ files in a directory
find Source -name "*.cpp" -o -name "*.h" | xargs clang-format -i

# Git hook automatically formats staged files
git commit  # Pre-commit hook runs clang-format
```

### clang-format Configuration

Our `.clang-format` is based on LLVM style with custom modifications:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 120
PointerAlignment: Left
BreakBeforeBraces: Attach
```

**Key formatting rules:**
- **Indentation**: 4 spaces (no tabs)
- **Line length**: 120 characters maximum
- **Braces**: Attached style (K&R)
- **Pointers**: `int* ptr` (left-aligned)
- **Spacing**: Space after control statements

### Indentation

**Use 4 spaces for indentation:**

```cpp
// ✅ Correct
void processAudio() {
    if (isPlaying) {
        for (int i = 0; i < bufferSize; ++i) {
            buffer[i] *= volume;
        }
    }
}

// ❌ Wrong (tabs or 2 spaces)
void processAudio() {
  if (isPlaying) {
    buffer[0] *= volume;
  }
}
```

### Line Length

**Maximum 120 characters per line:**

```cpp
// ✅ Correct (under 120 chars)
void initializeAudioEngine(int sampleRate, int bufferSize, AudioDriverType driverType);

// ✅ Correct (split long lines)
void initializeAudioEngine(int sampleRate,
                          int bufferSize,
                          AudioDriverType driverType);

// ❌ Wrong (too long)
void initializeAudioEngineWithComplexParametersAndMultipleOptionsForAdvancedConfiguration(int sampleRate, int bufferSize, AudioDriverType driverType);
```

### Braces

**Use attached braces (K&R style):**

```cpp
// ✅ Correct
if (condition) {
    doSomething();
}

class AudioEngine {
public:
    void start() {
        // ...
    }
};

// ❌ Wrong (Allman style)
if (condition)
{
    doSomething();
}
```

**Single-line statements:**

```cpp
// ✅ Correct (always use braces)
if (condition) {
    doSomething();
}

// ❌ Wrong (no braces)
if (condition)
    doSomething();
```

### Spacing

**Space after control statements:**

```cpp
// ✅ Correct
if (condition) { ... }
for (int i = 0; i < n; ++i) { ... }
while (running) { ... }

// ❌ Wrong
if(condition) { ... }
for(int i = 0; i < n; ++i) { ... }
```

**No space after function names:**

```cpp
// ✅ Correct
void processAudio(float* buffer, int size);
int getBufferSize();

// ❌ Wrong
void processAudio (float* buffer, int size);
int getBufferSize ();
```

## 🏷️ Naming Conventions

### Classes and Structs

**PascalCase** for class and struct names:

```cpp
// ✅ Correct
class AudioEngine { };
class TrackManager { };
struct AudioBuffer { };

// ❌ Wrong
class audio_engine { };
class trackManager { };
```

### Functions and Methods

**camelCase** for function names:

```cpp
// ✅ Correct
void processAudioBuffer();
int getSampleRate();
bool isPlaying() const;

// ❌ Wrong
void ProcessAudioBuffer();
int get_sample_rate();
```

### Variables

**camelCase** for local variables:

```cpp
// ✅ Correct
int bufferSize = 512;
float volume = 1.0f;
bool isPlaying = false;

// ❌ Wrong
int buffer_size = 512;
float Volume = 1.0f;
```

**Prefix member variables with `m_`:**

```cpp
class AudioEngine {
private:
    int m_sampleRate;         // ✅ Correct
    float m_volume;           // ✅ Correct
    bool m_isInitialized;     // ✅ Correct
    
    int sampleRate;           // ❌ Wrong (no prefix)
    float _volume;            // ❌ Wrong (wrong prefix)
};
```

**Prefix static members with `s_`:**

```cpp
class AudioEngine {
private:
    static int s_instanceCount;      // ✅ Correct
    static float s_globalVolume;     // ✅ Correct
};
```

### Constants and Enums

**UPPER_SNAKE_CASE** for constants:

```cpp
// ✅ Correct
constexpr int MAX_BUFFER_SIZE = 4096;
constexpr float DEFAULT_VOLUME = 1.0f;
const char* AUDIO_DRIVER_NAME = "WASAPI";

// ❌ Wrong
constexpr int maxBufferSize = 4096;
constexpr float defaultVolume = 1.0f;
```

**PascalCase** for enum classes:

```cpp
// ✅ Correct
enum class AudioDriverType {
    WASAPI,
    ASIO,
    DirectSound
};

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

// ❌ Wrong
enum class AudioDriverType {
    wasapi,
    asio,
    directSound
};
```

### Namespaces

**lowercase** for namespaces:

```cpp
// ✅ Correct
namespace nomad {
namespace audio {
    class AudioEngine { };
}
}

// ❌ Wrong
namespace Nomad {
namespace Audio {
    class AudioEngine { };
}
}
```

### File Names

**PascalCase** for file names matching class names:

```
AudioEngine.h
AudioEngine.cpp
TrackManager.h
TrackManager.cpp
```

## 📁 File Organization

### Header Files (.h)

**Standard header structure:**

```cpp
// Copyright header
// Copyright (c) 2025 Dylan Makori. All rights reserved.

#pragma once

// System includes
#include <memory>
#include <vector>

// Third-party includes
#include <juce_audio_basics/juce_audio_basics.h>

// Project includes
#include "AudioBuffer.h"
#include "AudioDriver.h"

namespace nomad {
namespace audio {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    
    // Public methods
    void start();
    void stop();
    
private:
    // Private members
    int m_sampleRate;
    bool m_isRunning;
};

} // namespace audio
} // namespace nomad
```

### Source Files (.cpp)

**Standard source structure:**

```cpp
// Copyright header
// Copyright (c) 2025 Dylan Makori. All rights reserved.

#include "AudioEngine.h"

// System includes
#include <algorithm>

// Third-party includes
#include <juce_core/juce_core.h>

namespace nomad {
namespace audio {

AudioEngine::AudioEngine()
    : m_sampleRate(44100)
    , m_isRunning(false) {
    // Constructor
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start() {
    m_isRunning = true;
}

void AudioEngine::stop() {
    m_isRunning = false;
}

} // namespace audio
} // namespace nomad
```

### Include Order

1. **Corresponding header** (for .cpp files)
2. **System headers** (`<memory>`, `<vector>`)
3. **Third-party headers** (JUCE, etc.)
4. **Project headers** (`"AudioEngine.h"`)

**Separate groups with blank lines.**

## 💬 Comments and Documentation

### File Headers

**Include copyright header in all files:**

```cpp
// Copyright (c) 2025 Dylan Makori. All rights reserved.
// NOMAD DAW - Digital Audio Workstation
```

### Class Documentation

**Document classes with descriptive comments:**

```cpp
/**
 * @brief Manages audio playback and processing pipeline.
 * 
 * The AudioEngine handles low-level audio I/O, buffer management,
 * and coordinates audio processing across multiple tracks.
 */
class AudioEngine {
    // ...
};
```

### Function Documentation

**Document public APIs:**

```cpp
/**
 * @brief Processes audio buffer with volume adjustment.
 * @param buffer Input/output audio buffer
 * @param numSamples Number of samples to process
 * @param volume Volume multiplier (0.0 to 1.0)
 */
void processAudio(float* buffer, int numSamples, float volume);
```

### Inline Comments

**Explain complex logic:**

```cpp
// Calculate playback position accounting for sample rate mismatch
// Always use OUTPUT sample rate, not file sample rate
double playbackPosition = (m_currentSample / static_cast<double>(m_outputSampleRate)) * 1000.0;
```

**Don't comment obvious code:**

```cpp
// ❌ Wrong (obvious)
i++;  // Increment i

// ✅ Correct (explains why)
i++;  // Skip padding byte in audio header
```

### TODO Comments

**Use TODO for future improvements:**

```cpp
// TODO: Implement sample rate conversion for mismatched files
// TODO(dylan): Add support for 24-bit audio files
// FIXME: Audio glitch when switching drivers
```

## ✨ Best Practices

### Use Modern C++17 Features

```cpp
// ✅ Use auto for complex types
auto audioDevice = std::make_unique<AudioEngine>();

// ✅ Use range-based for loops
for (const auto& track : tracks) {
    track->process();
}

// ✅ Use nullptr instead of NULL
float* buffer = nullptr;

// ✅ Use constexpr for compile-time constants
constexpr int BUFFER_SIZE = 512;
```

### RAII and Smart Pointers

```cpp
// ✅ Use smart pointers for ownership
std::unique_ptr<AudioEngine> m_audioEngine;
std::shared_ptr<AudioBuffer> m_sharedBuffer;

// ❌ Avoid raw pointers for ownership
AudioEngine* m_audioEngine;  // Who deletes this?
```

### Const Correctness

```cpp
// ✅ Use const for read-only parameters and methods
class AudioEngine {
public:
    int getSampleRate() const;
    void processAudio(const float* input, float* output, int size);
};
```

### Error Handling

```cpp
// ✅ Check for errors and handle gracefully
if (!audioEngine->initialize(sampleRate, bufferSize)) {
    LOG_ERROR("Failed to initialize audio engine");
    return false;
}

// ✅ Use assertions for programmer errors
assert(bufferSize > 0 && "Buffer size must be positive");
```

### Avoid Magic Numbers

```cpp
// ❌ Wrong (magic numbers)
buffer[44] = 0;
if (size > 4096) { ... }

// ✅ Correct (named constants)
constexpr int WAV_HEADER_SIZE = 44;
constexpr int MAX_BUFFER_SIZE = 4096;

buffer[WAV_HEADER_SIZE] = 0;
if (size > MAX_BUFFER_SIZE) { ... }
```

## 📝 Real Code Examples from Nomad

### Example 1: Logger Class (NomadCore/include/NomadLog.h)

This example demonstrates proper naming, structure, and documentation:

```cpp
// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <mutex>

namespace Nomad {

// =============================================================================
// Log Levels
// =============================================================================
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

// =============================================================================
// Logger Interface
// =============================================================================
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& message) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;
};

// =============================================================================
// Console Logger
// =============================================================================
class ConsoleLogger : public ILogger {
public:
    ConsoleLogger(LogLevel minLevel = LogLevel::Info) 
        : minLevel_(minLevel) {}

    void log(LogLevel level, const std::string& message) override {
        if (level < minLevel_) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << "[" << getTimestamp() << "] ";
        std::cout << getLevelString(level) << " " << message << std::endl;
    }

private:
    LogLevel minLevel_;
    std::mutex mutex_;
    
    std::string getTimestamp() const;
    std::string getLevelString(LogLevel level) const;
};

} // namespace Nomad
```

**Key Points:**
- ✅ Copyright header with NSSAL license reference
- ✅ `#pragma once` for include guards
- ✅ Section dividers for organization
- ✅ PascalCase for classes (`ILogger`, `ConsoleLogger`)
- ✅ Member variables with underscore suffix (`minLevel_`, `mutex_`)
- ✅ Virtual destructor in interface
- ✅ Const correctness (`getLevel() const`)
- ✅ Default parameter values
- ✅ Lock guard for thread safety

### Example 2: Audio Processing Function

```cpp
void TrackManager::processAudio(float* outputBuffer, uint32_t numFrames, double streamTime) {
    // Clear output buffer
    memset(outputBuffer, 0, numFrames * 2 * sizeof(float));
    
    // Mix all non-muted tracks
    for (auto& track : m_tracks) {
        if (!track->isSystemTrack() && !track->isMuted()) {
            // Process track audio into output buffer
            track->processAudio(outputBuffer, numFrames, streamTime);
        }
    }
    
    // Apply master volume with range clamping
    float masterVol = m_masterVolume.load();
    for (uint32_t i = 0; i < numFrames * 2; ++i) {
        outputBuffer[i] *= masterVol;
        
        // SAFETY: Clamp to prevent clipping
        outputBuffer[i] = std::clamp(outputBuffer[i], -1.0f, 1.0f);
    }
}
```

**Key Points:**
- ✅ camelCase for function names
- ✅ Descriptive variable names (`numFrames`, not `n`)
- ✅ Comments explain "why", not "what"
- ✅ Member variables prefixed with `m_`
- ✅ Use `auto` for iterator types
- ✅ Atomic load for thread-safe variables
- ✅ Performance annotations (`SAFETY:`)

### Example 3: UI Component Class Structure

```cpp
// © 2025 Nomad Studios – All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NomadUI/Core/NUIComponent.h"
#include "NomadUI/Core/NUIRenderer.h"

namespace NomadUI {

class TransportBar : public NUIComponent {
public:
    TransportBar();
    virtual ~TransportBar();
    
    // Position control
    void setPosition(double positionSeconds);
    double getPosition() const { return m_positionSeconds; }
    
    // Transport state
    bool isPlaying() const { return m_isPlaying; }
    void setPlaying(bool playing);
    
protected:
    // NUIComponent overrides
    void onRender(NUIRenderer& renderer) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    void onUpdate(double deltaTime) override;
    
private:
    // Helper methods
    void updatePlayButton();
    void renderTimeDisplay(NUIRenderer& renderer);
    
    // State
    double m_positionSeconds = 0.0;
    bool m_isPlaying = false;
    
    // UI components (owned)
    std::unique_ptr<NUIButton> m_playButton;
    std::unique_ptr<NUILabel> m_timeLabel;
    
    // Cached values for dirty checking
    double m_lastRenderedPosition = -1.0;
    bool m_needsRedraw = true;
};

} // namespace NomadUI
```

**Key Points:**
- ✅ Forward declarations to reduce compile time
- ✅ Public interface first, then protected, then private
- ✅ Inline simple getters in header
- ✅ Smart pointers for owned resources
- ✅ In-class initialization for simple types
- ✅ Dirty flag pattern for performance
- ✅ Clear comment organization

### Example 4: Before/After Bug Fix

**Before (Bug: Sample rate mismatch causing audio timing issues):**

```cpp
// ❌ WRONG: Uses sample file rate instead of device rate
double Track::getPosition() const {
    // BUG: This uses the sample's rate, not the device's rate!
    return m_playbackPhase.load() / (double)m_sampleInfo.sampleRate;
}
```

**After (Fixed):**

```cpp
// ✅ CORRECT: Uses device sample rate for accurate timing
double Track::getPosition() const {
    // Use device sample rate for consistent timing across all samples
    // regardless of their original sample rate
    return m_playbackPhase.load() / (double)m_deviceSampleRate;
}
```

**Explanation:**
- Audio output device runs at a fixed sample rate (e.g., 48000 Hz)
- Individual audio files may have different rates (44100, 48000, 96000 Hz)
- Position calculations must use the OUTPUT rate, not the INPUT rate
- This ensures accurate timing even when sample rates don't match

### Example 5: Performance Optimization Pattern

**Before (Slow: Allocations in audio thread):**

```cpp
// ❌ BAD: Allocates memory in real-time audio callback
void Track::processAudio(float* buffer, uint32_t frames, double time) {
    std::vector<float> tempBuffer(frames);  // ALLOCATION!
    
    // ... process audio ...
}
```

**After (Fast: Pre-allocated buffer):**

```cpp
// Header file
class Track {
private:
    std::vector<float> m_tempBuffer;  // Pre-allocated
};

// Constructor
Track::Track() {
    m_tempBuffer.resize(8192);  // Max buffer size
}

// ✅ GOOD: No allocation in audio thread
void Track::processAudio(float* buffer, uint32_t frames, double time) {
    // Reuse pre-allocated buffer (no allocation)
    memset(m_tempBuffer.data(), 0, frames * sizeof(float));
    
    // ... process audio ...
}
```

**Key Lesson:**
- Never allocate memory in real-time audio threads
- Pre-allocate buffers during initialization
- Reuse existing allocations for performance

## 🔧 C++ Guidelines

### Memory Management

- Prefer **stack allocation** over heap when possible
- Use **smart pointers** for dynamic memory
- Avoid **manual new/delete**
- Use **RAII** for resource management

### Performance

- Pass large objects **by const reference**
- Use **move semantics** for expensive objects
- Avoid **unnecessary copies**
- Profile before optimizing

### Thread Safety

- Document thread-safety requirements
- Use **std::atomic** for lock-free operations
- Prefer **std::mutex** over low-level primitives
- Avoid race conditions with proper synchronization

## 🛠️ Tooling

### clang-format

**Install and configure:**

```bash
# Install clang-format
# Windows (with Visual Studio)
# Linux
sudo apt install clang-format

# Check version (we use clang-format 14+)
clang-format --version
```

### IDE Integration

**Visual Studio**: Built-in clang-format support
**VS Code**: Install "C/C++" extension
**CLion**: Built-in support

### Git Hooks

Our pre-commit hook automatically runs clang-format:

```bash
pwsh -File scripts/install-hooks.ps1
```

## 📚 Additional Resources

- **[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)** — Industry standard C++ best practices
- **[clang-format Documentation](https://clang.llvm.org/docs/ClangFormat.html)** — Code formatting tool
- **[Architecture Overview](ARCHITECTURE.md)** — System design and structure
- **[Contributing Guide](CONTRIBUTING.md)** — How to contribute to Nomad
- **[Style Guide](STYLE_GUIDE.md)** — Documentation and comment standards

---

**Write clean code that others enjoy reading!** 💻

*Last updated: January 2025*
