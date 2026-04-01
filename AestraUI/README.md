# 🎨 Aestra UI Framework

A modern, GPU-accelerated UI framework built from scratch for the Aestra.

## Vision

Replace JUCE's component system with a fully custom, lightweight, and ultra-responsive UI engine that feels fast and responsive, but more modular and artistically fluid.

## Features

### ⚡ Performance
- **60-144Hz refresh rate** - Buttery smooth animations
- **GPU-accelerated rendering** - OpenGL 3.3+ (Vulkan coming soon)
- **Batched draw calls** - Optimized rendering pipeline
- **Dirty rectangle optimization** - Only redraw what changed
- **Texture caching** - Static elements cached as textures

### 🎨 Visual Design
- **Modern dark theme** - Professional aesthetics
- **Smooth animations** - Easing curves and transitions
- **Glow effects** - Shader-based visual feedback
- **Vector-based** - Crisp at any resolution
- **Responsive layouts** - Flexbox-like system

### 🏗️ Architecture
- **Zero JUCE dependencies** - Pure C++17+
- **Modular design** - Independent compilation units
- **Cross-platform** - Windows, macOS, Linux
- **Theme system** - JSON-based customization
- **Event-driven** - Efficient event propagation

## Project Structure

```
AestraUI/
├── Core/                   # Core framework
│   ├── NUITypes.h         # Basic types and structures
│   ├── NUIComponent.h/cpp # Base component class
│   ├── NUITheme.h/cpp     # Theme system
│   └── NUIApp.h/cpp       # Application lifecycle
├── Graphics/              # Rendering backend
│   ├── NUIRenderer.h      # Renderer interface
│   ├── OpenGL/            # OpenGL implementation
│   └── Vulkan/            # Vulkan implementation (future)
├── Layout/                # Layout engine
│   ├── NUILayout.h        # Layout interface
│   ├── NUIFlexLayout.h    # Flexbox layout
│   └── NUIGridLayout.h    # Grid layout
├── Widgets/               # UI controls
│   ├── NUIButton.h        # Button widget
│   ├── NUISlider.h        # Slider widget
│   ├── NUIKnob.h          # Knob widget
│   └── ...                # More widgets
└── Platform/              # Platform abstraction
    ├── Win32/             # Windows implementation
    ├── Cocoa/             # macOS implementation
    └── X11/               # Linux implementation
```

## Quick Start

### 1. Create a Simple Application

```cpp
#include "AestraUI/Core/NUIApp.h"
#include "AestraUI/Core/NUIComponent.h"
#include "AestraUI/Core/NUITheme.h"

using namespace AestraUI;

class MyComponent : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        auto theme = getTheme();
        
        // Draw background
        renderer.fillRoundedRect(
            getBounds(),
            theme->getBorderRadius(),
            theme->getSurface()
        );
        
        // Draw text
        renderer.drawTextCentered(
            "Hello, Aestra UI!",
            getBounds(),
            theme->getFontSizeTitle(),
            theme->getText()
        );
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        if (event.pressed) {
            // Handle click
            return true;
        }
        return false;
    }
};

int main() {
    NUIApp app;
    
    // Initialize
    if (!app.initialize(800, 600, "My Aestra App")) {
        return 1;
    }
    
    // Create root component
    auto root = std::make_shared<MyComponent>();
    root->setBounds(0, 0, 800, 600);
    root->setTheme(NUITheme::createDefault());
    app.setRootComponent(root);
    
    // Run
    app.run();
    
    return 0;
}
```

### 2. Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .
```

## Core Concepts

### Components

Everything is a component. Components can:
- Render themselves
- Handle events
- Contain child components
- Be styled with themes

```cpp
class MyWidget : public NUIComponent {
public:
    void onRender(NUIRenderer& renderer) override {
        // Custom rendering
    }
    
    bool onMouseEvent(const NUIMouseEvent& event) override {
        // Handle mouse input
        return true; // Event handled
    }
};
```

### Rendering

The renderer provides a high-level API for drawing:

```cpp
// Primitives
renderer.fillRect(rect, color);
renderer.fillRoundedRect(rect, radius, color);
renderer.fillCircle(center, radius, color);

// Gradients
renderer.fillRectGradient(rect, colorStart, colorEnd, vertical);

// Effects
renderer.drawGlow(rect, radius, intensity, color);
renderer.drawShadow(rect, offsetX, offsetY, blur, color);

// Text
renderer.drawText(text, position, fontSize, color);
renderer.drawTextCentered(text, rect, fontSize, color);
```

### Themes

Themes provide centralized styling:

```cpp
auto theme = NUITheme::createDefault();

// Colors
auto primary = theme->getPrimary();
auto background = theme->getBackground();

// Dimensions
auto radius = theme->getBorderRadius();
auto padding = theme->getPadding();

// Effects
auto glowIntensity = theme->getGlowIntensity();
```

### Events

Events flow through the component hierarchy:

```cpp
// Mouse events
bool onMouseEvent(const NUIMouseEvent& event) override {
    if (event.pressed) {
        // Mouse button pressed
    }
    if (event.released) {
        // Mouse button released
    }
    return true; // Stop propagation
}

// Key events
bool onKeyEvent(const NUIKeyEvent& event) override {
    if (event.keyCode == NUIKeyCode::Enter) {
        // Handle Enter key
    }
    return true;
}
```

## Roadmap

### Phase 1: Foundation ✅
- [x] Core types and structures
- [x] Base component system
- [x] Renderer interface
- [x] Theme system
- [x] Application lifecycle

### Phase 2: OpenGL Backend (In Progress)
- [ ] OpenGL renderer implementation
- [ ] Shader-based primitives
- [ ] Text rendering (FreeType)
- [ ] Texture management
- [ ] Draw call batching

### Phase 3: Essential Widgets
- [ ] Button
- [ ] Slider
- [ ] Knob
- [ ] Label
- [ ] Panel

### Phase 4: Layout Engine
- [ ] Flexbox layout
- [ ] Grid layout
- [ ] Stack layout
- [ ] Responsive sizing

### Phase 5: DAW Widgets
- [ ] Waveform display
- [ ] Step sequencer grid
- [ ] Piano roll
- [ ] Mixer channel strip
- [ ] Transport controls

### Phase 6: Advanced Features
- [ ] Animation system
- [ ] Drag & drop
- [ ] Context menus
- [ ] Tooltips
- [ ] Keyboard navigation

### Phase 7: Vulkan Backend
- [ ] Vulkan renderer
- [ ] Compute shader effects
- [ ] Advanced post-processing

## Design Philosophy

### Performance First
Every frame matters. Target 60-144Hz with zero hitches.

### Feel & Responsiveness
Sub-10ms input latency. Every interaction should feel instant and tactile.

### Modularity
Independent modules that can be used separately or together.

### Beauty
Every pixel is intentional. The UI should feel alive and motion-driven.

## Dependencies

### Required
- **C++17 compiler** (MSVC, GCC, Clang)
- **OpenGL 3.3+** (for rendering)
- **CMake 3.15+** (for building)

### Optional
- **FreeType** (for text rendering)
- **stb_image** (for image loading)
- **Vulkan SDK** (for Vulkan backend)

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Windows  | ✅ Planned | Win32 + WGL |
| macOS    | ✅ Planned | Cocoa + NSOpenGLView |
| Linux    | ✅ Planned | X11/Wayland + GLX/EGL |

## Contributing

This is currently a solo project for the Aestra, but contributions are welcome!

## License

TBD

## Credits

Inspired by:
- **Modern Workflow** - Visual design and workflow
- **Dear ImGui** - Immediate mode concepts
- **Skia** - 2D graphics rendering
- **NanoVG** - Vector graphics API

---

**Built with ❤️ for music producers who deserve better tools.**
