# Aestra Threading Model (B-004)

This document describes the threading architecture of Aestra.

## Thread Overview

Aestra uses a multi-threaded architecture with three main thread types:

```
┌─────────────────────────────────────────────────────────────────────┐
│                          MAIN THREAD                                │
│  • Event loop (AestraApp::run)                                       │
│  • UI rendering (NUIRenderer)                                       │
│  • User input handling                                              │
│  • Non-real-time audio commands                                     │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Lock-free command queue
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         AUDIO THREAD                                │
│  • Real-time audio processing (AudioEngine::process)                │
│  • DSP operations (mixing, effects)                                 │
│  • ⚠️NO allocations, NO locks, NO I/O                               │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              │ Atomic flags / counters
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        WORKER THREADS                               │
│  • Autosave (async file writes)                                     │
│  • Waveform decoding (background loading)                           │
│  • Plugin scanning                                                  │
│  • Export/render operations                                         │
└─────────────────────────────────────────────────────────────────────┘ 
```

## Thread Responsibilities

### 1. Main Thread (UI Thread)

**Entry Point:** `main()` → `AestraApp::initialize()` → `AestraApp::run()`

**Responsibilities:**
- Application lifecycle management
- Window creation and management
- OpenGL rendering context owner
- UI event handling (mouse, keyboard)
- Menu actions and dialogs
- Project save/load coordination
- Sending commands to audio thread

**Safe Operations:**
- ✅ Memory allocation
- ✅ Mutex locks
- ✅ File I/O
- ✅ Logging
- ✅ Exception handling

**Key Classes:**
- `AestraApp` - Application lifecycle
- `NUIRenderer` - UI rendering
- `NUIPlatformBridge` - Window/input abstraction
- `TrackManagerUI` - Track UI components

### 2. Audio Thread (Real-Time Thread)

**Entry Point:** OS audio callback → `AudioDeviceManager` → `AudioEngine::process()`

**Responsibilities:**
- Real-time audio processing
- Mixing tracks and buses
- Applying effects
- Metronome generation
- Recording input to buffers
- Transport position tracking

**⚠️ FORBIDDEN Operations:**
- ❌ Memory allocation (`new`, `malloc`, `std::vector::push_back`)
- ❌ Mutex locks (`std::mutex`, `std::lock_guard`)
- ❌ File I/O (`fopen`, `std::ifstream`)
- ❌ Logging (may allocate internally)
- ❌ System calls that block
- ❌ Throwing exceptions

**Safe Operations:**
- ✅ Atomic reads/writes
- ✅ Lock-free queue operations
- ✅ Pre-allocated buffer access
- ✅ Simple math and DSP

**Key Classes:**
- `AudioEngine` - Core audio processing
- `AudioCommandQueue` - Lock-free UI→Audio communication
- `TrackManager` - Track state management

### 3. Worker Threads (Background)

**Entry Point:** `std::async()` or thread pool

**Responsibilities:**
- Autosave (periodic project snapshots)
- Waveform decoding (loading audio files)
- Plugin scanning
- Offline rendering/export
- Large file operations

**Safe Operations:**
- ✅ Memory allocation
- ✅ Mutex locks (with care)
- ✅ File I/O
- ✅ Network operations

**Key Classes:**
- Autosave tasks in `AestraApp`
- `WaveformCache` background loading
- `PluginManager` scanning

## Communication Patterns

### UI → Audio: Command Queue

```cpp
// Main thread: send command
AudioQueueCommand cmd;
cmd.type = AudioQueueCommandType::SetTransportState;
cmd.value1 = 1.0f; // playing
m_commandQueue.push(cmd);

// Audio thread: process commands
AudioQueueCommand cmd;
while (m_commandQueue.pop(cmd)) {
    switch (cmd.type) {
        case SetTransportState: ...
    }
}
```

### Audio → UI: Atomic Flags

```cpp
// Audio thread: signal event
m_underrunFlag.store(true, std::memory_order_release);

// Main thread: check and clear
if (m_underrunFlag.exchange(false)) {
    showUnderrunWarning();
}
```

### Audio → UI: Atomic Counters

```cpp
// Audio thread: update position
m_globalSamplePos.store(pos, std::memory_order_relaxed);

// Main thread: read position
uint64_t pos = m_globalSamplePos.load(std::memory_order_relaxed);
```

## Lifecycle State Machine (B-002)

```
[Created] ──► [Initializing] ──► [Running] ──► [ShuttingDown] ──► [Terminated]
                    │                               ▲
                    ▼                               │
              [InitFailed] ─────────────────────────┘
```

Use `AppLifecycle::instance().getState()` to query current state.

## Debug Checks (B-005)

In debug builds, use these macros to catch threading violations:

```cpp
// Before any allocation
Aestra_ASSERT_NOT_AUDIO_THREAD();

// At start of audio callback
AudioThreadGuard guard;  // Sets thread-local flag

// Check thread context
if (Aestra::Audio::isAudioThread()) {
    // We're on the audio thread
}
```

## Best Practices

1. **Never block the audio thread** - Use lock-free queues for communication

2. **Pre-allocate everything** - Audio buffers, command queues, lookup tables

3. **Use atomic operations** - For simple values shared between threads

4. **Design for worst case** - Audio callback must complete in buffer period

5. **Test under load** - Run soak tests with stress on all threads

6. **Profile regularly** - Use Tracy or similar to identify bottlenecks

## Related Files

- `AppBootstrap.h` - Initialization modules (B-001)
- `AppLifecycle.h` - Lifecycle states (B-002)
- `ServiceLocator.h` - Service registry (B-003)
- `AudioThreadConstraints.h` - Thread safety checks (B-005)
