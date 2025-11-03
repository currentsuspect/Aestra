# NomadAudio

**Real-time audio engine powered by RtAudio**

## Purpose

NomadAudio provides low-latency cross-platform audio I/O:
- Audio device management
- Real-time audio callbacks
- DSP processing (filters, oscillators, envelopes)
- Mixer and routing
- Lock-free UI ↔ Audio communication

## Structure

```
NomadAudio/
├── include/
│   ├── NomadAudio.h
│   ├── AudioDeviceManager.h
│   ├── AudioDriver.h
│   ├── Mixer.h
│   ├── DSP/
│   │   ├── Filters.h
│   │   ├── Oscillator.h
│   │   └── Envelope.h
│   └── Utils/
│       ├── RingBuffer.h
│       └── AudioMath.h
└── src/
    ├── RtAudioBackend.cpp
    ├── Mixer.cpp
    └── DSP/
```

## Audio Flow

```
RtAudioCallback → NomadAudioEngine → MixerBus → OutputBuffer
```

## Supported APIs

- **Windows:** WASAPI, ASIO
- **macOS:** CoreAudio
- **Linux:** ALSA, JACK, PipeWire

## Design Principles

- **<10ms latency** - Real-time performance
- **No blocking calls** - Audio thread is sacred
- **Lock-free communication** - Ring buffers for UI data
- **RtAudio backend** - v1.0 uses RtAudio (MIT license)

## Status

✅ **RtAudio Integrated** - v1.0.0

### Completed
- ✅ RtAudio library integration (MIT license)
- ✅ CMake build configuration
- ✅ AudioDeviceManager implementation
- ✅ Cross-platform audio I/O (WASAPI on Windows)
- ✅ Basic audio test application

### Next Steps
1. Implement AudioDeviceManager device switching
2. Create lock-free ring buffer for UI↔Audio communication
3. Build basic mixer with gain control
4. Add DSP modules (oscillators, filters, envelopes)

---

*"No blocking calls in audio thread."*
