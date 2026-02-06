# 🐛 Debugging Guide

This guide covers tools and techniques for debugging Aestra DAW.

## 📋 Table of Contents

- [Logging](#-logging)
- [Visual Studio Debugger](#-visual-studio-debugger)
- [Aestra Profiler](#-aestra-profiler)
- [Common Issues](#-common-issues)

## 📝 Logging

Aestra uses a custom logging system defined in `AestraCore/include/Log.h`.

### Log Levels

- `Debug`: Verbose information for development
- `Info`: General application flow
- `Warning`: Potential issues
- `Error`: Critical failures

### Usage

```cpp
#include "Log.h"

void myFunction() {
    Log::info("Starting function...");

    if (error) {
        Log::error("Failed to load: " + filename);
    }
}
```

### Log Files

Logs are written to:
- **Windows:** `%APPDATA%\Aestra\logs\Aestra.log`
- **Linux:** `~/.Aestra/logs/Aestra.log`

## 🐞 Visual Studio Debugger

### Recommended Setup

1.  Select **Debug** build configuration.
2.  Enable **Break on Exceptions** (Ctrl+Alt+E).
3.  Use **Data Breakpoints** to catch memory corruption.

## ⏱️ Aestra Profiler

Aestra includes a built-in profiler for real-time performance analysis.

### Enabling Profiler

Set `Aestra_PROFILE_ENABLED` in CMake or `AestraConfig.h`.

```bash
cmake -DAestra_PROFILE_ENABLED=ON ...
```

### Viewing Data

Press **F12** to toggle the debug overlay, which shows:
- FPS
- Audio callback time
- Memory usage

## ❓ Common Issues

### Audio Dropouts

- Check buffer size (increase to 512/1024).
- Ensure Release build for performance testing.
- Verify ASIO driver settings.

### UI Lag

- Check if running in Debug mode (slower).
- Verify GPU drivers are up to date.

---

*Last updated: February 2026*
