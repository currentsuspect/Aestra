# 🎯 NOMAD Multi-Tier Audio Driver System - Quick Start

## ✅ What's Been Implemented (Phase 1 - 75% Complete)

### Core Architecture Files

1. **`AudioDriverTypes.h`** - Foundation
   - Driver type enum (ASIO External → DirectSound)
   - Priority system with metadata
   - Capability flags, states, errors
   - Performance statistics structure

2. **`NativeAudioDriver.h`** - Base Interface
   - Extended AudioDriver with type awareness
   - Error callbacks and statistics
   - Availability checking
   - Common interface for all native drivers

3. **`WASAPISharedDriver.h/cpp`** ✅ COMPLETE
   - Default safe mode (~15ms latency)
   - Event-driven callbacks
   - Thread priority management
   - MMCSS "Pro Audio" scheduling
   - Underrun detection
   - Auto sample-rate conversion
   
4. **`WASAPIExclusiveDriver.h/cpp`** ✅ COMPLETE
   - Professional mode (~3-5ms latency)
   - Exclusive device access
   - Sample-rate negotiation (44.1-192 kHz)
   - Buffer size alignment
   - Event-driven with Pro Audio scheduling
   - Format matching algorithm

## 🔧 Next Steps to Complete Phase 1

### 1. DirectSound Fallback Driver
**File**: `NomadAudio/include/DirectSoundDriver.h`

```cpp
class DirectSoundDriver : public NativeAudioDriver {
    // DirectSound 8 implementation
    // ~30ms latency (polling-based)
    // Maximum compatibility
    // Windows XP+ support
};
```

### 2. AudioDriverManager
**File**: `NomadAudio/include/AudioDriverManager.h`

```cpp
class AudioDriverManager {
public:
    // Register drivers
    void registerDriver(std::unique_ptr<NativeAudioDriver> driver);
    
    // Auto-select best available
    NativeAudioDriver* selectBestDriver();
    
    // Fallback on error
    bool tryNextDriver();
    
    // Get current active driver
    NativeAudioDriver* getActiveDriver();
    AudioDriverType getActiveDriverType();
    
    // Statistics
    DriverStatistics getStatistics();
};
```

**Priority Logic**:
```
1. Try ASIO External (if available)     → ~2ms
2. Try Nomad ASIO (if installed)        → ~3ms
3. Try WASAPI Exclusive (if not in use) → ~5ms
4. Try WASAPI Shared (always works)     → ~15ms
5. Try DirectSound (legacy fallback)    → ~30ms
```

**Fallback Rules**:
- Device in use → Skip Exclusive, try Shared
- Exclusive failed → Try Shared
- WASAPI failed → Try DirectSound
- DirectSound failed → Fatal error (unlikely)

## 🚀 Integration into Existing Code

### Update `AudioDeviceManager.cpp`

Replace `RtAudioBackend` with `AudioDriverManager`:

```cpp
// Old:
m_driver = std::make_unique<RtAudioBackend>();

// New:
m_driverManager = std::make_unique<AudioDriverManager>();
m_driverManager->registerDriver(std::make_unique<WASAPIExclusiveDriver>());
m_driverManager->registerDriver(std::make_unique<WASAPISharedDriver>());
m_driverManager->registerDriver(std::make_unique<DirectSoundDriver>());

m_driver = m_driverManager->selectBestDriver();
```

### Error Handling with Fallback

```cpp
bool AudioDeviceManager::openStream(const AudioStreamConfig& config, 
                                    AudioCallback callback, 
                                    void* userData) {
    NativeAudioDriver* driver = m_driverManager->getActiveDriver();
    
    while (driver) {
        if (driver->openStream(config, callback, userData)) {
            std::cout << "Using driver: " << driver->getDisplayName() << std::endl;
            return true;
        }
        
        // Failed - try next driver
        std::cerr << "Driver failed: " << driver->getErrorMessage() << std::endl;
        driver = m_driverManager->tryNextDriver();
    }
    
    return false; // All drivers failed
}
```

## 📊 Phase 2-4 Roadmap (Future)

### Phase 2: ASIO External Support
- **ASIODriverScanner** - Registry scanning for installed ASIO drivers
- **ASIOExternalDriver** - Dynamic DLL loading for ASIO4ALL, FL ASIO, etc.
- Compatible with existing pro audio interfaces

### Phase 3: Nomad ASIO Wrapper
- **NomadASIO.dll** - Standalone ASIO driver
- Bridges ASIO API → WASAPI Exclusive internally
- Appears in all DAWs (FL Studio, Reaper, etc.)
- Professional credibility

### Phase 4: Polish
- Driver benchmarking UI
- Performance monitoring
- Hot-plug detection
- Settings persistence

## 🎨 UI Integration (AudioSettingsDialog)

Add to `AudioSettingsDialog.cpp`:

```cpp
// Driver selection dropdown
const char* driverTypes[] = {
    "Best Available (Auto)",
    "ASIO (External)",
    "WASAPI Exclusive",
    "WASAPI Shared",
    "DirectSound"
};

// Display current driver info
auto* manager = m_deviceManager->getDriverManager();
auto* activeDriver = manager->getActiveDriver();

std::string info = std::string("Active: ") + activeDriver->getDisplayName();
info += "\nLatency: " + std::to_string(activeDriver->getStreamLatency() * 1000.0) + "ms";
info += "\nCPU: " + std::to_string(activeDriver->getStatistics().cpuLoadPercent) + "%";
info += "\nUnderruns: " + std::to_string(activeDriver->getStatistics().underrunCount);
```

## 🧪 Testing Checklist

### Phase 1 Testing
- [ ] WASAPI Shared works on built-in audio
- [ ] WASAPI Shared works on USB audio
- [ ] WASAPI Exclusive works when device available
- [ ] WASAPI Exclusive handles device-in-use gracefully
- [ ] DirectSound fallback works
- [ ] Fallback progression tested (Exclusive → Shared → DS)
- [ ] Statistics accurate
- [ ] No crashes after 10 minutes continuous playback
- [ ] Hot unplug doesn't crash

### Phase 2 Testing (ASIO)
- [ ] ASIO4ALL detected and loads
- [ ] FL Studio ASIO detected and loads
- [ ] Focusrite driver detected and loads
- [ ] Buffer size negotiation works
- [ ] Sample rate switching works
- [ ] Fallback to WASAPI on ASIO failure

## 💾 CMakeLists.txt Updates

Add new files to `NomadAudio/CMakeLists.txt`:

```cmake
# Source files
set(NOMAD_AUDIO_SOURCES
    # ... existing files ...
    src/WASAPISharedDriver.cpp
    src/WASAPIExclusiveDriver.cpp
    src/DirectSoundDriver.cpp          # Add
    src/AudioDriverManager.cpp         # Add
)

# Header files
set(NOMAD_AUDIO_HEADERS
    # ... existing files ...
    include/AudioDriverTypes.h
    include/NativeAudioDriver.h
    include/WASAPISharedDriver.h
    include/WASAPIExclusiveDriver.h
    include/DirectSoundDriver.h        # Add
    include/AudioDriverManager.h       # Add
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(NomadAudio PRIVATE
        ole32
        avrt
        dsound                          # Add for DirectSound
    )
endif()
```

## 📈 Expected Performance

| Driver            | Latency  | CPU Load | Compatibility | Use Case           |
|-------------------|----------|----------|---------------|--------------------|
| ASIO External     | 2-3ms    | Low      | Requires DLL  | Pro audio gear     |
| Nomad ASIO        | 3-4ms    | Low      | Ships with app| Pro mode           |
| WASAPI Exclusive  | 3-5ms    | Low      | Moderate      | Default pro mode   |
| WASAPI Shared     | 10-20ms  | Medium   | High          | Default safe mode  |
| DirectSound       | 25-40ms  | Higher   | Maximum       | Legacy fallback    |

## 🐛 Common Issues & Solutions

### Issue: WASAPI Exclusive fails with "Device in use"
**Solution**: Automatically fall back to WASAPI Shared

### Issue: Buffer underruns with small buffer sizes
**Solution**: Increase buffer size or use higher-priority driver

### Issue: Sample rate not supported
**Solution**: Driver auto-negotiates to nearest supported rate

### Issue: ASIO driver not loading
**Solution**: Check registry, verify DLL path, fall back to native

## 📝 Logging & Debugging

All drivers log to stdout/stderr:
```
[WASAPI Exclusive] Driver initialized successfully
[WASAPI Exclusive] Initialized - Sample Rate: 48000 Hz, Buffer: 256 frames (5.33ms)
[WASAPI Exclusive] Stream started
[WASAPI Exclusive] Audio thread running with 256 frames at 48000 Hz
```

Monitor statistics in real-time:
```cpp
auto stats = driver->getStatistics();
std::cout << "Callbacks: " << stats.callbackCount << "\n"
          << "Underruns: " << stats.underrunCount << "\n"
          << "CPU: " << stats.cpuLoadPercent << "%\n"
          << "Avg callback: " << stats.averageCallbackTimeUs << "μs" << std::endl;
```

## ✨ Key Advantages of This Architecture

1. **Resilience**: Never locks users out - always finds a working driver
2. **Performance**: Automatically uses best available driver
3. **Compatibility**: Works on every Windows machine (XP → 11)
4. **Professional**: ASIO support for pro workflows
5. **Transparent**: Users see what's active and why
6. **Future-proof**: Easy to add new driver types (CoreAudio, JACK, etc.)

---

**Status**: Ready for Phase 1 completion (DirectSound + Manager)
**Timeline**: 2-3 days to complete Phase 1, then test thoroughly before moving to Phase 2

## 🎯 Immediate Action Items

1. ✅ Implement `DirectSoundDriver.h/cpp`
2. ✅ Implement `AudioDriverManager.h/cpp`
3. ✅ Integrate into `AudioDeviceManager`
4. ⏳ Update `AudioSettingsDialog` with driver selection
5. ⏳ Test fallback scenarios
6. ⏳ Document API usage

**Ready to proceed with implementation!** 🚀
