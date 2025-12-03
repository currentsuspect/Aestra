# DPI Support in NomadPlat

## Overview

NomadPlat now includes comprehensive DPI (Dots Per Inch) awareness support for Windows, enabling applications to render correctly on high-DPI displays with proper scaling.

## Features

### 1. DPI Awareness Initialization

- Automatically sets DPI awareness when `Platform::initialize()` is called
- Supports multiple DPI awareness modes with fallback:
  - **Per-Monitor V2** (Windows 10 1703+) - Best support, handles non-client area scaling
  - **Per-Monitor V1** (Windows 8.1+) - Good support for per-monitor DPI
  - **System DPI Aware** (Windows Vista+) - Basic DPI awareness
  - Graceful fallback if DPI awareness cannot be set

### 2. DPI Scale Detection

- `getDPIScale()` method returns the current DPI scale factor
  - 1.0 = 96 DPI (100% scaling)
  - 1.5 = 144 DPI (150% scaling)
  - 2.0 = 192 DPI (200% scaling)
- `getDPI()` method returns the raw DPI value

### 3. DPI Change Notifications

- `WM_DPICHANGED` message handling
- Automatic window resizing when moved between monitors with different DPI
- Callback support via `setDPIChangeCallback()`
- Applications can respond to DPI changes in real-time

### 4. DPI Utility Functions

- `PlatformDPI::scale(value, dpiScale)` - Scale a value by DPI
- `PlatformDPI::unscale(value, dpiScale)` - Unscale a value by DPI

## Implementation Details

### Files Added

- `NomadPlat/src/Win32/PlatformDPIWin32.h` - DPI helper class interface
- `NomadPlat/src/Win32/PlatformDPIWin32.cpp` - DPI implementation
- `NomadPlat/src/Tests/PlatformDPITest.cpp` - DPI test application

### Files Modified

- `NomadPlat/CMakeLists.txt` - Added DPI files to build
- `NomadPlat/src/Platform.cpp` - Initialize DPI awareness on startup
- `NomadPlat/include/NomadPlatform.h` - Added `getDPIScale()` and `setDPIChangeCallback()`
- `NomadPlat/src/Win32/PlatformWindowWin32.h` - Added DPI tracking
- `NomadPlat/src/Win32/PlatformWindowWin32.cpp` - Handle `WM_DPICHANGED` messages

### NomadUI Integration

- `NomadUI/Platform/NUIPlatformBridge.h` - Added `getDPIScale()` method
- `NomadUI/Platform/NUIPlatformBridge.cpp` - Forward DPI changes to renderer

## Usage Example

```cpp
#include "NomadPlatform.h"

using namespace Nomad;

int main() {
    // Initialize platform (sets DPI awareness)
    Platform::initialize();
    
    // Create window
    IPlatformWindow* window = Platform::createWindow();
    WindowDesc desc;
    desc.title = "My App";
    desc.width = 800;
    desc.height = 600;
    window->create(desc);
    
    // Get current DPI scale
    float dpiScale = window->getDPIScale();
    std::cout << "DPI Scale: " << dpiScale << std::endl;
    
    // Set up DPI change callback
    window->setDPIChangeCallback([](float newScale) {
        std::cout << "DPI changed to: " << newScale << std::endl;
        // Update UI scaling, fonts, etc.
    });
    
    // Main loop
    while (window->pollEvents()) {
        // Render...
    }
    
    // Cleanup
    window->destroy();
    delete window;
    Platform::shutdown();
    
    return 0;
}
```

## Testing

Run the DPI test application:

```bash
.\build\NomadPlat\Debug\PlatformDPITest.exe
```

The test will:

1. Display the current DPI scale
2. Show logical vs physical window size
3. Allow you to move the window between monitors with different DPI settings
4. Log DPI changes in real-time

## Platform Support

### Windows

- ✅ Full support with Per-Monitor V2 awareness
- ✅ Automatic window resizing on DPI change
- ✅ Real-time DPI change notifications

### Linux

- ⏳ Planned (X11 DPI detection)

### macOS

- ⏳ Planned (Retina display support)

## Best Practices

1. **Always initialize Platform before creating windows**

   ```cpp
   Platform::initialize();  // Sets DPI awareness
   ```

2. **Use DPI scale for UI elements**

   ```cpp
   float scale = window->getDPIScale();
   int buttonWidth = PlatformDPI::scale(100, scale);
   ```

3. **Handle DPI changes**

   ```cpp
   window->setDPIChangeCallback([this](float newScale) {
       // Reload fonts at new size
       // Rescale UI elements
       // Trigger redraw
   });
   ```

4. **Test on multiple DPI settings**
   - 100% (96 DPI)
   - 125% (120 DPI)
   - 150% (144 DPI)
   - 200% (192 DPI)

## Known Limitations

1. DPI awareness is set process-wide and cannot be changed after initialization
2. Some Windows controls may not scale properly in mixed-DPI scenarios
3. Font rendering quality may vary at non-integer scale factors

## Future Enhancements

- [ ] Per-window DPI awareness (Windows 10 1803+)
- [ ] DPI-aware font rendering
- [ ] Automatic UI element scaling
- [ ] DPI-aware image loading (load different resolution assets)
- [ ] Linux X11 DPI support
- [ ] macOS Retina display support
