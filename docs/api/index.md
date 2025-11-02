# API Reference

Welcome to the NOMAD DAW API reference. This section provides detailed documentation for all public APIs.

!!! tip "🚀 Full API Reference"
    **[Browse the Complete Doxygen API Reference →](../api-reference/html/index.html)**
    
    Comprehensive documentation for all classes, functions, and modules with searchable interface, inheritance diagrams, and detailed member descriptions.

## 📚 Module APIs

NOMAD's API is organized by module. Each module provides a focused set of functionality:

### Core Foundation

#### NomadCore API
Foundation utilities and data structures.

**Key APIs:**
- `nomad::Vec2`, `nomad::Vec3`, `nomad::Vec4` — Vector mathematics
- `nomad::Mat4` — Matrix operations
- `nomad::LockFreeQueue<T>` — Lock-free thread communication
- `nomad::ThreadPool` — Background task execution
- `nomad::File` — Cross-platform file I/O
- `nomad::Logger` — Structured logging

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/nomad-core.md) for high-level design concepts.

---

### Platform Abstraction

#### NomadPlat API
Platform-specific functionality with unified interface.

**Key APIs:**
- `nomad::Window` — Window creation and management
- `nomad::Input` — Keyboard and mouse input
- `nomad::FileDialog` — Native file dialogs
- `nomad::SystemInfo` — System capabilities
- `nomad::Timer` — High-resolution timing

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/nomad-plat.md) for high-level design concepts.

---

### User Interface

#### NomadUI API
GPU-accelerated UI framework.

**Key APIs:**
- `nomad::Renderer` — OpenGL rendering
- `nomad::Widget` — Base widget class
- `nomad::Button` — Button widget
- `nomad::Slider` — Slider widget
- `nomad::TextBox` — Text input widget
- `nomad::Layout` — Layout management
- `nomad::Theme` — Theme system

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/nomad-ui.md) for high-level design concepts.

---

### Audio Engine

#### NomadAudio API
Professional audio processing system.

**Key APIs:**
- `nomad::AudioEngine` — Audio engine management
- `nomad::AudioDevice` — Device enumeration
- `nomad::AudioBuffer` — Audio buffer management
- `nomad::DSP::Gain` — Gain processor
- `nomad::DSP::EQ` — Equalizer processor

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/nomad-audio.md) for high-level design concepts.

---

### Plugin System (Planned)

#### NomadSDK API
Plugin hosting and extension system.

**Status:** 📅 Planned for Q2 2025

**Planned APIs:**
- `nomad::PluginHost` — VST3 plugin hosting
- `nomad::Effect` — Audio effect interface
- `nomad::Automation` — Parameter automation
- `nomad::MIDIRouter` — MIDI routing

---

## 🎯 Quick Start Examples

### Creating a Window

```cpp
#include "NomadPlat/Window.h"

nomad::WindowConfig config;
config.title = "My Application";
config.width = 1280;
config.height = 720;

auto window = nomad::Window::create(config);
window->show();

while (window->isOpen()) {
    window->pollEvents();
    // Render frame
}
```

### Initializing Audio

```cpp
#include "NomadAudio/AudioEngine.h"

nomad::AudioEngine engine;

nomad::AudioConfig config;
config.sampleRate = 48000;
config.bufferSize = 512;
config.channels = 2;

engine.setCallback([](float* buffer, int samples) {
    // Process audio
});

if (engine.initialize(config)) {
    engine.start();
}
```

### Creating UI

```cpp
#include "NomadUI/Renderer.h"
#include "NomadUI/Widgets/Button.h"

nomad::Renderer renderer;
renderer.initialize(window->getContext());

auto button = std::make_shared<nomad::Button>("Click Me");
button->setBounds(100, 100, 200, 50);
button->onClick([]() {
    std::cout << "Button clicked!" << std::endl;
});

// Main loop
while (window->isOpen()) {
    window->pollEvents();
    
    renderer.beginFrame();
    button->render(renderer);
    renderer.endFrame();
    
    window->swapBuffers();
}
```

## 📖 API Conventions

### Naming Conventions

**Classes and Types:**
```cpp
class AudioEngine { };    // PascalCase
enum class LogLevel { };  // PascalCase
```

**Functions and Methods:**
```cpp
void initialize();        // camelCase
bool isRunning() const;   // camelCase
```

**Constants:**
```cpp
const int MAX_CHANNELS = 32;  // SCREAMING_SNAKE_CASE
```

**Namespaces:**
```cpp
namespace nomad { }       // lowercase
```

### Error Handling

NOMAD uses return values for error handling (no exceptions in real-time code):

```cpp
// Boolean success/failure
bool result = engine.initialize(config);
if (!result) {
    // Handle error
}

// std::optional for nullable returns
auto device = AudioDevice::getDefault();
if (device) {
    // Use device
}

// Error codes
enum class ErrorCode {
    Success,
    DeviceNotFound,
    InitializationFailed
};

ErrorCode error = engine.open(deviceId);
if (error != ErrorCode::Success) {
    // Handle error
}
```

### Memory Management

NOMAD uses RAII and smart pointers:

```cpp
// Unique ownership
auto window = std::make_unique<Window>();

// Shared ownership
auto button = std::make_shared<Button>("Click");

// Weak references
std::weak_ptr<Widget> weakRef = button;
```

### Thread Safety

APIs document thread safety:

```cpp
// Thread-safe: Can be called from any thread
void Logger::log(const std::string& message);

// Not thread-safe: Must be called from UI thread
void Widget::render(Renderer& renderer);

// Real-time safe: Can be called from audio thread
void AudioBuffer::read(float* buffer, int samples);
```

## 🔍 Finding Documentation

### By Feature

Looking for specific functionality? See the architecture documentation:

- **Window Management** → [NomadPlat Architecture](../architecture/nomad-plat.md)
- **Rendering** → [NomadUI Architecture](../architecture/nomad-ui.md)
- **Audio I/O** → [NomadAudio Architecture](../architecture/nomad-audio.md)
- **File Operations** → [NomadCore Architecture](../architecture/nomad-core.md)

### By Module

Exploring a specific module? See architecture docs:

- [NomadCore Architecture](../architecture/nomad-core.md)
- [NomadPlat Architecture](../architecture/nomad-plat.md)
- [NomadUI Architecture](../architecture/nomad-ui.md)
- [NomadAudio Architecture](../architecture/nomad-audio.md)

## 📝 Documentation Status

| Module | API Docs | Examples | Coverage |
|--------|----------|----------|----------|
| NomadCore | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 80% |
| NomadPlat | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 75% |
| NomadUI | ✅ [Complete](../api-reference/html/index.html) | 🚧 Partial | 60% |
| NomadAudio | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 85% |
| NomadSDK | 📅 Planned | 📅 Planned | 0% |

!!! success "Full API Reference Available"
    Complete API documentation is now available via [Doxygen API Reference](../api-reference/html/index.html). Additional resources:
    
    - [Architecture documentation](../architecture/overview.md) for high-level design concepts
    - [Getting Started Guide](../getting-started/index.md) for quick setup
    - Example code in the repository
    - [GitHub Discussions](https://github.com/currentsuspect/NOMAD/discussions) for questions

## 🤝 Contributing to API Docs

Help improve NOMAD's API documentation:

1. **Report Issues** — Found incorrect or missing docs? [Open an issue](https://github.com/currentsuspect/NOMAD/issues)
2. **Submit Examples** — Share your code examples
3. **Improve Clarity** — Suggest clearer explanations
4. **Add Diagrams** — Visual aids help understanding

See the [Contributing Guide](../developer/contributing.md) for details.

## 📚 Additional Resources

- [Architecture Overview](../architecture/overview.md)
- [Getting Started Guide](../getting-started/index.md)
- [Developer Guide](../developer/contributing.md)
- [Code Examples](https://github.com/currentsuspect/NOMAD/tree/main/examples)

---

**Need help?** Ask in [GitHub Discussions](https://github.com/currentsuspect/NOMAD/discussions) or check the [FAQ](../technical/faq.md).
