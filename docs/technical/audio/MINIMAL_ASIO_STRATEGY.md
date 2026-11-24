# ASIO support

ASIO is a Windows-only driver model. Aestra loads an installed ASIO driver over COM and streams
through it, so an interface whose own driver beats its WASAPI endpoint can be used at its own
latency. The implementation is `AestraAudio/src/Drivers/ASIODriver.cpp`.

This page describes what the code does today. It replaced an earlier strategy note written before
the driver existed, which said Aestra would only *detect* ASIO drivers and run everything through
WASAPI. That is no longer true.

## Where Aestra looks

`ASIODriver::getDevices()` reads `HKLM\SOFTWARE\ASIO` — one subkey per installed driver — taking
`CLSID` and `Description`, and falling back to the key name when there is no description. Each
entry becomes a device of type `AudioDriverType::ASIO_EXTERNAL`.

The channel counts and sample rates reported at this stage are guesses: the driver has not been
loaded yet, so nothing has been asked. They are refined when a stream opens.

## How a driver is loaded

`loadDriver()`:

1. Initialises COM apartment-threaded (`CoInitializeEx(..., COINIT_APARTMENTTHREADED)`). An MTA
   session is tolerated, but it is not what ASIO expects.
2. `CLSIDFromString` → `CoCreateInstance(CLSCTX_INPROC_SERVER)`.
3. `QueryInterface(IID_IASIO)` — `{47d6d9d4-e949-11d1-86ba-006008caaa53}` — with a fallback that
   queries the driver's own CLSID as the interface ID, because some older drivers do that.
4. `init(hwnd)`, trying `GetForegroundWindow()`, then `GetDesktopWindow()`, then `nullptr`.
   ASIO4ALL and others behave better with an owned window handle.

`IASIO` itself is declared in `AestraAudio/include/Drivers/ASIOInterface.h`: the vtable and the
4-byte-packed structs are written out in the tree. Aestra therefore has no Steinberg SDK
dependency and links nothing against one — but it does load and drive the driver.

## Opening a stream

`openStream()`:

- Asks `getChannels()` and `getBufferSize()`. The requested buffer size is clamped into the
  driver's min/max, so a driver that only offers 256 frames gets 256.
- Calls `createBuffers()` with one planar buffer per active channel.
- Sample rate: if the driver's current rate differs from the requested one and `canSampleRate()`
  allows it, `setSampleRate()` is called. Otherwise the stream runs at the driver's rate and the
  engine is told so through `getStreamSampleRate()`.

## Inside the callback

`asioBufferSwitch()` runs on the driver's own thread:

1. On the first callback, the thread is raised to pro-audio priority through `MMCSS::setProAudio()`
   (`AestraCore/include/AestraThreading.h`).
2. Input is de-interleaved into a buffer allocated in `openStream`.
3. The engine's callback runs.
4. Output is interleaved back and clamped to ±1.0 for integer formats.
5. `outputReady()` is called.

Sample formats converted: `ASIOSTInt16LSB`, `ASIOSTInt32LSB`, `ASIOSTFloat32LSB`. Any other format
a driver reports leaves that channel silent — the buffers are zeroed rather than guessed at.

Nothing allocates inside the callback. The scratch buffers are sized once, when the stream opens.

## Latency

`getStreamLatency()` returns `(inputLatency + outputLatency) / sampleRate` from the driver's own
`getLatencies()`. It is what the driver reports, not a measurement Aestra takes.

## Limits worth knowing

- **One ASIO driver per process.** Callback routing goes through a single static instance, so two
  live engines cannot hold two different ASIO drivers at the same time.
- **`isAvailable()` returns true on Windows unconditionally.** The ASIO entry therefore appears in
  the driver list even on a machine with no ASIO driver installed; choosing it fails at
  `loadDriver()` with the COM error.
- **Only three sample formats are converted.** Packed 24-bit and MSB-first formats are not.
- **Device capabilities are guesses until a stream opens.**

## Fallback

`AestraAudio/src/Win32/WindowsDriverRegistry.cpp` registers ASIO first, then WASAPI Exclusive, then
WASAPI Shared, then RtAudio, then a silent dummy driver. An ASIO failure is not fatal: the engine
falls through to the next driver.
