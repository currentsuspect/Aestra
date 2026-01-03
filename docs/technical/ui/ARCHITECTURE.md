# 🎨 Nomad UI Framework - Architecture

## Vision
A modern, GPU-accelerated UI framework built from scratch for the Nomad DAW. Zero dependencies on JUCE's component system - pure C++17+ with OpenGL/Vulkan rendering.

## Core Philosophy

### Performance First
- 60-144Hz refresh rate target
- GPU-accelerated rendering with batched draw calls
- Cached textures for static elements
- Dirty rectangle optimization
- Zero-copy where possible

### Feel & Responsiveness
- Sub-10ms input latency
- Smooth animations with easing curves
- Tactile feedback on every interaction
- Motion-driven design language

### Modularity
- Independent compilation units
- Plugin-style widget system
- Theme engine with hot-reload
- Cross-platform from day one

## Architecture Layers

```
┌─────────────────────────────────────────────┐
│         Application Layer (Nomad DAW)       │
├─────────────────────────────────────────────┤
│      nomad_ui_widgets (Knobs, Sliders)      │
├─────────────────────────────────────────────┤
│     nomad_ui_layout (Flexbox, Grid)         │
├─────────────────────────────────────────────┤
│   nomad_ui_core (Components, Events, Render)│
├─────────────────────────────────────────────┤
│      nomad_ui_graphics (OpenGL/Vulkan)      │
├─────────────────────────────────────────────┤
│         Platform Layer (Window, Input)      │
└─────────────────────────────────────────────┘
```

## Module Breakdown

### 1. nomad_ui_graphics (Rendering Backend)
**Purpose:** Low-level GPU rendering abstraction

**Components:**
- `NUIRenderer` - Main rendering interface
- `NUIShader` - Shader compilation and management
- `NUITexture` - Texture loading and caching
- `NUIBatch` - Draw call batching
- `NUIFont` - Text rendering (FreeType integration)

**Features:**
- OpenGL 3.3+ backend (initial)
- Vulkan backend (future)
- Shader-based primitives (rounded rects, gradients, glows)
- SDF text rendering
- Framebuffer effects

### 2. nomad_ui_core (Component System)
**Purpose:** Base component architecture and event system

**Components:**
- `NUIComponent` - Base class for all UI elements
- `NUIEvent` - Event system (mouse, keyboard, focus)
- `NUIAnimator` - Animation curves and transitions
- `NUITheme` - Theme management
- `NUIApp` - Application lifecycle

**Key Classes:**

```cpp
class NUIComponent {
public:
    virtual void onRender(NUIRenderer& renderer) = 0;
    virtual void onMouseEvent(const NUIMouseEvent& event) {}
    virtual void onKeyEvent(const NUIKeyEvent& event) {}
    virtual void onResize(int width, int height) {}
    virtual void onUpdate(double deltaTime) {}
    
    // Layout
    void setBounds(int x, int y, int w, int h);
    NUIRect getBounds() const;
    
    // Hierarchy
    void addChild(std::shared_ptr<NUIComponent> child);
    void removeChild(std::shared_ptr<NUIComponent> child);
    
    // State
    void setVisible(bool visible);
    void setEnabled(bool enabled);
    void setFocused(bool focused);
    
protected:
    NUIRect bounds;
    std::vector<std::shared_ptr<NUIComponent>> children;
    NUIComponent* parent = nullptr;
    bool visible = true;
    bool enabled = true;
    bool focused = false;
};
```

### 3. nomad_ui_layout (Layout Engine)
**Purpose:** Flexbox-like layout system

**Components:**
- `NUILayout` - Base layout interface
- `NUIFlexLayout` - Flexbox implementation
- `NUIGridLayout` - Grid layout
- `NUIStackLayout` - Stack/overlay layout

**Features:**
- Automatic sizing and positioning
- Responsive scaling
- Margin/padding support
- Alignment and justification

### 4. nomad_ui_widgets (DAW Controls)
**Purpose:** Specialized widgets for music production

**Widgets:**
- `NUIKnob` - Rotary knob with value display
- `NUISlider` - Linear slider (horizontal/vertical)
- `NUIButton` - Push button with states
- `NUIToggle` - Toggle switch
- `NUIMeter` - Level meter (VU, peak)
- `NUIWaveform` - Audio waveform display
- `NUIPianoRoll` - Piano roll editor
- `NUIStepSequencer` - Step sequencer grid
- `NUITransport` - Transport controls
- `NUIMixer` - Mixer channel strip

## Rendering Pipeline

### Frame Lifecycle
```
1. Input Processing
   ├─ Poll platform events
   ├─ Dispatch to focused component
   └─ Update hover states

2. Update Phase
   ├─ Update animations
   ├─ Update component states
   └─ Mark dirty regions

3. Layout Phase
   ├─ Calculate component bounds
   ├─ Apply layout rules
   └─ Propagate to children

4. Render Phase
   ├─ Clear framebuffer
   ├─ Batch draw calls
   ├─ Render components (back to front)
   ├─ Apply post-effects
   └─ Swap buffers
```

### Draw Call Batching
```cpp
// Batch similar primitives together
renderer.beginBatch();
for (auto& component : visibleComponents) {
    if (component->isDirty()) {
        component->onRender(renderer);
    }
}
renderer.endBatch();
renderer.flush();
```

## Event System

### Event Flow
```
Platform Event
    ↓
NUIApp::processEvent()
    ↓
Event Capture Phase (root → target)
    ↓
Target Component
    ↓
Event Bubble Phase (target → root)
```

### Event Types
- `NUIMouseEvent` - Mouse move, click, drag, wheel
- `NUIKeyEvent` - Key press, release, text input
- `NUIFocusEvent` - Focus gain/loss
- `NUIResizeEvent` - Window/component resize
- `NUICustomEvent` - User-defined events

## Animation System

### Easing Functions
```cpp
enum class NUIEasing {
    Linear,
    EaseIn, EaseOut, EaseInOut,
    BounceIn, BounceOut,
    ElasticIn, ElasticOut,
    BackIn, BackOut
};

class NUIAnimator {
public:
    void animate(float& value, float target, 
                 float duration, NUIEasing easing);
    void update(double deltaTime);
};
```

### Use Cases
- Hover effects (color, scale, glow)
- Value changes (knob rotation, slider position)
- Panel transitions (slide, fade)
- Waveform scrolling

## Theme System

### Theme Definition (JSON)
```json
{
  "name": "Nomad Dark",
  "colors": {
    "background": "#0d0d0d",
    "surface": "#1a1a1a",
    "primary": "#a855f7",
    "secondary": "#3b82f6",
    "text": "#ffffff",
    "textSecondary": "#999999"
  },
  "dimensions": {
    "borderRadius": 4,
    "padding": 8,
    "margin": 4
  },
  "effects": {
    "glowIntensity": 0.3,
    "shadowBlur": 8
  }
}
```

### Theme Application
```cpp
auto theme = NUITheme::load("themes/dark.json");
component->setTheme(theme);
```

## Platform Integration

### Window Management
```cpp
class NUIPlatformWindow {
public:
    virtual void create(int width, int height, const char* title) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void setSize(int width, int height) = 0;
    virtual void* getNativeHandle() = 0;
    
    // Event callbacks
    std::function<void(NUIMouseEvent)> onMouseEvent;
    std::function<void(NUIKeyEvent)> onKeyEvent;
    std::function<void(int, int)> onResize;
};
```

### Platform Implementations
- **Windows:** Win32 API + WGL
- **macOS:** Cocoa + NSOpenGLView
- **Linux:** X11/Wayland + GLX/EGL

## Performance Optimizations

### 1. Dirty Rectangle Tracking
Only redraw regions that changed

### 2. Texture Caching
Cache rendered controls as textures

### 3. GPU Instancing
Render multiple identical elements in one draw call

### 4. Culling
Skip rendering for off-screen components

### 5. LOD (Level of Detail)
Simplify rendering for small/distant elements

## Migration Strategy

### Phase 1: Foundation (Week 1-2)
- Core rendering engine (OpenGL)
- Base component system
- Event system
- Basic shapes and text

### Phase 2: Widgets (Week 3-4)
- Essential controls (button, slider, knob)
- Layout engine
- Theme system

### Phase 3: DAW Integration (Week 5-6)
- Waveform display
- Step sequencer grid
- Piano roll
- Mixer strips

### Phase 4: Polish (Week 7-8)
- Animations
- Advanced effects
- Performance tuning
- Vulkan backend

## File Structure

```
NomadUI/
├── Core/
│   ├── NUIComponent.h/cpp
│   ├── NUIEvent.h/cpp
│   ├── NUIAnimator.h/cpp
│   ├── NUITheme.h/cpp
│   └── NUIApp.h/cpp
├── Graphics/
│   ├── NUIRenderer.h/cpp
│   ├── NUIShader.h/cpp
│   ├── NUITexture.h/cpp
│   ├── NUIBatch.h/cpp
│   └── NUIFont.h/cpp
├── Layout/
│   ├── NUILayout.h/cpp
│   ├── NUIFlexLayout.h/cpp
│   └── NUIGridLayout.h/cpp
├── Widgets/
│   ├── NUIButton.h/cpp
│   ├── NUISlider.h/cpp
│   ├── NUIKnob.h/cpp
│   └── ... (more widgets)
├── Platform/
│   ├── NUIPlatform.h
│   ├── Win32/
│   ├── Cocoa/
│   └── X11/
└── Shaders/
    ├── primitive.vert
    ├── primitive.frag
    ├── text.vert
    └── text.frag
```

## Next Steps

1. **Create base classes** (NUIComponent, NUIRenderer, NUIEvent)
2. **Implement OpenGL renderer** with basic primitives
3. **Build demo application** with sample window
4. **Add first widget** (NUIButton)
5. **Integrate with existing Nomad** (parallel to JUCE)

This architecture provides a solid foundation for a modern, performant UI framework that will give Nomad the visual and interactive quality it deserves! 🚀
