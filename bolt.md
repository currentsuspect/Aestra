# Bolt: Innovations for Aestra

As the performance and quality agent "Bolt", I propose the following innovations and improvements to make Aestra the strongest DAW.

## 1. Innovations

### NeuralFX Suite

Expand the prototype `NeuralAmp` into a full suite of differentiable DSP plugins.
- **NeuralCab**: Convolutional/Recurrent models for cabinet simulation.
- **NeuralComp**: Modeling vintage opto/FET compressors using LSTMs.
- **Architecture**: Use AVX-512 optimized inference (hand-written kernels) instead of generic matrix ops.
- **Training**: Provide a Python script to let users train their own models from hardware.

### TurboSampler (Zero-Copy Streaming)

Current `SamplerPlugin` loads entire samples into RAM.
- **Innovation**: Implement memory-mapped file streaming (using `mmap` on Linux/Windows) with a lock-free ring buffer for the audio thread.
- **Benefit**: Instant load times for multi-GB libraries, near-zero RAM footprint.

### HyperGraph Audio Engine

Move from a linear processing list to a DAG (Directed Acyclic Graph) task scheduler.
- **Innovation**: Analyze dependency graph of tracks/busses. Dispatch independent branches to separate threads using a lock-free work-stealing scheduler.
- **Benefit**: Massive multi-core scaling (e.g., 64-core Threadripper support) with strictly wait-free execution paths.

### Dynamic Oversampling

- **Innovation**: Automatically scale internal processing resolution (2x, 4x, 8x) depending on the context (real-time playback vs. offline render) and CPU load headroom.
- **Benefit**: Guarantees artifact-free mixdowns while maintaining a responsive UI and glitch-free audio during live sessions.

### WASM Sandboxed Plugins

- **Innovation**: Run third-party VST3s inside a WebAssembly container (using `wasm2c` or similar).
- **Benefit**: Plugin crashes never crash the DAW. Security against malicious plugins.

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection

- **Status**: Missing global GC for audio thread resources.
- **Plan**: Implement a `GarbageCollector` singleton using the "Zombie Queue" pattern.
- **Benefit**: Eliminates all mutexes from `processBlock` paths (specifically fixing `SamplerPlugin`).

### Zero-Allocation UI

- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

## 3. Sound Quality

### 64-bit End-to-End Mixing

- **Plan**: Ensure `AudioBuffer` supports `double` precision.
- **Benefit**: -300dB noise floor, preventing truncation errors in complex chains.

### True-Peak Limiting

- **Plan**: Add 4x oversampling to the Master Limiter to catch inter-sample peaks.
- **Algo**: Use `Sinc32Turbo` for upsampling, apply lookahead limiting, then downsample.

### Phase-Linear EQs

- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

### Spectral Anti-Aliasing

- **Plan**: Develop real-time spectral shaping algorithms to suppress aliasing natively without relying entirely on traditional oversampling filters.
- **Benefit**: Significantly sharper transients and uncompromised high frequencies, even under extreme distortion or saturation.

## 4. Fixes & Cleanups

### Real-Time Safety

- **Violation**: `SamplerPlugin` uses `std::unique_lock` in `process()`.
- **Fix**: Replaced with `std::atomic<std::shared_ptr>` + Deferred Reclamation (GC).
- **Violation**: `EffectChain` deleted operators (False Positive in audit, but good to know).

### Platform Abstraction Leaks

- **Fix**: Ensure `#include <windows.h>` is properly wrapped and conditionally compiled or marked as allowed with `// ALLOW_PLATFORM_INCLUDE` to pass `scripts/check_platform_leaks.py`. Verified for:
  - `AestraCore/include/AestraThreading.h`
  - `AestraAudio/include/Core/AudioEngine.h`
  - `AestraAudio/include/Drivers/ASIOInterface.h`

---
*Signed: Bolt*
