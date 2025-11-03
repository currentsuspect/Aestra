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
- ⏳ **Linux** (X11) - Planned
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

## Future Enhancements

- Linux X11 implementation
- macOS Cocoa implementation
- Vulkan context support
- Multi-monitor support
- Custom window decorations
- Drag-and-drop file support
- IME (Input Method Editor) support for international text input
