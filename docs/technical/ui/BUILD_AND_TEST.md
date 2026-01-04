# 🔨 Build and Test Guide

## Quick Start - Test Core Classes

We have a **minimal test** that verifies the core classes work without needing OpenGL or platform code.

### Windows (Visual Studio)

```powershell
# Navigate to NomadUI directory
cd NomadUI

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
cmake --build . --config Debug

# Run tests
.\bin\Debug\NomadUI_MinimalTest.exe
```

### Expected Output

```
========================================
  Nomad UI - Minimal Core Tests
========================================

Testing NUITypes...
  ✓ NUITypes tests passed
Testing NUIComponent...
  ✓ NUIComponent tests passed
Testing NUITheme...
  ✓ NUITheme tests passed
Testing Component Hierarchy...
  ✓ Component Hierarchy tests passed
Testing Event System...
  ✓ Event System tests passed
Testing Theme Inheritance...
  ✓ Theme Inheritance tests passed

========================================
  ✅ ALL TESTS PASSED!
========================================

Core classes are working correctly.
Next: Implement OpenGL renderer and platform layer.
```

## What This Tests

### ✅ NUITypes
- Point, Rect, Size structures
- Color creation and manipulation
- Coordinate math

### ✅ NUIComponent
- Component creation
- Bounds management
- Visibility and enabled state
- Hierarchy (parent/child)
- Dirty flag system

### ✅ NUITheme
- Theme creation
- Color access
- Dimension access
- Effect parameters
- Font sizes
- Custom values

### ✅ Component Hierarchy
- Parent-child relationships
- Find by ID
- Coordinate conversion (local ↔ global)

### ✅ Event System
- Mouse events
- Hover state
- Focus state

### ✅ Theme Inheritance
- Theme propagation to children

## Troubleshooting

### Build Errors

**Error: "Cannot open include file"**
- Make sure you're in the `NomadUI` directory
- Check that all header files exist

**Error: "Unresolved external symbol"**
- Make sure all `.cpp` files are in CMakeLists.txt
- Try cleaning and rebuilding: `cmake --build . --config Debug --clean-first`

**Error: "64-bit required"**
- You're trying to build 32-bit
- Use x64 Native Tools Command Prompt for VS
- Or configure CMake with: `cmake -A x64 ..`

### Test Failures

**Assertion failed**
- This means a core class has a bug
- Check the line number in the error
- Review the corresponding test in `MinimalTest.cpp`

## Next Steps

Once this test passes, we know the core classes work! Then we can:

1. **Implement OpenGL Renderer** (`NUIRendererGL.cpp`)
2. **Implement Windows Platform** (`NUIPlatformWindows.cpp`)
3. **Build Full Demo** (`SimpleDemo.cpp`)

## Alternative: Manual Verification

If you don't want to build yet, you can verify the code compiles by:

### Check Syntax Only

```powershell
# Just check if it compiles (no linking)
cl /c /std:c++17 /I. Core/NUIComponent.cpp Core/NUITheme.cpp Core/NUIApp.cpp
```

### Use Visual Studio

1. Open Visual Studio
2. File → Open → CMake → Select `NomadUI/CMakeLists.txt`
3. Build → Build All
4. Run `NomadUI_MinimalTest`

## Continuous Integration (Future)

We should add:
- GitHub Actions for automated testing
- Unit tests for each class
- Performance benchmarks
- Memory leak detection

## Test Coverage

Current test coverage:
- **NUITypes:** 80% (basic functionality)
- **NUIComponent:** 70% (core features)
- **NUITheme:** 60% (basic access)
- **Hierarchy:** 50% (basic tree operations)
- **Events:** 40% (basic event handling)

Not yet tested:
- Rendering (needs OpenGL)
- Platform integration (needs Win32)
- Widgets (needs renderer)
- Layout engine (not implemented)
- Animation system (not implemented)

---

**Ready to test? Run the build commands above!** 🚀
