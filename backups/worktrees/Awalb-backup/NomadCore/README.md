# NomadCore

Core utilities and foundation for the NOMAD DAW project.

## Overview

NomadCore provides essential low-level utilities used throughout the NOMAD DAW:

- **Configuration** - Build detection, platform/compiler/arch detection, SIMD support
- **Assertions** - Debug assertions integrated with logging system
- **Math Utilities** - Vector2/3/4, Matrix4x4, DSP math functions
- **Threading Primitives** - Lock-free ring buffer, thread pool, atomic utilities
- **File I/O** - File abstraction, binary serialization, JSON parsing
- **Logging System** - Multi-level logging with console and file output

## Features

### Configuration (NomadConfig.h)
- Build type detection (Debug/Release)
- Platform detection (Windows/Linux/macOS)
- Compiler detection (MSVC/GCC/Clang)
- Architecture detection (x64/x86/ARM)
- SIMD support detection (AVX2/AVX/SSE4/SSE2/NEON)
- Audio configuration constants
- Compiler attributes (force inline, branch hints, etc.)
- Version information

### Assertions (NomadAssert.h)
- Debug assertions with logging integration
- Assertions with custom messages
- Precondition/postcondition/invariant checks
- Bounds and null pointer checking
- Static assertions (compile-time)
- Verify (always-enabled assertions)
- Unreachable code markers

### Math (NomadMath.h)
- Vector2, Vector3, Vector4 with standard operations
- Matrix4x4 with transformations (translation, rotation, scale)
- DSP functions: lerp, clamp, smoothstep, map, dB conversion

### Threading (NomadThreading.h)
- Lock-free ring buffer (SPSC) for real-time audio
- Thread pool for parallel task execution
- Atomic utilities: AtomicFlag, AtomicCounter, SpinLock

### File I/O (NomadFile.h, NomadJSON.h)
- Cross-platform file abstraction
- Binary serialization for efficient data storage
- Lightweight JSON parser for configuration files

### Logging (NomadLog.h)
- Multiple log levels: Debug, Info, Warning, Error
- Console and file logging
- Thread-safe multi-logger support
- Stream-style logging macros

## Usage

```cpp
#include <NomadConfig.h>
#include <NomadAssert.h>
#include <NomadMath.h>
#include <NomadThreading.h>
#include <NomadFile.h>
#include <NomadLog.h>

using namespace Nomad;

// Configuration
#if NOMAD_PLATFORM_WINDOWS
    // Windows-specific code
#endif

// Assertions
NOMAD_ASSERT(sampleRate > 0);
NOMAD_ASSERT_MSG(bufferSize >= 64, "Buffer too small");
NOMAD_ASSERT_RANGE(volume, 0.0f, 1.0f);

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
NOMAD_INFO << "Application started";
NOMAD_ERROR << "Error code: " << 42;
```

## Testing

All modules include comprehensive unit tests:

```bash
cmake -B build -S .
cmake --build build --config Release
./build/NomadCore/Release/ConfigAssertTests.exe
./build/NomadCore/Release/MathTests.exe
./build/NomadCore/Release/ThreadingTests.exe
./build/NomadCore/Release/FileTests.exe
./build/NomadCore/Release/LogTests.exe
```

Test in Debug mode to verify assertions:
```bash
cmake --build build --config Debug
./build/NomadCore/Debug/ConfigAssertTests.exe
```

## Design Philosophy

- **Header-only where possible** - Easy integration
- **Zero dependencies** - Only standard library
- **Real-time safe** - Lock-free structures for audio thread
- **Cross-platform** - Works on Windows, Linux, macOS
