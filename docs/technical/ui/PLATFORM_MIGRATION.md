# Platform Layer Migration - NomadUI to NomadPlat

## Overview

NomadUI has been successfully migrated from using its own Windows-specific platform code to using the unified **NomadPlat** platform abstraction layer. This provides better code organization, cross-platform support, and eliminates code duplication.

## What Changed

### Removed Files
The following Windows-specific files have been removed from NomadUI:

- `NomadUI/Platform/Windows/NUIWindowWin32.h` - Old Windows window implementation
- `NomadUI/Platform/Windows/NUIWindowWin32.cpp` - Old Windows window implementation
- `NomadUI/Platform/Windows/NUIDPIHelper.h` - Old DPI helper (now in NomadPlat)
- `NomadUI/Platform/Windows/NUIDPIHelper.cpp` - Old DPI helper (now in NomadPlat)
- `NomadUI/Platform/Windows/NUIPlatformWindows.h` - Old platform header
- `NomadUI/Platform/Windows/NUIWindowWin32_Compat.h` - Old compatibility header

### New Architecture

```
Before:
NomadUI/Platform/Windows/NUIWindowWin32 → Win32 API directly

After:
NomadUI/Platform/NUIPlatformBridge → NomadPlat/IPlatformWindow → Win32/X11/Cocoa
```

### Benefits

1. **Unified Platform Layer**: All platform-specific code is now in NomadPlat
2. **Cross-Platform Ready**: Easy to add Linux and macOS support
3. **DPI Support**: Comprehensive DPI awareness built into the platform layer
4. **Code Reuse**: Other projects can use NomadPlat without NomadUI
5. **Cleaner Separation**: UI code is separate from platform code

## Migration Guide for Existing Code

### Old Code (Direct NUIWindowWin32)
```cpp
#include "Platform/Windows/NUIWindowWin32.h"

NUIWindowWin32 window;
window.create("My App", 800, 600);
```

### New Code (NUIPlatformBridge)
```cpp
#include "Platform/NUIPlatformBridge.h"

NomadUI::NUIPlatformBridge window;
window.create("My App", 800, 600);
```

### Compatibility Typedef
For backward compatibility, you can use:
```cpp
#include "Platform/NUIPlatformBridge.h"
using NUIWindowWin32 = NomadUI::NUIPlatformBridge;  // Compatibility

// Now old code works without changes
NUIWindowWin32 window;
window.create("My App", 800, 600);
```

## API Compatibility

The `NUIPlatformBridge` API is designed to be compatible with the old `NUIWindowWin32` API:

| Old API | New API | Status |
|---------|---------|--------|
| `create(title, width, height)` | `create(title, width, height)` | ✅ Compatible |
| `show()` | `show()` | ✅ Compatible |
| `hide()` | `hide()` | ✅ Compatible |
| `processEvents()` | `processEvents()` | ✅ Compatible |
| `swapBuffers()` | `swapBuffers()` | ✅ Compatible |
| `setTitle()` | `setTitle()` | ✅ Compatible |
| `setSize()` | `setSize()` | ✅ Compatible |
| `maximize()` | `maximize()` | ✅ Compatible |
| `minimize()` | `minimize()` | ✅ Compatible |
| `toggleFullScreen()` | `toggleFullScreen()` | ✅ Compatible |
| N/A | `getDPIScale()` | ✨ New Feature |
| N/A | `setDPIChangeCallback()` | ✨ New Feature |

## CMakeLists Changes

### Before
```cmake
add_executable(MyApp
    main.cpp
    Platform/Windows/NUIWindowWin32.cpp  # Had to include manually
)

target_link_libraries(MyApp PRIVATE
    NomadUI_Core
    opengl32
)
```

### After
```cmake
add_executable(MyApp
    main.cpp
    # No platform files needed!
)

target_link_libraries(MyApp PRIVATE
    NomadUI_Core
    NomadUI_Platform  # Links to NomadPlat automatically
    opengl32
)
```

## Examples Updated

All NomadUI examples have been updated to use the new platform layer:

- ✅ WindowDemo
- ✅ ButtonLabelDemo
- ✅ NewComponentsDemo
- ✅ CustomWindowDemo
- ✅ FullScreenDemo
- ✅ IconDemo
- ✅ SimpleRenderTest

## Testing

All examples build and run successfully with the new platform layer. No functionality was lost in the migration.

### Build Test
```bash
cmake --build build --config Debug --target NomadUI_CustomWindowDemo
cmake --build build --config Debug --target NomadUI_WindowDemo
```

Both build successfully without errors.

## Future Enhancements

With the unified platform layer, we can now easily add:

- [ ] Linux support (X11/Wayland)
- [ ] macOS support (Cocoa)
- [ ] Vulkan rendering backend
- [ ] Multi-window support
- [ ] Advanced DPI handling
- [ ] Platform-specific optimizations

## Notes

- The old Windows directory is completely removed
- All DPI functionality is now in `NomadPlat/src/Win32/PlatformDPIWin32`
- Platform initialization happens automatically when creating a window
- No breaking changes for existing code using the compatibility typedef

## Related Documentation

- [DPI Support Guide](../../NomadPlat/docs/DPI_SUPPORT.md)
- [NomadPlat Platform Interface](../../NomadPlat/include/NomadPlatform.h)
- [NUIPlatformBridge API](../Platform/NUIPlatformBridge.h)
