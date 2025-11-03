# RtAudio Integration Guide

## Overview

NomadAudio uses **RtAudio v6.0.1** (MIT License) as its cross-platform audio backend.

## Integration Details

### Files Added
- `External/rtaudio/RtAudio.h` - RtAudio header
- `External/rtaudio/RtAudio.cpp` - RtAudio implementation
- `External/rtaudio/LICENSE` - MIT license

New: `NomadASIOBackend` - A Nomad wrapper that prefers ASIO on Windows when RtAudio is built with ASIO support. If ASIO isn't available, the wrapper falls back to the default RtAudio behavior. To enable full ASIO support you may need to build RtAudio with ASIO support and/or provide the ASIO SDK to the build.
How to enable ASIO (summary)
1. Extract the ASIO SDK zip somewhere on your machine. The project will look for the SDK in a few places.
    - Recommended locations:
       - Inside the repo (recommended for local builds): `NomadAudio/External/ASIOSDK` — convenient for local development but do NOT commit the vendor files to Git; add them to `.gitignore`.
       - Outside the repo: `C:/ASIOSDK` (system-wide)
       - Or under your user folder: `%USERPROFILE%\ASIOSDK`

2. Configure the build and point CMake to the SDK path. From the repository root (PowerShell):

```powershell
# Example: extract zip then configure
# Expand-Archive -Path C:\path\to\ASIOSDK.zip -DestinationPath C:\ASIOSDK -Force
# cmake -S . -B build -G "Visual Studio 17 2022" -DDASIO_SDK_ROOT="C:/ASIOSDK"
# cmake --build build --config Release
```

3. If RtAudio is compiled with ASIO support, `NomadASIOBackend` will detect that at runtime and request ASIO. If not, the backend will fall back to the default Windows API (WASAPI/DirectSound) and log that ASIO is not available.

Notes:
- The Steinberg ASIO SDK is covered by a separate license and may not be redistributed. Keep the SDK outside of source control and respect its license.
- If you want ASIO4ALL to be used at runtime, you still need RtAudio to be compiled with ASIO support; ASIO4ALL provides the driver at runtime but the library must be built to talk to ASIO.

### Platform Support

| Platform | Audio API | Status |
|----------|-----------|--------|
| Windows  | WASAPI    | ✅ Tested |
| Windows  | DirectSound | ✅ Available |
| macOS    | CoreAudio | ✅ Available |
| Linux    | ALSA      | ✅ Available |
| Linux    | JACK      | ⚠️ Optional |

### Build Configuration

The CMake configuration automatically selects the appropriate audio API:

```cmake
# Windows: WASAPI (primary)
add_definitions(-D__WINDOWS_WASAPI__)

# macOS: CoreAudio
add_definitions(-D__MACOSX_CORE__)

# Linux: ALSA
add_definitions(-D__LINUX_ALSA__)
```

### API Differences (RtAudio v6)

RtAudio v6 introduced breaking changes from v5:

1. **Error Handling**: Uses error callbacks instead of exceptions
   ```cpp
   // v6: Error callback
   rtAudio->setErrorCallback([](RtAudioErrorType type, const std::string& msg) {
       std::cerr << "Error: " << msg << std::endl;
   });
   ```

2. **Device Enumeration**: Uses `getDeviceIds()` instead of `getDeviceCount()`
   ```cpp
   // v6: Get device IDs
   std::vector<unsigned int> ids = rtAudio->getDeviceIds();
   ```

3. **Return Values**: Functions return `RtAudioErrorType` instead of throwing
   ```cpp
   // v6: Check return value
   RtAudioErrorType error = rtAudio->openStream(...);
   if (error != RTAUDIO_NO_ERROR) {
       // Handle error
   }
   ```

## Testing

Run the test application to verify audio output:

```bash
# Build
cmake --build build --config Release --target NomadAudioTest

# Run
./build/NomadAudio/Release/NomadAudioTest.exe
```

The test plays a 440 Hz sine wave for 3 seconds.

## Performance

- **Latency**: <10ms (tested with 512 sample buffer @ 48kHz)
- **Sample Rates**: 44.1kHz, 48kHz, 96kHz (device dependent)
- **Buffer Sizes**: 128, 256, 512, 1024 frames

## License Compliance

RtAudio is licensed under the MIT License, which is compatible with NOMAD's MIT license.

**Attribution Required**: Yes (included in `External/rtaudio/LICENSE`)

## References

- [RtAudio GitHub](https://github.com/thestk/rtaudio)
- [RtAudio Documentation](http://www.music.mcgill.ca/~gary/rtaudio/)
- [RtAudio v6 Migration Guide](https://github.com/thestk/rtaudio/blob/master/doc/release.txt)

---

**Last Updated**: January 2025  
**RtAudio Version**: 6.0.1  
**Integration Status**: ✅ Complete
