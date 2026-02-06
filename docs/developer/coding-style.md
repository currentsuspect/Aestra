# 💻 Aestra C++ Coding Style Guide

This guide defines the C++ coding standards for the Aestra DAW project. Adherence to these standards is mandatory for all contributions.

---

## 📋 Table of Contents

- [General Principles](#-general-principles)
- [Formatting](#-formatting)
- [Naming Conventions](#-naming-conventions)
- [Modern C++ Features](#-modern-c-features)
- [Real-Time Audio Safety](#-real-time-audio-safety)
- [File Organization](#-file-organization)

---

## 🎯 General Principles

1.  **Clarity First**: Code should be easy to read and understand.
2.  **Consistency**: Follow the existing style of the codebase.
3.  **Correctness**: Avoid undefined behavior and race conditions.
4.  **Performance**: Write efficient code, especially in audio processing paths.

---

## 🖌️ Formatting

We use **clang-format** to enforce code formatting. All code must pass the formatting check before merging.

### Running clang-format

```bash
# Check formatting
clang-format -n Source/Main.cpp

# Apply formatting
clang-format -i Source/Main.cpp
```

### Key Style Points (Enforced by clang-format)

- **Indentation**: 4 spaces.
- **Braces**: Allman style (braces on new line) for functions and classes; K&R/OTBS for control structures depending on config (check `.clang-format`).
- **Line Length**: 120 characters.
- **Pointer/Reference**: Left-aligned (`int* ptr`, `const int& ref`).

---

## 🏷️ Naming Conventions

| Category | Style | Example |
| :--- | :--- | :--- |
| **Classes / Structs** | PascalCase | `AudioEngine`, `Track` |
| **Functions / Methods** | camelCase | `processAudio`, `getSampleRate` |
| **Variables (Local)** | camelCase | `bufferSize`, `index` |
| **Member Variables** | m_camelCase | `m_sampleRate`, `m_isActive` |
| **Static Members** | s_camelCase | `s_instance`, `s_logger` |
| **Constants / Enums** | PascalCase | `MaxChannels`, `SampleRate` |
| **Macros** | UPPER_SNAKE_CASE | `AESTRA_VERSION`, `MAX_BUFFER_SIZE` |
| **Namespaces** | PascalCase | `Aestra`, `Audio`, `UI` |
| **Template Types** | PascalCase (T prefix) | `TValue`, `TBuffer` |

### Examples

```cpp
namespace Aestra {
namespace Audio {

class AudioProcessor {
public:
    static const int MaxChannels = 2;

    void processBuffer(float* buffer, int numFrames) {
        for (int i = 0; i < numFrames; ++i) {
            // ...
        }
    }

private:
    float m_gain = 1.0f;
    static int s_instanceCount;
};

} // namespace Audio
} // namespace Aestra
```

---

## 🚀 Modern C++ Features

We target **C++17**. Use modern features where appropriate:

-   **`auto`**: Use for complex types (iterators) or when type is obvious (`auto* ptr = new T()`). Avoid for simple types (`int`, `float`).
-   **`nullptr`**: Always use `nullptr` instead of `NULL` or `0`.
-   **`override` / `final`**: Always mark virtual overrides.
-   **Smart Pointers**: Use `std::unique_ptr` and `std::shared_ptr` instead of raw pointers for ownership.
    -   Use `std::make_unique` and `std::make_shared`.
-   **Lambdas**: Preferred over `std::bind`.
-   **`constexpr`**: Use for compile-time constants.
-   **Structured Binding**: Encouraged for tuple/pair unpacking.

---

## ⚡ Real-Time Audio Safety

Code running in the audio thread (e.g., `processBlock`) must meet strict real-time requirements:

1.  **No Memory Allocation**: Avoid `new`, `malloc`, `std::vector::push_back`, string manipulation, or anything that allocates heap memory.
    -   Pre-allocate all buffers in `prepareToPlay` or constructors.
2.  **No Locks**: Do not use `std::mutex`, `std::lock_guard`, or other blocking synchronization.
    -   Use `std::atomic` (wait-free) or lock-free data structures (e.g., SPSC queues).
3.  **No System Calls**: Avoid `printf`, `std::cout`, file I/O, or thread sleeping.
4.  **Exceptions**: Do not throw exceptions in the audio thread.

### Verification

Use the audit script to check for violations:

```bash
python3 scripts/audit_codebase.py
```

---

## 📂 File Organization

-   **Header Files (`.h`)**:
    -   Use `#pragma once`.
    -   Minimize includes in headers (forward declare when possible).
    -   Group includes: standard library, external libraries, project headers.
-   **Source Files (`.cpp`)**:
    -   Implement functions defined in headers.
    -   Keep implementation details private (use anonymous namespaces).

### Include Order

```cpp
#include "MyClass.h"        // 1. Related header

#include <vector>           // 2. Standard library
#include <string>

#include <juce_audio_basics // 3. External libraries (if any)

#include "OtherClass.h"     // 4. Project headers
```

---

## 📚 Related Resources

-   [Contributing Guide](contributing.md)
-   [Documentation Style Guide](style-guide.md)
-   [Architecture Overview](../architecture/overview.md)

---

*Last updated: February 2026*
