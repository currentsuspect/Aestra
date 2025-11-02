# OpenGL Linking Guide for NOMAD

## Overview

This document explains how NOMAD properly handles OpenGL linking to avoid symbol conflicts between GLAD and opengl32.lib.

## The Problem

When using GLAD for dynamic OpenGL function loading, you can encounter `LNK2005` errors (multiply defined symbols) if both GLAD and opengl32.lib are linked:

```
glad.lib(glad.obj) : error LNK2005: glClear already defined in opengl32.lib(OPENGL32.dll)
glad.lib(glad.obj) : error LNK2005: glViewport already defined in opengl32.lib(OPENGL32.dll)
...
```

This happens because:
- **GLAD** provides all OpenGL functions (glClear, glViewport, etc.) dynamically
- **opengl32.lib** also provides these same functions statically
- The linker sees duplicate definitions and fails

## The Solution

### 1. NomadPlat Links opengl32 PRIVATELY

**File:** `NomadPlat/CMakeLists.txt`

```cmake
target_link_libraries(NomadPlat PRIVATE
    opengl32  # For WGL functions (wglCreateContext, wglMakeCurrent, etc.)
    gdi32
    user32
    ...
)
```

**Why:** NomadPlat needs `opengl32.lib` for Windows-specific WGL functions used in OpenGL context creation. By linking it `PRIVATE`, these symbols don't propagate to dependent targets.

### 2. NomadUI_OpenGL Uses GLAD Only

**File:** `NomadUI/CMakeLists.txt`

```cmake
target_link_libraries(NomadUI_OpenGL PUBLIC
    NomadUI_Core
    glad        # ✅ GLAD for dynamic GL function loading
    freetype
    # NOT OpenGL::GL or opengl32 ❌
)
```

**Why:** The UI renderer uses GLAD to dynamically load all OpenGL functions at runtime. No static linking to opengl32.

### 3. Main Application Links GLAD with Priority

**File:** `Source/CMakeLists.txt`

```cmake
target_link_libraries(NOMAD_DAW PRIVATE
    NomadCore
    NomadPlat
    NomadUI_OpenGL
    NomadAudio
)

# Force GLAD symbols to take precedence
target_link_options(NOMAD_DAW PRIVATE
    "/WHOLEARCHIVE:$<TARGET_FILE:glad>"
    "/IGNORE:4098"  # Suppress CRT mismatch warning
)
```

**Why:** The `/WHOLEARCHIVE` flag ensures all GLAD symbols are included and take precedence over any opengl32 symbols that might be pulled in transitively.

## Symbol Hierarchy

```
Application (NOMAD_DAW)
├── GL* functions → GLAD (dynamic loading)
├── wgl* functions → opengl32.lib (via NomadPlat, PRIVATE)
└── Platform functions → NomadPlat
```

## Key Principles

1. **GLAD for GL functions**: All `gl*` functions (glClear, glViewport, etc.) come from GLAD
2. **opengl32 for WGL functions**: Windows-specific `wgl*` functions (wglCreateContext, etc.) come from opengl32.lib
3. **PRIVATE linking**: NomadPlat links opengl32 as PRIVATE so it doesn't propagate
4. **Symbol precedence**: Use `/WHOLEARCHIVE` to ensure GLAD symbols take priority

## Common Mistakes to Avoid

### ❌ DON'T: Link OpenGL::GL in UI code
```cmake
target_link_libraries(NomadUI_OpenGL PUBLIC
    OpenGL::GL  # ❌ This causes symbol conflicts
    glad
)
```

### ❌ DON'T: Link opengl32 as PUBLIC
```cmake
target_link_libraries(NomadPlat PUBLIC
    opengl32  # ❌ This propagates to all dependents
)
```

### ❌ DON'T: Use /FORCE:MULTIPLE
```cmake
target_link_options(NOMAD_DAW PRIVATE
    "/FORCE:MULTIPLE"  # ❌ Masks real problems, causes runtime instability
)
```

### ✅ DO: Use GLAD exclusively for GL functions
```cmake
target_link_libraries(NomadUI_OpenGL PUBLIC
    glad  # ✅ Dynamic loading, no conflicts
)
```

### ✅ DO: Link opengl32 PRIVATE where needed
```cmake
target_link_libraries(NomadPlat PRIVATE
    opengl32  # ✅ For WGL functions only
)
```

## Verification

To verify the linking is correct:

```bash
# Clean build
rm -rf build
cmake -B build -S .
cmake --build build --config Release

# Should see NO LNK2005 errors
# Should see NO /FORCE:MULTIPLE warnings
# Application should run without crashes
```

## Runtime Behavior

1. **Application starts** → NomadPlat creates OpenGL context using WGL functions from opengl32.lib
2. **GLAD initializes** → `gladLoadGL()` dynamically loads all GL function pointers
3. **Rendering** → All GL calls go through GLAD's function pointers
4. **No symbol conflicts** → WGL and GL functions are properly separated

## References

- [GLAD Documentation](https://github.com/Dav1dde/glad)
- [OpenGL Context Creation on Windows](https://www.khronos.org/opengl/wiki/Creating_an_OpenGL_Context_(WGL))
- [CMake target_link_libraries](https://cmake.org/cmake/help/latest/command/target_link_libraries.html)

---

**Last Updated:** 2025-01-22  
**Status:** ✅ Working correctly in NOMAD v1.0
