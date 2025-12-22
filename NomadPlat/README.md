# NomadPlat

Cross-platform abstraction layer for the NOMAD DAW project.

## Overview

NomadPlat provides a unified interface for platform-specific functionality:

- **Window Management** - Create and manage native windows
- **OpenGL Context** - Cross-platform OpenGL context creation
- **Input Events** - Mouse, keyboard, and window events
- **Platform Utilities** - File dialogs, clipboard, system info, high-resolution timer

## Supported Platforms

- ✅ **Windows** (Win32) - Fully implemented and tested
- 🚧 **Linux** (X11/SDL2) - Implemented and tested
- ⏳ **macOS** (Cocoa) - Planned

## Architecture

NomadPlat follows an interface-based design for easy platform switching:

```
IPlatformWindow (interface)
├── PlatformWindowWin32 (Windows implementation)
├── PlatformWindowX11 (Linux implementation - TODO)
└── PlatformWindowCocoa (macOS implementation - TODO)

IPlatformUtils (interface)
├── PlatformUtilsWin32 (Windows implementation)
├── PlatformUtilsX11 (Linux implementation - TODO)
└── PlatformUtilsCocoa (macOS implementation - TODO)
```

## Features

### Window Management
- Create/destroy windows with customizable properties
- Show/hide, minimize/maximize/restore
- Fullscreen support
- Window positioning and resizing
- Title bar customization

### OpenGL Context
- OpenGL context creation and management
- VSync control
- Context switching
- Double buffering

### Input Events
- Mouse move, button, and wheel events
- Keyboard events with modifiers
- Character input for text entry
- Window resize and focus events
- Close event handling

### Platform Utilities
- High-resolution timer (microsecond precision)
- File open/save dialogs
- Folder selection dialog
- Clipboard text operations
- System information (CPU count, memory, platform name)

## Usage

```cpp
#include <NomadPlatform.h>

using namespace Nomad;

int main() {
    // Initialize platform
    Platform::initialize();

    // Create window
    IPlatformWindow* window = Platform::createWindow();
    
    WindowDesc desc;
    desc.title = "NOMAD DAW";
    desc.width = 1280;
    desc.height = 720;
    
    window->create(desc);
    window->createGLContext();
    window->makeContextCurrent();

    // Setup event callbacks
    window->setKeyCallback([](KeyCode key, bool pressed, const KeyModifiers& mods) {
        if (key == KeyCode::Escape && pressed) {
            // Handle escape key
        }
    });

    window->setResizeCallback([](int width, int height) {
        // Handle window resize
    });

    // Main loop
    window->show();
    while (window->pollEvents()) {
        // Render frame
        window->swapBuffers();
    }

    // Cleanup
    window->destroy();
    delete window;
    Platform::shutdown();

    return 0;
}
```

## Platform Utilities

```cpp
// Get platform utilities
IPlatformUtils* utils = Platform::getUtils();

// High-resolution timer
double startTime = utils->getTime();
// ... do work ...
double elapsed = utils->getTime() - startTime;

// File dialogs
std::string filename = utils->openFileDialog("Open Project", "Project Files\0*.nomad\0");
std::string savePath = utils->saveFileDialog("Save Project", "Project Files\0*.nomad\0");
std::string folder = utils->selectFolderDialog("Select Samples Folder");

// Clipboard
utils->setClipboardText("Hello from NOMAD!");
std::string text = utils->getClipboardText();

// System info
std::cout << "Platform: " << utils->getPlatformName() << std::endl;
std::cout << "CPUs: " << utils->getProcessorCount() << std::endl;
std::cout << "Memory: " << (utils->getSystemMemory() / (1024*1024)) << " MB" << std::endl;
```

## Testing

```bash
cmake -B build -S .
cmake --build build --target PlatformWindowTest --config Release
./build/NomadPlat/Release/PlatformWindowTest.exe
```

## Design Philosophy

- **Interface-based** - Easy to add new platforms
- **Zero overhead** - Direct platform API calls, no unnecessary abstractions
- **Real-time safe** - Event polling doesn't allocate memory
- **Minimal dependencies** - Only platform SDKs required

## Windows Implementation Details

The Windows implementation uses:
- **Win32 API** for window management
- **WGL** for OpenGL context creation
- **Common Dialogs** for file/folder selection
- **QueryPerformanceCounter** for high-resolution timing

Key features:
- DPI awareness
- Fullscreen borderless window mode
- Proper key code translation
- Mouse capture for dragging
- Window snap zones (planned)

## Platform Implementation Checklist

### Required for All Platform Implementations

All platform window implementations MUST implement the following methods to prevent compile breaks:

#### Core Window Interface
- [x] `PlatformWindowWin32` - **Windows (Win32) - COMPLETE**
- [x] `PlatformWindowLinux` - **Linux (X11/SDL2) - COMPLETE**
  - [x] `setCursorVisible(bool visible)` - Implemented using SDL_ShowCursor()
  - [x] `getCurrentModifiers()` - Implemented using SDL_GetModState()
  - [x] All other IPlatformWindow pure virtual methods
- [ ] `PlatformWindowCocoa` - **macOS (Cocoa) - TODO**
  - [ ] `setCursorVisible(bool visible)` - Show/hide cursor immediately using Cocoa APIs
  - [ ] `getCurrentModifiers()` - Query modifier key state using NSEvent
  - [ ] All other IPlatformWindow pure virtual methods

#### Platform Utils Interface
- [x] `PlatformUtilsWin32` - **Windows (Win32) - COMPLETE**
- [x] `PlatformUtilsLinux` - **Linux (X11/SDL2) - COMPLETE**
- [ ] `PlatformUtilsCocoa` - **macOS (Cocoa) - TODO**

### Critical Implementation Notes

1. **setCursorVisible() Requirements:**
   - Must show/hide cursor immediately with no delay
   - Should persist across window state changes (minimize/restore)
   - **MUST be called from the same thread that created the window (window thread)**
   - Cross-thread calls cause cursor display count desynchronization due to ShowCursor()'s per-thread behavior
   - Reference implementations:
     - Windows: `PlatformWindowWin32::setCursorVisible()` (lines 377-400)
     - Linux: `PlatformWindowLinux::setCursorVisible()` (line 266-268)

2. **getCurrentModifiers() Requirements:**
   - Must return current state of Shift, Control, Alt, and Super keys
   - Used by mouse wheel event handlers for modifier-aware scrolling
   - Must be real-time accurate, not cached state
   - **MUST be called from the same thread that created the window (window thread)**
   - Cross-thread calls can cause inconsistent modifier state
   - Reference implementations:
     - Windows: `PlatformWindowWin32::getCurrentModifiers()` (lines 898-905)
     - Linux: `PlatformWindowLinux::getCurrentModifiers()` (line 270-272)

3. **Thread Safety Enforcement:**
   - Windows implementation includes `assertWindowThread()` checks
   - Debug builds will assert if called from wrong thread
   - Release builds allow calls but may exhibit undefined behavior
   - All platform implementations should enforce thread affinity

### TODO: macOS/Cocoa Implementation Required

When implementing `PlatformWindowCocoa`, ensure these methods are properly implemented:

```cpp
// TODO: Implement setCursorVisible for macOS
void PlatformWindowCocoa::setCursorVisible(bool visible) override {
    assertWindowThread(); // CRITICAL: Must enforce thread affinity
    // TODO: Use [NSCursor setHiddenUntilMouseMoves:] or similar Cocoa APIs
    // Must show/hide cursor immediately like Win32/Linux implementations
}

// TODO: Implement getCurrentModifiers for macOS
KeyModifiers PlatformWindowCocoa::getCurrentModifiers() const override {
    assertWindowThread(); // CRITICAL: Must enforce thread affinity
    // TODO: Use [NSEvent modifierFlags] to query current modifier state
    // Must return real-time accurate state like Win32/Linux implementations
}

// TODO: Add thread tracking to header
private:
    DWORD m_creatingThreadId; // Store creating thread for thread safety checks

// TODO: Initialize in constructor
PlatformWindowCocoa::PlatformWindowCocoa() {
    m_creatingThreadId = GetCurrentThreadId(); // Or Cocoa equivalent
}

// TODO: Add assertion method
void PlatformWindowCocoa::assertWindowThread() const {
    NOMAD_ASSERT(GetCurrentThreadId() == m_creatingThreadId,
        "PlatformWindowCocoa methods must be called from the same thread that created the window.");
}
```

## Future Enhancements

- [ ] Linux X11 implementation (PlatformWindowX11, PlatformUtilsX11)
- [ ] macOS Cocoa implementation (PlatformWindowCocoa, PlatformUtilsCocoa)
- Vulkan context support
- Multi-monitor support
- Custom window decorations
- Drag-and-drop file support
- IME (Input Method Editor) support for international text input
