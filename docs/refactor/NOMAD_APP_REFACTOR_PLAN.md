# NomadApp Refactoring Plan

## Problem Statement
`Source/Main.cpp` contains the `NomadApp` class, which has grown into a "God Class" (1500+ lines). It handles:
- Application Lifecycle (Init/Run/Shutdown)
- Window Management (GLFW/PlatformBridge)
- Audio Engine Setup & Callbacks
- UI Composition & Layout
- Event Handling (Keyboard/Mouse)
- Project Persistence
- Testing (Test Tone Generator)

This violates the Single Responsibility Principle, makes unit testing impossible, and complicates concurrent development.

## Proposed Architecture

We will split `NomadApp` into a set of focused controllers orchestrated by a thin `NomadApp` shell.

### 1. NomadAppController (The Orchestrator)
**Responsibility:** Bootstrap the application, tie subsystems together, and run the main loop.
**Location:** `Source/App/NomadApp.cpp`
**Dependencies:** `NomadWindowManager`, `NomadAudioController`, `NomadProjectManager`, `NomadUIController`.

### 2. NomadWindowManager
**Responsibility:** Manage the physical window, OpenGL context, and platform events.
**Location:** `Source/App/NomadWindowManager.h`
**Key Methods:**
- `initialize(width, height, title)`
- `processEvents()`
- `swapBuffers()`
- `setResizeCallback(...)`

### 3. NomadAudioController
**Responsibility:** Initialize `AudioDeviceManager`, `AudioEngine`, handle device changes, and provide the audio callback.
**Location:** `Source/App/NomadAudioController.h`
**Key Methods:**
- `initializeAudio()`
- `getAudioEngine()`
- `audioCallback(...)` (Static wrapper)
- `handleDeviceChange()`

### 4. NomadUIController
**Responsibility:** Construct the UI hierarchy (`NomadContent`, `NomadRootComponent`), handle high-level UI commands (Toggle Mixer, etc.).
**Location:** `Source/App/NomadUIController.h`
**Key Methods:**
- `createUI(window, audioEngine)`
- `render(renderer)`
- `update(dt)`
- `handleShortcut(key)`

### 5. NomadProjectManager
**Responsibility:** Load/Save projects, autosave logic.
**Location:** `Source/App/NomadProjectManager.h`
**Key Methods:**
- `loadProject(path)`
- `saveProject(path)`
- `startAutosaveTimer()`

## Incremental Refactoring Steps

1.  **Extract Audio Logic:** Move `m_audioManager`, `m_audioEngine`, and `audioCallback` to `NomadAudioController`.
2.  **Extract Window Logic:** Move `m_window`, `m_renderer` setup to `NomadWindowManager`.
3.  **Extract UI Logic:** Move `m_rootComponent`, `m_content`, `m_transportBar` to `NomadUIController`.
4.  **Simplify Main.cpp:** `NomadApp` becomes a coordinator that just calls `initialize` on these subsystems and loops.

## Benefits
- **Testability:** We can unit test `NomadProjectManager` without opening a window.
- **Maintainability:** Audio logic is isolated from UI logic.
- **Scalability:** Multiple developers can work on Audio vs UI setup simultaneously.
