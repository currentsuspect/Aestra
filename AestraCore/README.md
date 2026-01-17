# AestraCore

Core utilities and foundation for the Aestra project.

## Overview

AestraCore provides essential low-level utilities used throughout the Aestra:

- **Configuration** - Build detection, platform/compiler/arch detection, SIMD support
- **Assertions** - Debug assertions integrated with logging system
- **Math Utilities** - Vector2/3/4, Matrix4x4, DSP math functions
- **Threading Primitives** - Lock-free ring buffer, thread pool, atomic utilities
- **File I/O** - File abstraction, binary serialization, JSON parsing
- **Logging System** - Multi-level logging with console and file output

## Features

### Configuration (AestraConfig.h)
- Build type detection (Debug/Release)
- Platform detection (Windows/Linux/macOS)
- Compiler detection (MSVC/GCC/Clang)
- Architecture detection (x64/x86/ARM)
- SIMD support detection (AVX2/AVX/SSE4/SSE2/NEON)
- Audio configuration constants
- Compiler attributes (force inline, branch hints, etc.)
- Version information

### Assertions (AestraAssert.h)
- Debug assertions with logging integration
- Assertions with custom messages
- Precondition/postcondition/invariant checks
- Bounds and null pointer checking
- Static assertions (compile-time)
- Verify (always-enabled assertions)
- Unreachable code markers

### Math (AestraMath.h)
- Vector2, Vector3, Vector4 with standard operations
- Matrix4x4 with transformations (translation, rotation, scale)
- DSP functions: lerp, clamp, smoothstep, map, dB conversion

### Threading (AestraThreading.h)
- Lock-free ring buffer (SPSC) for real-time audio
- Thread pool for parallel task execution
- Atomic utilities: AtomicFlag, AtomicCounter, SpinLock

### File I/O (AestraFile.h, AestraJSON.h)
- Cross-platform file abstraction
- Binary serialization for efficient data storage
- Lightweight JSON parser for configuration files

### Logging (AestraLog.h)
- Multiple log levels: Debug, Info, Warning, Error
- Console and file logging
- Thread-safe multi-logger support
- Stream-style logging macros

## Usage

```cpp
#include <AestraConfig.h>
#include <AestraAssert.h>
#include <AestraMath.h>
#include <AestraThreading.h>
#include <AestraFile.h>
#include <AestraLog.h>

using namespace Aestra;

// Configuration
#if Aestra_PLATFORM_WINDOWS
    // Windows-specific code
#endif

// Assertions
Aestra_ASSERT(sampleRate > 0);
Aestra_ASSERT_MSG(bufferSize >= 64, "Buffer too small");
Aestra_ASSERT_RANGE(volume, 0.0f, 1.0f);

// Math
Vector3 v(1.0f, 2.0f, 3.0f);
float len = v.length();

// Threading
LockFreeRingBuffer<float, 1024> audioBuffer;
ThreadPool pool(4);

// File I/O
std::string content = File::readAllText("config.txt");
JSON config = JSON::parse(content);

// Logging
Aestra_INFO << "Application started";
Aestra_ERROR << "Error code: " << 42;
```

## Testing

All modules include comprehensive unit tests:

```bash
cmake -B build -S .
cmake --build build --config Release
./build/AestraCore/Release/ConfigAssertTests.exe
./build/AestraCore/Release/MathTests.exe
./build/AestraCore/Release/ThreadingTests.exe
./build/AestraCore/Release/FileTests.exe
./build/AestraCore/Release/LogTests.exe
```

Test in Debug mode to verify assertions:
```bash
cmake --build build --config Debug
./build/AestraCore/Debug/ConfigAssertTests.exe
```

## Design Philosophy

- **Header-only where possible** - Easy integration
- **Zero dependencies** - Only standard library
- **Real-time safe** - Lock-free structures for audio thread
- **Cross-platform** - Works on Windows, Linux, macOS
