# Coding Style Guide

Aestra follows a consistent C++17 coding style enforced by clang-format (LLVM-based). This guide covers naming conventions, formatting rules, and best practices.

## Quick Reference

| Element | Convention | Example |
|---------|-----------|---------|
| Classes/Structs | PascalCase | `AudioEngine`, `TrackManager` |
| Functions/Methods | camelCase | `initialize()`, `setSampleRate()` |
| Member variables | camelCase with trailing `_` | `sampleRate_`, `bufferSize_` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_CHANNELS`, `DEFAULT_SAMPLE_RATE` |
| Enums | PascalCase values | `AudioState::Playing` |
| Namespaces | lowercase | `Aestra::core`, `Aestra::audio` |
| Files | PascalCase | `AudioEngine.h`, `TrackManager.cpp` |
| Private members | Leading underscore or trailing `_` | `internalState_` |

## Formatting

Formatting is enforced automatically by clang-format via pre-commit hooks. Run manually:

```bash
clang-format -i Source/**/*.cpp AestraAudio/**/*.cpp AestraUI/**/*.cpp
```

Key rules (from `.clang-format`):

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 120 characters max
- **Braces**: Attach style (opening brace on same line)
- **Pointer alignment**: Left (`int* ptr`, not `int *ptr`)
- **Includes**: Sorted and grouped (system, third-party, project)

## Include Order

```cpp
// 1. Corresponding header
#include "AudioEngine.h"

// 2. Aestra project headers
#include "AestraCore/Logger.h"
#include "AestraAudio/Types.h"

// 3. Third-party libraries
#include <rtaudio/RtAudio.h>

// 4. Standard library
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
```

## Header Guards

Use `#pragma once`:

```cpp
#pragma once

#include <cstdint>

namespace Aestra {
namespace audio {

class AudioEngine {
    // ...
};

} // namespace audio
} // namespace Aestra
```

## Class Structure

Organize class members in this order:

```cpp
class AudioEngine {
public:
    // Types and enums
    enum class State { Stopped, Playing, Paused };

    // Constructors / destructor
    AudioEngine();
    ~AudioEngine();

    // Public interface
    bool initialize(const AudioConfig& config);
    void start();
    void stop();

protected:
    // Protected members

private:
    // Private methods
    void processAudio(float* buffer, int frames);

    // Private data members
    State state_ = State::Stopped;
    uint32_t sampleRate_ = 48000;
};
```

## Real-Time Audio Constraints

The audio thread has strict requirements:

- **No allocations** — No `new`, `malloc`, `std::vector::push_back`, or `std::string` operations
- **No locks** — No `std::mutex::lock()`, use lock-free structures instead
- **No system calls** — No file I/O, no `printf`, no network
- **No exceptions** — Use return codes or `std::optional`

```cpp
// Good: Lock-free, allocation-free audio callback
void AudioEngine::processAudio(float* buffer, int frames) {
    // Read commands from lock-free queue
    AudioCommand cmd;
    while (commandQueue_.pop(cmd)) {
        handleCommand(cmd);  // Must also be RT-safe
    }

    // Process audio (no allocations, no locks)
    mixer_.mix(buffer, frames);
}
```

## Error Handling

- Use `bool` for simple success/failure
- Use `std::optional` for nullable returns
- Use `std::expected` or error codes for detailed errors
- No exceptions in real-time paths

```cpp
std::optional<AudioDevice> getDefaultDevice();
bool initialize(const AudioConfig& config);
```

## Documentation

Use `/** */` Doxygen-style comments for public APIs:

```cpp
/**
 * @brief Initialize the audio engine with the given configuration.
 * @param config Audio configuration (sample rate, buffer size, etc.)
 * @return true on success, false if the audio device cannot be opened.
 *
 * Must be called before start(). If initialization fails, check
 * lastError() for details.
 */
bool initialize(const AudioConfig& config);
```

## Commit Messages

Follow conventional commits:

```
type(scope): subject

body (optional)

Closes #issue
```

Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`

Examples:
```
feat(audio): add ASIO exclusive mode support
fix(ui): resolve click-through on dropdown menus
docs: update architecture overview for plugin system
```

## Pre-Commit Hooks

Install hooks to enforce style automatically:

```powershell
# Windows
pwsh -File scripts/install-hooks.ps1
```

```bash
# Linux
bash scripts/install-hooks.sh
```

The hooks run:
- `clang-format` on all staged C++ files
- Gitleaks secret scanning
- Basic validation checks

## Resources

- `.clang-format` — Full formatting rules
- `.clang-tidy` — Static analysis configuration
- [Contributing Guide](contributing.md) — Development workflow
- [Architecture Overview](../architecture/overview.md) — System design

---

*Last updated: March 2026*
