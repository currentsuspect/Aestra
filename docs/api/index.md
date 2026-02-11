# API Reference

Welcome to the Aestra API reference. This section provides detailed documentation for all public APIs.

!!! tip "🚀 Full API Reference"
    **[Browse the Complete Doxygen API Reference →](../api-reference/html/index.html)**
    
    Comprehensive documentation for all classes, functions, and modules with searchable interface, inheritance diagrams, and detailed member descriptions.

## 📚 Module APIs

Aestra's API is organized by module. Each module provides a focused set of functionality:

### Core Foundation

#### AestraCore API
Foundation utilities and data structures.

**Key APIs:**
- `Aestra::Vec2`, `Aestra::Vec3`, `Aestra::Vec4` — Vector mathematics
- `Aestra::Mat4` — Matrix operations
- `Aestra::LockFreeQueue<T>` — Lock-free thread communication
- `Aestra::ThreadPool` — Background task execution
- `Aestra::File` — Cross-platform file I/O
- `Aestra::Logger` — Structured logging

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/Aestra-core.md) for high-level design concepts.

---

### Platform Abstraction

#### AestraPlat API
Platform-specific functionality with unified interface.

**Key APIs:**
- `Aestra::Window` — Window creation and management
- `Aestra::Input` — Keyboard and mouse input
- `Aestra::FileDialog` — Native file dialogs
- `Aestra::SystemInfo` — System capabilities
- `Aestra::Timer` — High-resolution timing

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/Aestra-plat.md) for high-level design concepts.

---

### User Interface

#### AestraUI API
GPU-accelerated UI framework.

**Key APIs:**
- `Aestra::Renderer` — OpenGL rendering
- `Aestra::Widget` — Base widget class
- `Aestra::Button` — Button widget
- `Aestra::Slider` — Slider widget
- `Aestra::TextBox` — Text input widget
- `Aestra::Layout` — Layout management
- `Aestra::Theme` — Theme system

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/Aestra-ui.md) for high-level design concepts.

---

### Audio Engine

#### AestraAudio API
Professional audio processing system.

**Key APIs:**
- `Aestra::AudioEngine` — Audio engine management
- `Aestra::AudioDevice` — Device enumeration
- `Aestra::AudioBuffer` — Audio buffer management
- `Aestra::DSP::Gain` — Gain processor
- `Aestra::DSP::EQ` — Equalizer processor

!!! tip "Full API Reference Available"
    Complete API documentation is available in the [Doxygen API Reference](../api-reference/html/index.html). See the [Architecture documentation](../architecture/Aestra-audio.md) for high-level design concepts.

---

### Plugin System (Planned)

#### AestraSDK API
Plugin hosting and extension system.

**Status:** 📅 Planned for Q2 2025

**Planned APIs:**
- `Aestra::PluginHost` — VST3 plugin hosting
- `Aestra::Effect` — Audio effect interface
- `Aestra::Automation` — Parameter automation
- `Aestra::MIDIRouter` — MIDI routing

---

## 🎯 Quick Start Examples

### Creating a Window

```cpp
#include "AestraPlat/Window.h"

Aestra::WindowConfig config;
config.title = "My Application";
config.width = 1280;
config.height = 720;

auto window = Aestra::Window::create(config);
window->show();

while (window->isOpen()) {
    window->pollEvents();
    // Render frame
}
```

### Initializing Audio

```cpp
#include "AestraAudio/AudioEngine.h"

Aestra::AudioEngine engine;

Aestra::AudioConfig config;
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
#include "AestraUI/Renderer.h"
#include "AestraUI/Widgets/Button.h"

Aestra::Renderer renderer;
renderer.initialize(window->getContext());

auto button = std::make_shared<Aestra::Button>("Click Me");
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
namespace Aestra { }       // lowercase
```

### Error Handling

Aestra uses return values for error handling (no exceptions in real-time code):

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

Aestra uses RAII and smart pointers:

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

- **Window Management** → [AestraPlat Architecture](../architecture/Aestra-plat.md)
- **Rendering** → [AestraUI Architecture](../architecture/Aestra-ui.md)
- **Audio I/O** → [AestraAudio Architecture](../architecture/Aestra-audio.md)
- **File Operations** → [AestraCore Architecture](../architecture/Aestra-core.md)

### By Module

Exploring a specific module? See architecture docs:

- [AestraCore Architecture](../architecture/Aestra-core.md)
- [AestraPlat Architecture](../architecture/Aestra-plat.md)
- [AestraUI Architecture](../architecture/Aestra-ui.md)
- [AestraAudio Architecture](../architecture/Aestra-audio.md)

## 📝 Documentation Status

| Module | API Docs | Examples | Coverage |
|--------|----------|----------|----------|
| AestraCore | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 80% |
| AestraPlat | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 75% |
| AestraUI | ✅ [Complete](../api-reference/html/index.html) | 🚧 Partial | 60% |
| AestraAudio | ✅ [Complete](../api-reference/html/index.html) | ✅ Complete | 85% |
| AestraSDK | 📅 Planned | 📅 Planned | 0% |

!!! success "Full API Reference Available"
    Complete API documentation is now available via [Doxygen API Reference](../api-reference/html/index.html). Additional resources:
    
    - [Architecture documentation](../architecture/overview.md) for high-level design concepts
    - [Getting Started Guide](../getting-started/index.md) for quick setup
    - Example code in the repository
    - [GitHub Discussions](https://github.com/currentsuspect/Aestra/discussions) for questions

## 🤝 Contributing to API Docs

Help improve Aestra's API documentation:

1. **Report Issues** — Found incorrect or missing docs? [Open an issue](https://github.com/currentsuspect/Aestra/issues)
2. **Submit Examples** — Share your code examples
3. **Improve Clarity** — Suggest clearer explanations
4. **Add Diagrams** — Visual aids help understanding

See the [Contributing Guide](../developer/contributing.md) for details.

## 📚 Additional Resources

- [Architecture Overview](../architecture/overview.md)
- [Getting Started Guide](../getting-started/index.md)
- [Developer Guide](../developer/contributing.md)
- [Code Examples](https://github.com/currentsuspect/Aestra)

---

**Need help?** Ask in [GitHub Discussions](https://github.com/currentsuspect/Aestra/discussions) or check the [FAQ](../technical/faq.md).
