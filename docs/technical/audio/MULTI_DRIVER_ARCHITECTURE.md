# Aestra Multi-Tier Audio Driver Implementation Plan

## 📋 Overview

This document outlines the complete implementation of Aestra's professional-grade audio driver architecture, supporting multiple Windows audio APIs with intelligent fallback and ASIO integration.

## 🎯 Phase 1: Lock Down Stability (WASAPI + Legacy) ✅

### ✅ Completed Components

#### 1. **Driver Architecture Foundation**
- **File**: `AudioDriverTypes.h`
- **Features**:
  - `AudioDriverType` enum with priority ordering
  - `DriverPriority` metadata system
  - `DriverCapability` flags
  - `DriverState` and `DriverError` enums
  - `DriverStatistics` for performance monitoring

#### 2. **NativeAudioDriver Base Class**
- **File**: `NativeAudioDriver.h`
- **Features**:
  - Extended interface for driver type awareness
  - Error callback support
  - Statistics and monitoring
  - Availability detection

#### 3. **WASAPI Shared Mode Driver** ✅
- **Files**: `WASAPISharedDriver.h/cpp`
- **Features**:
  - Default safe mode for all users
  - Automatic sample rate conversion
  - Thread priority management (TIME_CRITICAL)
  - MMCSS "Pro Audio" scheduling
  - Event-driven callbacks
  - Underrun detection and statistics
  - ~15ms typical latency
- **Capabilities**:
  - Playback + Recording + Duplex
  - Sample rate conversion
  - Bit depth conversion
  - Hot-plug detection
  - Channel mixing

#### 4. **WASAPI Exclusive Mode Driver** ✅
- **Files**: `WASAPIExclusiveDriver.h/cpp`
- **Features**:
  - Professional low-latency mode
  - Exclusive device access (no mixing)
  - Event-driven callbacks
  - Sample-rate matching and negotiation
  - Buffer size alignment
  - ~3-5ms typical latency
  - Pro Audio MMCSS scheduling
- **Capabilities**:
  - Playback + Recording + Duplex
  - Exclusive mode access
  - Event-driven operation
  - Hot-plug detection
- **Sample Rate Support**:
  - Auto-detection: 44.1, 48, 88.2, 96, 176.4, 192 kHz
  - Fallback to best available rate

### 🔨 To Do: DirectSound Fallback

#### **DirectSoundDriver** (Legacy Support)
- **Purpose**: Universal fallback for maximum compatibility
- **Target Latency**: ~30ms (acceptable for legacy systems)
- **Priority**: Lowest (fallback only)
- **Files to Create**:
  - `AestraAudio/include/DirectSoundDriver.h`
  - `AestraAudio/src/DirectSoundDriver.cpp`
- **Key Features**:
  - DirectSound 8 API
  - Primary/Secondary buffer management
  - Polling-based audio thread
  - Compatible with Windows XP+
  - Guaranteed to work on any Windows system

## 🎮 Phase 2: Driver Management System

### **AudioDriverManager** (Priority System)

**File**: `AestraAudio/include/AudioDriverManager.h/cpp`

**Responsibilities**:
1. **Driver Registration**
   - Register all available drivers at startup
   - Query capabilities and availability
   - Build priority-ordered list

2. **Auto-Detection**
   - Scan for ASIO drivers (registry)
   - Test exclusive mode availability
   - Detect device capabilities

3. **Intelligent Selection**
   - Select best available driver based on:
     - User preference (if set)
     - Driver priority
     - Device capabilities
     - Current system state

4. **Fallback Logic**
   - Automatic fallback on driver failure
   - Retry with next-best driver
   - Comprehensive error logging

5. **Runtime Information**
   - Display active driver type in UI
   - Show latency and buffer info
   - Expose statistics

**Priority Order**:
```
1. ASIO External  (if available)
2. Aestra ASIO     (if no external ASIO)
3. WASAPI Exclusive (if device not in use)
4. WASAPI Shared    (default safe mode)
5. DirectSound      (legacy fallback)
```

### **Error and Fallback Layer**

**Features**:
- Centralized error handling
- Automatic fallback progression
- Error logging and reporting
- Recovery strategies:
  - Device in use → Try shared mode
  - Exclusive failed → Try shared
  - WASAPI failed → Try DirectSound
  - Format unsupported → Try different sample rate

## ⚡ Phase 3: ASIO Integration (External Drivers)

### **ASIODriverScanner**

**File**: `AestraAudio/include/ASIODriverScanner.h/cpp`

**Purpose**: Enumerate and load external ASIO drivers

**Features**:
1. **Registry Scanning**
   - Read `HKLM\SOFTWARE\ASIO` and `HKLM\SOFTWARE\WOW6432Node\ASIO`
   - Parse driver CLSIDs
   - Extract driver names and paths

2. **Driver Enumeration**
   ```cpp
   struct ASIODriverInfo {
       std::string name;
       std::string clsid;
       std::string dllPath;
       bool isValid;
   };
   ```

3. **Compatibility**
   - ASIO4ALL
   - FL Studio ASIO
   - Focusrite, Universal Audio, RME drivers
   - Any Steinberg ASIO-compliant driver

### **ASIOExternalDriver**

**File**: `AestraAudio/include/ASIOExternalDriver.h/cpp`

**Purpose**: Load and interface with external ASIO DLLs

**Features**:
1. **Dynamic Loading**
   - `LoadLibrary()` for driver DLL
   - `GetProcAddress()` for ASIO functions
   - Safe error handling

2. **ASIO Interface** (Steinberg SDK)
   - `ASIOInit()`
   - `ASIOCreateBuffers()`
   - `ASIOStart()` / `ASIOStop()`
   - `ASIOGetBufferSize()`
   - `ASIOGetSampleRate()`
   - Buffer callbacks

3. **Fallback Safety**
   - DLL not found → Skip to next driver
   - Init failed → Revert to WASAPI Exclusive
   - Runtime error → Automatic fallback

4. **Buffer Negotiation**
   - Query preferred/min/max buffer sizes
   - Negotiate sample rate with driver
   - Handle buffer size changes

## 🧠 Phase 4: Aestra ASIO (Your Own Wrapper)

### **AestraASIO.dll** (Standalone ASIO Driver)

**Purpose**: Ship a built-in ASIO driver that wraps WASAPI Exclusive

**Architecture**:
```
┌─────────────────────┐
│   DAW (FL, Reaper)  │
│                     │
└──────────┬──────────┘
           │ ASIO API
┌──────────▼──────────┐
│   AestraASIO.dll     │ ← Implements Steinberg ASIO exports
│                     │
└──────────┬──────────┘
           │ Internal Bridge
┌──────────▼──────────┐
│ WASAPIExclusiveDriver│
│                     │
└──────────┬──────────┘
           │ Windows API
┌──────────▼──────────┐
│   Audio Hardware    │
└─────────────────────┘
```

**Files**:
- `AestraAudio/ASIO/AestraASIO/`
  - `AestraASIO.cpp` (DLL exports)
  - `AestraASIO.h`
  - `AestraASIO.def` (export definitions)
  - `CMakeLists.txt`
  - `installer/` (registry setup)

**Key Exports** (ASIO API):
```cpp
ASIOError ASIOInit(ASIODriverInfo* info);
ASIOError ASIOGetChannels(long* numInputChannels, long* numOutputChannels);
ASIOError ASIOGetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity);
ASIOError ASIOGetSampleRate(ASIOSampleRate* currentRate);
ASIOError ASIOCreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
ASIOError ASIOStart();
ASIOError ASIOStop();
// ... etc
```

**Features**:
- Register as "Aestra ASIO" in Windows Registry
- Appears in all DAWs' ASIO driver lists
- Control panel UI (sample rate, buffer size)
- Bidirectional routing to WASAPI Exclusive
- Buffer format conversion if needed

**Installation**:
```registry
[HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Aestra ASIO]
"CLSID"="{...generated-guid...}"
"Description"="Aestra ASIO Driver"
```

## 🧩 Phase 5: Polishing & UI Integration

### **Driver Benchmarking**

**File**: `AestraAudio/include/DriverBenchmark.h/cpp`

**Metrics**:
- Latency (round-trip, measured)
- Dropouts/underruns per hour
- CPU load percentage
- Peak callback time
- Average callback time
- Jitter analysis

**Display**:
- Real-time latency graph
- Dropout counter
- CPU meter
- Performance grade (A-F)

### **Audio Settings UI**

**Integration Points** (in `AudioSettingsDialog`):
1. **Driver Selection Dropdown**
   - List all available drivers
   - Show priority and typical latency
   - Disable unavailable drivers (grayed out)

2. **Driver Status Display**
   - Current active driver
   - Sample rate (actual)
   - Buffer size (frames + ms)
   - Measured latency

3. **Buffer Size Control**
   - Slider or dropdown
   - Show latency estimate
   - Warning for < 128 frames

4. **Sample Rate Control**
   - List supported rates for active driver
   - Auto-detect exclusive mode rates

5. **Advanced Settings**
   - Thread priority
   - MMCSS enable/disable
   - Fallback behavior

### **Hot-Plug Detection**

- Listen for device change notifications
- Gracefully handle device disconnect
- Attempt to reopen stream on reconnect
- Notify user of device changes

## 📦 File Structure Summary

```
AestraAudio/
├── include/
│   ├── AudioDriverTypes.h          ✅ Done
│   ├── NativeAudioDriver.h         ✅ Done
│   ├── WASAPISharedDriver.h        ✅ Done
│   ├── WASAPIExclusiveDriver.h     ✅ Done
│   ├── DirectSoundDriver.h         ⏳ To Do
│   ├── AudioDriverManager.h        ⏳ To Do
│   ├── ASIODriverScanner.h         ⏳ Phase 3
│   ├── ASIOExternalDriver.h        ⏳ Phase 3
│   └── DriverBenchmark.h           ⏳ Phase 5
├── src/
│   ├── WASAPISharedDriver.cpp      ✅ Done
│   ├── WASAPIExclusiveDriver.cpp   ✅ Done
│   ├── DirectSoundDriver.cpp       ⏳ To Do
│   ├── AudioDriverManager.cpp      ⏳ To Do
│   ├── ASIODriverScanner.cpp       ⏳ Phase 3
│   ├── ASIOExternalDriver.cpp      ⏳ Phase 3
│   └── DriverBenchmark.cpp         ⏳ Phase 5
├── ASIO/
│   ├── sdk/                        ⏳ Phase 3 (Steinberg SDK)
│   └── AestraASIO/                  ⏳ Phase 4
│       ├── AestraASIO.cpp
│       ├── AestraASIO.h
│       ├── AestraASIO.def
│       ├── CMakeLists.txt
│       └── installer/
└── docs/
    └── MULTI_DRIVER_ARCHITECTURE.md ✅ This file
```

## 🚀 Implementation Roadmap

### **Week 1-2: Phase 1 Completion**
- [x] WASAPI Shared Driver
- [x] WASAPI Exclusive Driver
- [ ] DirectSound Driver
- [ ] Basic driver manager
- [ ] Fallback logic

### **Week 3-4: Phase 2 Integration**
- [ ] Complete AudioDriverManager
- [ ] UI integration for driver selection
- [ ] Performance monitoring
- [ ] Hot-plug support

### **Week 5-6: Phase 3 ASIO External**
- [ ] ASIO driver scanner
- [ ] External ASIO loader
- [ ] Testing with ASIO4ALL, FL ASIO
- [ ] Error handling and fallback

### **Week 7-8: Phase 4 Aestra ASIO**
- [ ] AestraASIO.dll implementation
- [ ] ASIO→WASAPI bridge
- [ ] Registry installer
- [ ] Control panel UI
- [ ] Testing with external DAWs

### **Week 9-10: Polish & Testing**
- [ ] Benchmarking system
- [ ] Performance optimization
- [ ] Extensive testing across devices
- [ ] Documentation and examples

## 📊 Testing Strategy

### **Test Matrix**

| Driver Type         | Device Type      | Buffer Sizes      | Sample Rates         |
|---------------------|------------------|-------------------|----------------------|
| WASAPI Shared       | Built-in/USB     | 256, 512, 1024    | 44.1, 48, 96 kHz     |
| WASAPI Exclusive    | Built-in/USB     | 64, 128, 256, 512 | 44.1, 48, 96, 192 kHz|
| DirectSound         | Any              | 1024, 2048        | 44.1, 48 kHz         |
| ASIO4ALL            | Any              | 64, 128, 256      | 44.1, 48, 96 kHz     |
| FL Studio ASIO      | FL Audio         | 128, 256, 512     | 44.1, 48 kHz         |

### **Scenarios**
1. ✅ Cold start (no audio running)
2. ⏳ Device in use (exclusive mode unavailable)
3. ⏳ Device disconnect/reconnect
4. ⏳ Sample rate change
5. ⏳ Buffer size change while running
6. ⏳ System sleep/resume
7. ⏳ Multiple Aestra instances

## 💡 Pro Tips

1. **Development Order**:
   - Get WASAPI Exclusive rock solid first
   - Use it as reference for ASIO wrapper
   - Add external ASIO only when everything else works

2. **Debugging**:
   - Log everything during development
   - Add performance counters
   - Use ETW traces for Windows audio stack
   - Test on multiple hardware configs

3. **Performance**:
   - Never allocate in audio thread
   - Pre-allocate all buffers
   - Use lock-free structures for UI communication
   - Profile callback execution time

4. **Compatibility**:
   - Test on Windows 10, 11
   - Test with USB vs built-in audio
   - Test with multiple audio interfaces
   - Verify fallback paths work

## 📚 References

- [Microsoft WASAPI Documentation](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
- [Steinberg ASIO SDK](https://www.steinberg.net/developers/)
- [DirectSound Documentation](https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ee416960(v=vs.85))
- [MMCSS Reference](https://docs.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service)

## ✅ Success Criteria

Phase 1 is considered complete when:
- ✅ WASAPI Shared works on all devices
- ✅ WASAPI Exclusive works when available
- ⏳ DirectSound works as fallback
- ⏳ Automatic fallback tested and verified
- ⏳ Statistics and monitoring operational
- ⏳ Zero crashes across all driver modes

---

**Status**: Phase 1 - 75% Complete (WASAPI drivers done, DirectSound + Manager remaining)

**Next Steps**: 
1. Implement DirectSound driver
2. Create AudioDriverManager with fallback logic
3. Integrate into AudioDeviceManager
4. Add UI for driver selection
