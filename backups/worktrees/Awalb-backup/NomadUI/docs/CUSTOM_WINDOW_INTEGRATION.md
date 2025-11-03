# Nomad Custom Window Integration Guide

## Overview

The Nomad Custom Window provides a modern, borderless window experience with a custom title bar, similar to VS Code, Discord, and other modern applications.

## Features

✅ **Pixel-Perfect Borderless Window** - No visible borders or padding
✅ **Custom Title Bar** - Integrated with window background
✅ **Window Controls** - Minimize, maximize/restore, close with hover effects
✅ **Drag to Move** - Click and drag the title bar
✅ **Resize from Edges** - 8px resize borders on all sides
✅ **Full-Screen Support** - Press F11 to toggle
✅ **High-DPI Support** - Crisp rendering on all displays
✅ **Anti-Aliased Icons** - Modern, thin icons with smooth edges

## Quick Start

### Basic Usage

```cpp
#include "Core/NUICustomWindow.h"
#include "Platform/Windows/NUIWindowWin32.h"
#include "Graphics/OpenGL/NUIRendererGL.h"

using namespace NomadUI;

int main() {
    // Create platform window (starts maximized by default - DAW behavior)
    NUIWindowWin32 window;
    if (!window.create("My Nomad App", 1200, 800)) {
        return -1;
    }
    
    // Or start windowed:
    // if (!window.create("My Nomad App", 1200, 800, false)) {
    //     return -1;
    // }
    
    // Create renderer
    NUIRendererGL renderer;
    if (!renderer.initialize(1200, 800)) {
        return -1;
    }
    
    // Create custom window with your content
    auto customWindow = std::make_shared<NUICustomWindow>();
    customWindow->setTitle("My Nomad App");
    customWindow->setBounds(NUIRect(0, 0, 1200, 800));
    
    // Add your content
    auto myContent = std::make_shared<MyContentComponent>();
    customWindow->setContent(myContent.get());
    
    // Connect to platform window
    customWindow->setWindowHandle(&window);
    window.setRootComponent(customWindow.get());
    window.setRenderer(&renderer);
    window.show();
    
    // Main loop
    while (window.processEvents()) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        renderer.beginFrame();
        customWindow->onRender(renderer);
        renderer.endFrame();
        
        window.swapBuffers();
    }
    
    return 0;
}
```

## Customization

### Title Bar Height

```cpp
customWindow->getTitleBar()->setHeight(40.0f); // Default is 32px
```

### Window Controls Callbacks

```cpp
auto titleBar = customWindow->getTitleBar();

titleBar->setOnMinimize([]() {
    std::cout << "Minimize clicked" << std::endl;
});

titleBar->setOnMaximize([]() {
    std::cout << "Maximize clicked" << std::endl;
});

titleBar->setOnClose([]() {
    std::cout << "Close clicked" << std::endl;
});
```

### Full-Screen Mode

```cpp
// Toggle full-screen
customWindow->toggleFullScreen();

// Check if full-screen
if (customWindow->isFullScreen()) {
    // ...
}

// Exit full-screen
customWindow->exitFullScreen();
```

## Architecture

### Components

1. **NUIWindowWin32** - Platform-specific window (Win32 API)
   - Handles OS window creation
   - Processes events (mouse, keyboard, resize)
   - Manages OpenGL context

2. **NUICustomWindow** - High-level window wrapper
   - Manages title bar and content area
   - Handles full-screen transitions
   - Coordinates layout

3. **NUICustomTitleBar** - Title bar component
   - Renders title text
   - Draws window control buttons
   - Handles hover effects
   - Processes button clicks

### Window Styles

The window uses these Win32 styles for borderless functionality:
- `WS_POPUP` - No default borders
- `WS_MAXIMIZEBOX` - Enable maximize
- `WS_MINIMIZEBOX` - Enable minimize
- `WS_SYSMENU` - Enable system menu (Alt+Space, taskbar menu)
- **No `WS_THICKFRAME`** - Prevents invisible borders

### DWM Configuration

```cpp
// Margins set to {0,0,0,0} for flush appearance
MARGINS margins = {0, 0, 0, 0};
DwmExtendFrameIntoClientArea(hwnd, &margins);

// Shadows disabled for clean look
DWMNCRENDERINGPOLICY policy = DWMNCRP_DISABLED;
DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
```

### Custom Hit-Testing

The window implements custom `WM_NCHITTEST` handling:
- **Resize borders**: 8px on all edges (when not maximized)
- **Title bar drag**: Top 32px (excluding right 150px for controls)
- **Window controls**: Right 138px (3 buttons × 46px)

## Icon Rendering

### Current Implementation

Icons use OpenGL line and shape primitives with anti-aliasing:

```cpp
// Enable in renderer initialization
glEnable(GL_LINE_SMOOTH);
glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
glEnable(GL_POLYGON_SMOOTH);
glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
```

### Icon Specifications

- **Line thickness**: 1.0px (crisp on high-DPI)
- **Icon size**: 35% of button size
- **Colors**:
  - Default: Theme text color
  - Hover (minimize/maximize): 10% white overlay
  - Hover (close): Red background (#E63939) with white icon

### Future Improvements

For even crisper icons, consider:

1. **SDF (Signed Distance Field) Icons**
   - Pre-render icons as distance field textures
   - Perfect scaling at any size
   - Used by VS Code, Figma

2. **Vector Path Rendering**
   - Use NV_path_rendering or similar
   - True vector rendering in GPU
   - Sub-pixel precision

3. **Icon Fonts**
   - Use icon fonts (like Font Awesome)
   - Leverage existing text rendering
   - Easy to customize

## Theme Integration

The custom window uses these theme colors:

```cpp
auto& theme = NUIThemeManager::getInstance();

// Title bar background (flush with window)
NUIColor bgColor = theme.getColor("background");

// Title text
NUIColor textColor = theme.getColor("text");

// Window size info
NUIColor accentColor = theme.getColor("accent");
```

## Best Practices

### 1. Always Set Window Handle

```cpp
customWindow->setWindowHandle(&window);
```

This connects the custom window to the platform window for proper maximize/minimize/close functionality.

### 2. Handle Resize Events

```cpp
window.setResizeCallback([&](int width, int height) {
    customWindow->setBounds(NUIRect(0, 0, width, height));
    customWindow->onResize(width, height);
});
```

### 3. Update Content on Full-Screen Toggle

```cpp
// In your content component
void onRender(NUIRenderer& renderer) override {
    if (customWindow_->isFullScreen()) {
        // Adjust layout for full-screen
    } else {
        // Normal windowed layout
    }
}
```

### 4. DPI Awareness

The window sets DPI awareness automatically:

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

This ensures crisp rendering on high-DPI displays.

## Troubleshooting

### Issue: Window has visible borders

**Solution**: Ensure `WS_THICKFRAME` is NOT in the window style. Use only:
```cpp
WS_POPUP | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU
```

### Issue: Content doesn't fill window when maximized

**Solution**: Check `WM_NCCALCSIZE` handling. It should set client area = work area when maximized.

### Issue: Icons look pixelated

**Solution**: 
1. Ensure line smoothing is enabled in renderer
2. Use thinner lines (1.0px instead of 1.5px+)
3. Check DPI scaling is correct

### Issue: Window controls don't respond

**Solution**: Verify `WM_NCHITTEST` returns `HTCLIENT` for the content area and doesn't interfere with button clicks.

## Platform Support

Currently supported:
- ✅ Windows 10/11 (Win32 API)

Planned:
- 🔄 macOS (Cocoa)
- 🔄 Linux (X11/Wayland)

## Examples

See `NomadUI/Examples/CustomWindowDemo.cpp` for a complete working example.

## API Reference

### NUICustomWindow

```cpp
class NUICustomWindow : public NUIComponent {
public:
    void setTitle(const std::string& title);
    void setContent(NUIComponent* content);
    void setWindowHandle(NUIWindowWin32* window);
    void setMaximized(bool maximized);
    
    void toggleFullScreen();
    void enterFullScreen();
    void exitFullScreen();
    bool isFullScreen() const;
    
    NUICustomTitleBar* getTitleBar() const;
};
```

### NUICustomTitleBar

```cpp
class NUICustomTitleBar : public NUIComponent {
public:
    void setTitle(const std::string& title);
    void setHeight(float height);
    void setMaximized(bool maximized);
    
    void setOnMinimize(std::function<void()> callback);
    void setOnMaximize(std::function<void()> callback);
    void setOnClose(std::function<void()> callback);
    
    float getHeight() const;
    bool isMaximized() const;
};
```

## License

Part of the NomadUI framework.
