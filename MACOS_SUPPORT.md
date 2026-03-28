# macOS Support for Aestra

This document describes the macOS platform support that has been implemented for Aestra.

## Status: ✅ Core Implementation Complete

The macOS platform layer, audio driver, and build system integration are fully implemented.

## What Was Implemented

### 1. Platform Layer (`AestraPlat/src/macOS/`)

| File | Description |
|------|-------------|
| `PlatformWindowmacOS.h/cpp` | SDL2-based window implementation with OpenGL 3.3 Core support, Retina DPI handling |
| `PlatformUtilsmacOS.h/cpp` | Platform utilities using native macOS APIs (mach time, sysinfo) |
| `PlatformThreadmacOS.cpp` | Thread priority management with realtime audio support |

**Features:**
- ✅ Full SDL2 window lifecycle (create, destroy, events)
- ✅ OpenGL 3.3 Core context creation
- ✅ Retina display DPI scaling support
- ✅ Keyboard and mouse input with proper key mapping
- ✅ UTF-8 text input support
- ✅ macOS-specific SDL hints (Ctrl+Click = Right Click, Spaces fullscreen)
- ✅ High-resolution timer using mach_absolute_time()
- ✅ System info (CPU count, memory) via sysctl
- ✅ App data path in ~/Library/Application Support/

### 2. Audio Driver (`AestraAudio/src/macOS/`)

| File | Description |
|------|-------------|
| `RtAudioDriver.h/cpp` | CoreAudio backend via RtAudio |

**Features:**
- ✅ CoreAudio backend integration
- ✅ Device enumeration
- ✅ Non-interleaved audio buffer handling
- ✅ Realtime thread priority for audio callback
- ✅ Configurable sample rates and buffer sizes

### 3. Build System Updates

**Files Modified:**
- `CMakeLists.txt` - SDL2 detection for macOS
- `AestraPlat/CMakeLists.txt` - macOS platform sources and frameworks
- `AestraAudio/CMakeLists.txt` - macOS audio backend library
- `AestraPlat/src/Platform.cpp` - Factory integration

**macOS Frameworks Linked:**
- Cocoa
- OpenGL
- IOKit
- CoreFoundation
- CoreAudio (for audio)

## Build Instructions

### Prerequisites

```bash
# Install dependencies via Homebrew
brew install cmake sdl2 freetype
```

### Configure and Build

```bash
cd Aestra

# Configure
cmake -B build -DAESTRA_ENABLE_TESTS=OFF

# Build everything
cmake --build build -j$(sysctl -n hw.ncpu)

# Or build specific targets
cmake --build build --target AestraHeadless  # Headless only
cmake --build build --target AestraPlat      # Platform layer
cmake --build build --target AestraAudioMacOS # Audio driver
```

## Verified Builds

| Library | Status | Size |
|---------|--------|------|
| `libAestraPlat.a` | ✅ Built | 263 KB |
| `libAestraAudioCore.a` | ✅ Built | 10.9 MB |
| `libAestraAudioMacOS.a` | ✅ Built | 578 KB |
| `AestraHeadless` | ✅ Built | Executable |

## Architecture

The macOS implementation follows the same abstraction patterns as Windows and Linux:

```
┌─────────────────────────────────────┐
│           Aestra App                │
├─────────────────────────────────────┤
│  AestraUI (OpenGL 3.3 Core)         │
├─────────────────────────────────────┤
│  AestraPlat (macOS/SDL2)            │
│  - PlatformWindowmacOS              │
│  - PlatformUtilsmacOS               │
│  - PlatformThreadmacOS              │
├─────────────────────────────────────┤
│  AestraAudio (CoreAudio/RtAudio)    │
│  - RtAudioDriver (MACOSX_CORE)      │
├─────────────────────────────────────┤
│  AestraCore                         │
└─────────────────────────────────────┘
```

## Known Limitations

1. **File Dialogs**: Not yet implemented (return empty strings). Native Cocoa file dialogs need to be added to `PlatformUtilsmacOS`.

2. **VST3/CLAP Plugins**: VST3 SDK and CLAP SDK not included in the repository. Plugin hosting is disabled on macOS until these are added.

3. **Main Application**: The main Aestra DAW application has some API mismatches with TrackManager that are unrelated to macOS support.

## Next Steps (Optional Enhancements)

1. Implement native Cocoa file dialogs (NSOpenPanel/NSSavePanel)
2. Add VST3 plugin hosting support
3. Test on Apple Silicon (M1/M2/M3) - currently built for x86_64
4. Add CoreAudio exclusive mode support if needed
5. Implement native macOS menu bar integration

## Technical Notes

### SDL2 vs Cocoa
The implementation uses SDL2 for windowing (consistent with Linux) rather than native Cocoa. This provides:
- Consistent code with Linux platform
- Easier maintenance
- Cross-platform event handling
- Built-in Retina display support

Future versions could add a native Cocoa backend for deeper macOS integration.

### OpenGL on macOS
macOS only supports OpenGL 3.3 Core Profile (not Compatibility Profile). The implementation correctly requests:
- Core Profile (not Compatibility)
- Forward-compatible context
- No deprecated features

### Audio Backend
RtAudio uses CoreAudio on macOS, which provides:
- Low-latency audio I/O
- Integration with system audio preferences
- Automatic sample rate conversion
- Multi-device support

## License

The macOS implementation follows the same ASSAL v1.1 license as the rest of Aestra.
